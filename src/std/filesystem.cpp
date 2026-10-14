// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/std/filesystem.cpp
// ────────────────────────────────────────────────────────
#include "fls/std/filesystem.hpp"
#include <cerrno> // errno
#include <cstdint>
#include <fcntl.h>
#include <filesystem> // std::filesystem::path, exists, is_directory, is_regular_file
#include <fstream>
#include <ios>       // std::ios::binary
#include <stdexcept> // std::runtime_error
#include <sys/stat.h>
#include <system_error> // std::system_error / std::error_code
#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
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
#if defined(_WIN32)
	int        fd    = -1;
	const auto error = ::_wsopen_s(&fd, file.c_str(), _O_RDONLY | _O_BINARY | _O_NOINHERIT, _SH_DENYNO, 0);
	if (error != 0) {
		throw std::system_error(error, std::generic_category(), file.string());
	}
#else
	const int   fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
#endif
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

int64_t FileSystem::read_file_size(const int fd) {
#if defined(_WIN32)
	struct _stat64 st {};
	const int      result = ::_fstat64(fd, &st);
#else
	struct stat st {};
	const int   result = ::fstat(fd, &st);
#endif
	if (result != 0) {
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
#if defined(_WIN32)
	::_close(fd);
#else
	::close(fd);
#endif
}

template void FileSystem::close(std::ifstream&);
template void FileSystem::close(std::ofstream&);

} // namespace fastlanes
