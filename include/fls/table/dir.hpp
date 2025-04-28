#ifndef FLS_TABLE_DIR_HPP
#define FLS_TABLE_DIR_HPP

#include "fls/common/common.hpp"
#include "fls/std/filesystem.hpp"
#include "fls/std/string.hpp"

namespace fastlanes {
//! The possible states for a file of an external format.
enum class FileT : uint8_t {
	INVALID = 0,
	// Supported formats.
	CSV = 1,
	JSON = 2,
	// TODO: Implement
	CONSOLE = 3,
    };

inline constexpr auto CSV = FileT::CSV;
inline constexpr auto JSON = FileT::JSON;
inline constexpr auto CONSOLE = FileT::CONSOLE;

/*!
 * A directory contains the required information to construct a FastLanes representation from an external file
 * format.
 */
class Dir {
public:
	//! TODO
	string table_name;
	//! System path to the data file.
	path file_path;
	//! System path to the schema file, used to encode the file_path.
	path schema_path;
	//! Filetype of file_path, used to invoke the respective parser.
	FileT file_t;
};
} // namespace fastlanes

#endif // FLS_TABLE_DIR_HPP
