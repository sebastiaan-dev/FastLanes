// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/include/fls/io/file.hpp
// ────────────────────────────────────────────────────────
#ifndef FLS_IO_FILE_HPP
#define FLS_IO_FILE_HPP

#include "fls/common/common.hpp"
#include "fls/std/filesystem.hpp"
#include "fls/std/string.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <sys/types.h>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace fastlanes {
/*--------------------------------------------------------------------------------------------------------------------*/
class Buf;
/*--------------------------------------------------------------------------------------------------------------------*/

class ThreadPool {
public:
	explicit ThreadPool(size_t n)
	    : stop_(false) {
		workers_.reserve(n ? n : 1);
		for (size_t i = 0; i < (n ? n : 1); ++i) {
			workers_.emplace_back([this] {
				for (;;) {
					std::function<void()> job;
					{
						std::unique_lock<std::mutex> lk(m_);
						cv_.wait(lk, [this] { return stop_ || !q_.empty(); });
						if (stop_ && q_.empty())
							return;
						job = std::move(q_.front());
						q_.pop();
					}
					job();
				}
			});
		}
	}

	~ThreadPool() {
		{
			std::lock_guard<std::mutex> lk(m_);
			stop_ = true;
		}
		cv_.notify_all();
		for (auto& t : workers_)
			t.join();
	}

	template <class F, class... Args>
	auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
		using R = std::invoke_result_t<F, Args...>;

		// wrap the task in a shared_ptr so the lambda is copyable
		auto task =
		    std::make_shared<std::packaged_task<R()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

		std::future<R> fut = task->get_future();
		{
			std::lock_guard<std::mutex> lk(m_);
			q_.emplace([task]() mutable { (*task)(); });
		}
		cv_.notify_one();
		return fut;
	}

private:
	std::vector<std::thread>          workers_;
	std::queue<std::function<void()>> q_;
	std::mutex                        m_;
	std::condition_variable           cv_;
	bool                              stop_;
};

class ComputeExecutor {
public:
	static ThreadPool& pool() {
		static ThreadPool p {pick_pool_size()};
		return p;
	}

private:
	static size_t pick_pool_size() {
		const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
		return std::min<size_t>(hw, 12);
	}
};

class IOExecutor {
public:
	static ThreadPool& pool() {
		static ThreadPool p {pick_pool_size()};
		return p;
	}

private:
	static size_t pick_pool_size() {
		const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
		return std::min<size_t>(hw, 12);
	}
};

class File {
public:
	explicit File(const path& path);
	~File();

	File(const File&)            = delete;
	File& operator=(const File&) = delete;

public:
	// write to file_path
	void Write(const Buf& buf);
	// write to file_path
	void Read(const Buf& buf);
	// write to file_path
	void Append(const Buf& buf);
	// Append
	void Append(const char* pointer, n_t size);
	//
	void ReadRange(const Buf& buf, n_t offset, n_t size);
	// get file size
	[[nodiscard]] n_t Size() const;

public:
	/// read from file_path and return string.
	static string read(const path& file_path);
	/// write to file_path
	static void write(const path& file_path, const string& dump);
	/// append to file_path
	static void append(const path& file_path, const string& dump);

private:
	void EnsureReadOpen() const;
	void ReadInternal(uint8_t* dst, n_t size, off_t offset = 0) const;

private:
	path              m_path;
	up<std::ofstream> m_of_stream;
	// Defer read-only I/O so a File can also create a new output file. call_once
	// publishes the descriptor and size safely to concurrent positional readers.
	mutable std::once_flag m_read_once;
	mutable int            m_fd {-1};
	mutable n_t            m_file_size {0};
};

struct ReadEvent {
	uint64_t off;
	uint32_t size;
	uint64_t ns;       // wall time of the syscall
	uint32_t inflight; // number of reads in flight incl. this one
	uint64_t tid;
};

class IoTracer {
public:
	static IoTracer& get() {
		static IoTracer T;
		return T;
	}

	bool enabled() const {
		return enabled_;
	}

	// Call before syscall; returns "inflight after increment" for recording.
	uint32_t on_submit() {
		auto     v    = inflight_.fetch_add(1, std::memory_order_relaxed) + 1;
		uint32_t prev = max_inflight_.load(std::memory_order_relaxed);
		while (v > prev && !max_inflight_.compare_exchange_weak(prev, v)) {}
		return v;
	}
	// Call after syscall.
	void on_complete() {
		inflight_.fetch_sub(1, std::memory_order_relaxed);
	}

	void record(uint64_t off, uint32_t size, uint64_t ns, uint32_t inflight) {
		if (!enabled_)
			return;
		std::lock_guard<std::mutex> lk(mu_);
		ev_.push_back(ReadEvent {off, size, ns, inflight, tid64()});
		ops_++;
		bytes_ += size;
		ns_total_ += ns;
	}

	void dump_summary(const char* label = "pread") {
		if (!enabled_)
			return;

		std::vector<uint64_t> lat;
		{
			std::lock_guard<std::mutex> lk(mu_);
			lat.reserve(ev_.size());
			for (auto& e : ev_)
				lat.push_back(e.ns);
		}
		std::sort(lat.begin(), lat.end());

		auto p = [&](double q) -> double {
			if (lat.empty())
				return 0.0;
			const size_t n = lat.size();
			// cast size_t to double explicitly before multiply
			double pos = q * static_cast<double>(n - 1);
			size_t idx = static_cast<size_t>(pos + 0.5);
			if (idx >= n)
				idx = n - 1;
			return static_cast<double>(lat[idx]) / 1e6; // ms
		};

		double sec  = static_cast<double>(ns_total_.load()) / 1e9;
		double gb   = static_cast<double>(bytes_.load()) / 1e9;
		double gbps = sec > 0 ? gb / sec : 0.0;

		std::fprintf(stderr,
		             "[%s] ops=%llu bytes=%.3f GB time=%.3f s bw=%.2f GB/s "
		             "p50=%.2f ms p95=%.2f ms maxQD=%u\n",
		             label,
		             static_cast<unsigned long long>(ops_.load()),
		             gb,
		             sec,
		             gbps,
		             p(0.50),
		             p(0.95),
		             static_cast<unsigned>(max_inflight_.load()));
	}

private:
	IoTracer() {
#if defined(_MSC_VER)
		char*  e      = nullptr;
		size_t length = 0;
		if (_dupenv_s(&e, &length, "FLS_IO_TRACE") == 0) {
			enabled_ = (e && *e == '1');
		}
		std::free(e);
#else
		const char* e = std::getenv("FLS_IO_TRACE");
		enabled_      = (e && *e == '1');
#endif
	}
	static uint64_t tid64() {
		auto id = std::this_thread::get_id();
		static_assert(sizeof(id) == sizeof(uint64_t) || sizeof(id) == sizeof(uint64_t) / 2, "tid size");
		uint64_t v = 0;
		std::memcpy(&v, &id, std::min(sizeof(id), sizeof(v)));
		return v;
	}

	std::atomic<bool>      enabled_ {false};
	std::atomic<uint64_t>  ops_ {0}, bytes_ {0}, ns_total_ {0};
	std::atomic<uint32_t>  inflight_ {0}, max_inflight_ {0};
	std::mutex             mu_;
	std::vector<ReadEvent> ev_;
};
} // namespace fastlanes

#endif // FLS_IO_FILE_HPP
