// Standalone regression tests for the branch-consolidation patches.
// No external data or test framework is required; checks also run under NDEBUG.
#include "fls/connection.hpp"
#include "fls/cor/lyt/buf.hpp"
#include "fls/file/file_header.hpp"
#include "fls/io/file.hpp"
#include "fls/reader/table_reader.hpp"
#include "fls/writer/file_writer.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using fastlanes::Buf;
using fastlanes::File;

void Check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename F>
void CheckThrows(F&& operation) {
	bool threw = false;
	try {
		operation();
	} catch (const std::exception&) {
		threw = true;
	}
	Check(threw, "Expected an I/O exception");
}

struct TempDirectory {
	fs::path path = fs::temp_directory_path() /
	                ("fastlanes-integration-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	TempDirectory() {
		Check(fs::create_directory(path), "Cannot create test directory");
	}
	~TempDirectory() {
		std::error_code error;
		fs::remove_all(path, error);
	}
};

void WriteText(const fs::path& path, const std::string& text) {
	std::ofstream out(path, std::ios::binary);
	out << text;
	out.close();
	Check(!out.fail(), "Cannot write fixture");
}

void TestNewOutput(const fs::path& root) {
	const auto path = root / "new.bin";
	const std::string text = "new output";
	{
		File output(path); // Must not try to open a nonexistent output for reading.
		Check(!fs::exists(path), "Constructing File should not create it");
		Buf buffer(text.size());
		buffer.Append(text.data(), text.size());
		output.Write(buffer);
	}
	File input(path);
	Check(input.Size() == text.size(), "Size must initialize the read handle");
	const Buf buffer(text.size());
	input.Read(buffer);
	Check(std::string(reinterpret_cast<const char*>(buffer.data()), text.size()) == text, "Write/read mismatch");

	const auto append_path = root / "append.bin";
	{
		File output(append_path);
		output.Append(text.data(), text.size());
		Buf suffix(1);
		suffix.Append("!", 1);
		output.Append(suffix);
	}
	Check(File::read(append_path) == text + "!", "Append must create a new file");
}

void TestConcurrentFirstRead(const fs::path& root) {
	std::string text(4096, ' ');
	for (size_t i = 0; i < text.size(); ++i) {
		text[i] = static_cast<char>('a' + i % 26);
	}
	const auto path = root / "concurrent.bin";
	WriteText(path, text);
	File input(path);
	std::atomic<bool> start {false};
	std::vector<std::future<void>> workers;
	for (size_t worker = 0; worker < 8; ++worker) {
		workers.push_back(std::async(std::launch::async, [&, worker] {
			while (!start.load()) {
				std::this_thread::yield();
			}
			for (size_t iteration = 0; iteration < 100; ++iteration) {
				const auto offset = (worker * 127 + iteration * 31) % (text.size() - 64);
				const Buf buffer(64);
				input.ReadRange(buffer, offset, 64);
				Check(input.Size() == text.size(), "Concurrent Size mismatch");
				Check(std::string(reinterpret_cast<const char*>(buffer.data()), 64) == text.substr(offset, 64),
				      "Concurrent positional read mismatch");
			}
		}));
	}
	start.store(true);
	for (auto& worker : workers) {
		worker.get();
	}
}

void TestReadErrors(const fs::path& root) {
	const auto path = root / "missing.bin";
	File input(path);
	CheckThrows([&] { (void)input.Size(); });
	// Failed call_once initialization must be retryable when the file appears.
	WriteText(path, "abcdef");
	Check(input.Size() == 6, "Retry after failed initialization failed");
	const Buf small(2);
	CheckThrows([&] { input.Read(small); });
	CheckThrows([&] { input.ReadRange(small, 5, 2); });
	CheckThrows([&] { input.ReadRange(small, 0, 3); });
	CheckThrows([&] { input.ReadRange(small, std::numeric_limits<fastlanes::n_t>::max(), 2); });
	fs::resize_file(path, 0);
	// A cached file size must not turn EOF into an infinite loop in Release.
	CheckThrows([&] { input.ReadRange(small, 0, 2); });
}

void TestEmptyFile(const fs::path& root) {
	const auto path = root / "empty.bin";
	WriteText(path, "");
	File input(path);
	Check(input.Size() == 0, "Empty file size mismatch");
	const Buf buffer(1);
	input.Read(buffer);
	input.ReadRange(buffer, 0, 0);
}

void TestWriterOptions(const fs::path& root, bool forced_pool, bool inline_footer) {
	using namespace fastlanes;
	const auto dir = root / (forced_pool ? "pool" : "schema");
	fs::create_directory(dir);
	WriteText(dir / "schema.json", R"({"columns":[{"name":"value","type":"BIGINT"}]})");
	std::string csv;
	for (int64_t i = 0; i < 1031; ++i) {
		csv += std::to_string(1000000000000LL + i) + "\n";
	}
	WriteText(dir / "values.csv", csv);
	Config config;
	config.inline_footer = inline_footer ? FLS_TRUE : FLS_FALSE;
	Connection connection(config);
	connection.set_sample_size(1);
	const vector<OperatorToken> tokens {OperatorToken::EXP_UNCOMPRESSED_I64};
	if (forced_pool) {
		connection.force_schema_pool(tokens);
	} else {
		connection.force_schema(tokens);
	}
	const auto output = dir / "output.fls";
	connection.read_csv(dir).to_fls(output);
	FileHeader header {};
	FileHeader::Load(header, output);
	Check(header.settings.inline_footer == config.inline_footer, "Connection footer option was lost");
	Check(fs::exists(dir / FOOTER_NAME) == !inline_footer, "Unexpected external footer");
	auto reader = connection.read_fls(output);
	const auto* groups = reader->get_descriptor().m_rowgroup_descriptors();
	Check(groups->size() == 1, "Expected one row group");
	Check(groups->Get(0)->m_n_tuples() == 1031, "Partial-vector row count was lost");
	const auto* operators = groups->Get(0)->m_column_descriptors()->Get(0)->encoding_rpn()->operator_tokens();
	Check(operators->size() == 1 && operators->Get(0) == tokens[0], "Forced writer encoding was lost");

	auto writer = FileWriter::Builder().WithPath(dir / "unused.fls").WithConnection(connection).WithSampleSize(3).Build();
	Check(writer->GetSampleSize() == 3, "Writer sample size was lost");
}
} // namespace

int main() {
	try {
		TempDirectory root;
		TestNewOutput(root.path);
		TestConcurrentFirstRead(root.path);
		TestReadErrors(root.path);
		TestEmptyFile(root.path);
		TestWriterOptions(root.path, false, false);
		TestWriterOptions(root.path, true, true);
		std::cout << "All 6 standalone integration cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
