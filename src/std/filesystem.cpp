// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/std/filesystem.cpp
// ────────────────────────────────────────────────────────
#include "fls/std/filesystem.hpp"
#include "fls/common/assert.hpp"
#include <cerrno>     // errno
#include <filesystem> // std::filesystem::path, exists, is_directory, is_regular_file
#include <fstream>
#include <ios>       // std::ios::binary
#include <stdexcept> // std::runtime_error
#include <sys/stat.h>
#include <system_error> // NEW – std::system_error / std::error_code
#include <unistd.h>     // for ::close

#if defined(__APPLE__)
#include <sys/_types/_off_t.h>
#include <sys/fcntl.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/types.h> // off_t
#endif

namespace fastlanes {

// Helper: capture errno once and throw
[[noreturn]] static void throw_last_error(const path& file) {
	const int             err = errno;                       // save immediately
	const std::error_code ec {err, std::generic_category()}; // portable category
	throw std::system_error {ec, file.string()};             // message = path
}

// ---------- open helpers ----------------------------------------------------
std::ifstream FileSystem::open_r(const path& file) {
	std::ifstream stream(file.c_str());
	if (!stream) {
		throw_last_error(file);
	}
	return stream;
}

int FileSystem::open_r_binary(const path& file) {
	const int fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		throw_last_error(file);
	}
	return fd;
}

std::ofstream FileSystem::open_w(const path& file) {
	std::ofstream stream(file.c_str());
	if (!stream) {
		throw_last_error(file);
	}
	return stream;
}

std::ofstream FileSystem::open_w_binary(const path& file) {
	std::ofstream stream(file.c_str(), std::ios::binary);
	if (!stream) {
		throw_last_error(file);
	}
	return stream;
}

std::ofstream FileSystem::opend_app(const path& file) {
	std::ofstream stream(file.c_str(), std::ios::app);
	if (!stream) {
		throw_last_error(file);
	}
	return stream;
}

void FileSystem::check_if_dir_exists(const path& dir_path) {
	if (!exists(dir_path) || !is_directory(dir_path)) {
		throw std::runtime_error("Directory with path | " + dir_path.string() + " | does not exist.");
	}
}

void FileSystem::check_if_file_exists(const fs::path& file_path) {
	if (!exists(file_path) || !is_regular_file(file_path)) {
		throw std::runtime_error("File with path | " + file_path.string() +
		                         " | does not exist or is not a regular file.");
	}
}

off_t FileSystem::read_file_size(const int fd) {
	struct stat st {};
	if (::fstat(fd, &st) != 0) {
		// The caller owns fd, including on failure.
		throw std::system_error(errno, std::generic_category(), "fstat");
	}
	return st.st_size;
}

template <typename STREAM>
void FileSystem::close(STREAM& stream) {
	stream.close();
}

void FileSystem::close(const int fd) {
	::close(fd);
}

template void FileSystem::close(std::ifstream&);
template void FileSystem::close(std::ofstream&);

} // namespace fastlanes
