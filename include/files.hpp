// SPDX-FileCopyrightText: 2026 Nikolai Fedorov
//
// SPDX-License-Identifier: MIT

#pragma once

/// File I/O and filesystem search utilities.

#include "common.hpp"

namespace klspw {

// --- File I/O ---

/// Read entire file into a string. Throws on empty path, missing file, or read failure.
string read_file(const path& path);

/// Write content to a file, creating parent directories as needed. Throws on empty path or write failure.
void write_file(const path& path, string_view content);

/// Write binary data to a file, creating parent directories as needed. Throws on empty path or write failure.
void write_binary_file(const path& path, std::span<const std::uint8_t> data);

// --- Filesystem search ---

/// Member function pointer to directory_entry::is_directory/is_regular_file (error_code overload).
using EntryCheck = bool (fs::directory_entry::*)(std::error_code&) const;

/// Find the first entry under `root` whose path ends with `suffix`, filtered by `check`.
opt_path find_entry(const path& root, string_view suffix, EntryCheck check);

/// Find the first directory under `root` whose path ends with `suffix`.
inline opt_path find_dir(const path& root, string_view suffix) {
  return find_entry(root, suffix, &fs::directory_entry::is_directory);
}

/// Find the first regular file under `root` whose path ends with `suffix`.
inline opt_path find_file(const path& root, string_view suffix) {
  return find_entry(root, suffix, &fs::directory_entry::is_regular_file);
}

/// Find all directories under `search_dir` that contain at least one of the `markers` files.
/// Returns canonical paths sorted lexicographically. Skips hidden directories and does not
/// descend into matched directories (avoids picking up nested subprojects).
paths find_dirs_with_markers(const path& search_dir, string_views markers);

/// Extract the filename without extension from a path string.
/// E.g., "/cache/kotlin-stdlib-2.0.0.jar" -> "kotlin-stdlib-2.0.0".
inline string file_stem(string_view p) { return path{p}.stem().string(); }

}  // namespace klspw
