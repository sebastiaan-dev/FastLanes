// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/io/file.cpp
// ────────────────────────────────────────────────────────
#include "fls/io/file.hpp"
#include "fls/common/alias.hpp"
#include "fls/common/assert.hpp"
#include "fls/cor/lyt/buf.hpp"
#include "fls/std/filesystem.hpp"
#include "fls/std/string.hpp"
#include <cerrno>
#include <cstdint>    // for int64_t
#include <filesystem> // for std::filesystem::file_size, std::filesystem::exists
#include <fstream>    // for std::ifstream, std::ofstream
#include <ios>        // for std::ios, std::streamoff, std::streamsize
#include <memory>     // for std::make_unique
#include <sstream>
#include <stdexcept> // for std::runtime_error
#include <system_error>

// POSIX
#include <unistd.h>

namespace fastlanes {

File::File(const path& p)
    : m_path(p) {
}

void File::EnsureReadOpen() const {
	std::call_once(m_read_once, [this] {
		const int fd = FileSystem::open_r_binary(m_path);
		try {
			m_file_size = static_cast<n_t>(FileSystem::read_file_size(fd));
		} catch (...) {
			FileSystem::close(fd);
			throw;
		}
		m_fd = fd;
	});
}

File::~File() {
	if (m_of_stream) {
		FileSystem::close(*m_of_stream);
	}
	if (m_fd >= 0) {
		::close(m_fd);
	}
}

void File::Write(const Buf& buf) {
	if (!m_of_stream) {
		m_of_stream = std::make_unique<std::ofstream>(FileSystem::open_w_binary(m_path));
	}
	m_of_stream->write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.Size()));
}

void File::Read(const Buf& buf) {
	EnsureReadOpen();
	if (m_file_size > buf.Capacity()) {
		throw std::out_of_range("File::Read buffer is too small");
	}

	uint8_t* dst = buf.mutable_data();
	ReadInternal(dst, m_file_size);
}

void File::Append(const Buf& buf) {
	if (!m_of_stream) {
		// Open in append mode, binary
		m_of_stream = std::make_unique<std::ofstream>(m_path, std::ios::binary | std::ios::app);
		if (!*m_of_stream) {
			throw std::runtime_error("failed to open file for append");
		}
	}
	m_of_stream->write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.Size()));
	if (!*m_of_stream) {
		throw std::runtime_error("append write failed");
	}
}

void File::Append(const char* pointer, n_t size) {
	if (!m_of_stream) {
		m_of_stream = std::make_unique<std::ofstream>(m_path, std::ios::binary | std::ios::app);
		if (!*m_of_stream) {
			throw std::runtime_error("failed to open file for append");
		}
	}
	m_of_stream->write(pointer, static_cast<std::streamsize>(size));
	if (!*m_of_stream) {
		throw std::runtime_error("append write failed");
	}
}

void File::ReadRange(const Buf& buf, const n_t offset, const n_t size) {
	EnsureReadOpen();
	if (offset > m_file_size || size > m_file_size - offset || size > buf.Capacity()) {
		throw std::out_of_range("File::ReadRange exceeds file or buffer bounds");
	}

	uint8_t* dst = buf.mutable_data();
	ReadInternal(dst, size, static_cast<off_t>(offset));
}

n_t File::Size() const {
	EnsureReadOpen();
	return m_file_size;
}

void File::ReadInternal(uint8_t* dst, const n_t size, const off_t offset) const {
	n_t done = 0;
	while (done < size) {
		const ssize_t n_bytes = ::pread(m_fd, dst + done, size - done, offset + static_cast<off_t>(done));
		if (n_bytes < 0 && errno == EINTR) {
			continue;
		}
		if (n_bytes < 0) {
			throw std::system_error(errno, std::generic_category(), "pread: " + m_path.string());
		}
		if (n_bytes == 0) {
			throw std::runtime_error("Unexpected end of file: " + m_path.string());
		}

		done += static_cast<n_t>(n_bytes);
	}
}

/*--------------------------------------------------------------------------------------------------------------------*\
 * STATIC
\*--------------------------------------------------------------------------------------------------------------------*/
string File::read(const path& file_path) {
	std::ifstream     json_stream = FileSystem::open_r(file_path);
	std::stringstream buffer;
	buffer << json_stream.rdbuf();
	return buffer.str();
}

void File::write(const path& dir_path, const string& dump) {
	auto file = FileSystem::open_w(dir_path);

	file << dump;

	FileSystem::close(file);
}

void File::append(const path& dir_path, const string& dump) {
	auto file = FileSystem::opend_app(dir_path);

	file << dump;

	FileSystem::close(file);
}

} // namespace fastlanes