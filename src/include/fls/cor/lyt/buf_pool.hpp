// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/include/fls/cor/lyt/buf_pool.hpp
// ────────────────────────────────────────────────────────
#ifndef FLS_COR_LYT_BUF_POOL_HPP
#define FLS_COR_LYT_BUF_POOL_HPP

#include "fls/cor/lyt/buf.hpp"
#include "fls/std/vector.hpp"
#include <functional>
#include <memory>
#include <mutex>

namespace fastlanes {

// Simple thread-safe pool for reusing Buf instances between row-group scans.
class BufPool : public std::enable_shared_from_this<BufPool> {
public:
	BufPool()                              = default;
	BufPool(const BufPool&)                = delete;
	BufPool& operator=(const BufPool&)     = delete;
	BufPool(BufPool&&) noexcept            = delete;
	BufPool& operator=(BufPool&&) noexcept = delete;

	// Acquire a buffer that can hold at least min_capacity bytes.
	std::shared_ptr<Buf> Acquire(n_t min_capacity);

	// Release all cached buffers.
	void Clear();

private:
	void Recycle(Buf* buffer);

	std::mutex                        mutex_;
	std::vector<std::unique_ptr<Buf>> free_list_;
};

inline std::shared_ptr<Buf> BufPool::Acquire(const n_t min_capacity) {
	std::unique_ptr<Buf> buffer;

	{
		std::lock_guard<std::mutex> guard(mutex_);
		for (auto it = free_list_.begin(); it != free_list_.end(); ++it) {
			if ((*it)->Capacity() >= min_capacity) {
				buffer = std::move(*it);
				free_list_.erase(it);
				break;
			}
		}
	}

	if (!buffer) {
		buffer = std::make_unique<Buf>(min_capacity);
	} else if (buffer->Capacity() < min_capacity) {
		buffer->Resize(min_capacity);
	}

	buffer->Reset();

	auto self    = shared_from_this();
	auto raw_buf = buffer.release();
	auto deleter = [weak_pool = std::weak_ptr<BufPool>(self)](Buf* ptr) {
		if (!ptr) {
			return;
		}
		if (auto pool = weak_pool.lock()) {
			pool->Recycle(ptr);
		} else {
			delete ptr;
		}
	};

	return std::shared_ptr<Buf>(raw_buf, std::move(deleter));
}

inline void BufPool::Clear() {
	std::lock_guard<std::mutex> guard(mutex_);
	free_list_.clear();
}

inline void BufPool::Recycle(Buf* buffer) {
	std::unique_ptr<Buf> owned(buffer);
	owned->Reset();
	std::lock_guard<std::mutex> guard(mutex_);
	free_list_.push_back(std::move(owned));
}

} // namespace fastlanes

#endif // FLS_COR_LYT_BUF_POOL_HPP
