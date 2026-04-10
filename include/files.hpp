#pragma once

/// File I/O and filesystem search utilities.

#include "common.hpp"

namespace klspw {

// --- File I/O ---

string read_file(const fs::path& path);

void write_file(const fs::path& path, string_view content);

// --- Filesystem search ---

/// Member function pointer to directory_entry::is_directory/is_regular_file (error_code overload).
using EntryCheck = bool (fs::directory_entry::*)(std::error_code&) const;

/// Find the first entry under `root` whose path ends with `suffix`, filtered by `check`.
optional<fs::path> find_entry(const fs::path& root, string_view suffix, EntryCheck check);

/// Find the first directory under `root` whose path ends with `suffix`.
inline optional<fs::path> find_dir(const fs::path& root, string_view suffix) {
    return find_entry(root, suffix, &fs::directory_entry::is_directory);
}

/// Find the first regular file under `root` whose path ends with `suffix`.
inline optional<fs::path> find_file(const fs::path& root, string_view suffix) {
    return find_entry(root, suffix, &fs::directory_entry::is_regular_file);
}

/// Extract the filename without extension from a path string.
/// E.g., "/cache/kotlin-stdlib-2.0.0.jar" -> "kotlin-stdlib-2.0.0".
inline string file_stem(string_view path) { return fs::path{path}.stem().string(); }

} // namespace klspw
