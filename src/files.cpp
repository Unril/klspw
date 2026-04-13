#include "files.hpp"

#include <fstream>

namespace klspw {

string read_file(const path& path) {
  require(!path.empty(), "read_file: empty path");
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  require(!ec && !std::cmp_equal(size, -1), "Cannot determine file size for {}: {}", path, ec);
  require(std::in_range<size_t>(size), "File too large: {}", path);
  std::ifstream file(path, std::ios::in | std::ios::binary);
  require(file.good(), "Failed to open file: {}", path);
  string content(static_cast<size_t>(size), '\0');
  file.read(content.data(), static_cast<std::streamsize>(size));
  require(file.good(), "Failed to read file (got {}/{}): {}", [&] { return file.gcount(); }, size, path);
  return content;
}

void write_file(const path& path, string_view content) {
  require(!path.empty(), "write_file: empty path");
  std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
  require(file.good(), "Failed to open file for writing: {}", path);
  file.write(content.data(), static_cast<std::streamsize>(content.size()));
  require(file.good(), "Failed to write file: {}", path);
}

opt_path find_entry(const path& root, string_view suffix, EntryCheck check) {
  std::error_code ec;
  for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
    if ((entry.*check)(ec) && entry.path().native().ends_with(suffix)) {
      return entry.path();
    }
  }
  return nullopt;
}

namespace {

bool is_hidden(const path& path) {
  const auto name = path.filename().string();
  return !name.empty() && name[0] == '.';
}

bool has_any_marker(const path& dir, string_views markers) {
  return r::any_of(markers, [&](string_view m) { return fs::is_regular_file(dir / m); });
}

}  // namespace

paths find_dirs_with_markers(const path& search_dir, string_views markers) {
  require(fs::is_directory(search_dir), "search directory does not exist: {}", search_dir);
  require(!markers.empty(), "at least one marker filename is required");

  paths roots;
  std::error_code ec;

  for (auto it = fs::recursive_directory_iterator(search_dir, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const auto& entry = *it;
    if (!entry.is_directory(ec)) {
      continue;
    }
    if (is_hidden(entry.path())) {
      it.disable_recursion_pending();
      continue;
    }
    if (has_any_marker(entry.path(), markers)) {
      roots.push_back(fs::weakly_canonical(entry.path()));
      it.disable_recursion_pending();
    }
  }

  r::sort(roots);
  return roots;
}

}  // namespace klspw
