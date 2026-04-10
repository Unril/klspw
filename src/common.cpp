#include "common.hpp"

#include <fstream>

namespace klspw {

string read_file(const fs::path& path) {
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

void write_file(const fs::path& path, string_view content) {
    require(!path.empty(), "write_file: empty path");
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    require(file.good(), "Failed to open file for writing: {}", path);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    require(file.good(), "Failed to write file: {}", path);
}

opt_string extract_between(string_view input, Delimiters delimiters) {
    require(!delimiters.open.empty(), "extract_between: open delimiter must not be empty");
    require(!delimiters.close.empty(), "extract_between: close delimiter must not be empty");
    const auto begin_pos = input.find(delimiters.open);
    if (begin_pos == string_view::npos) {
        return nullopt;
    }
    const auto content_start = begin_pos + delimiters.open.size();
    const auto end_pos = input.find(delimiters.close, content_start);
    if (end_pos == string_view::npos) {
        return nullopt;
    }
    return string{trim(input.substr(content_start, end_pos - content_start))};
}

optional<fs::path> find_entry(const fs::path& root, string_view suffix, EntryCheck check) {
    std::error_code ec;
    for (const auto& entry :
        fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (std::mem_fn(check)(entry, ec) && entry.path().native().ends_with(suffix)) {
            return entry.path();
        }
    }
    return nullopt;
}

} // namespace klspw
