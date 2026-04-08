#pragma once

/// Shared type aliases, namespace imports, string utilities, and glaze opts.

#include <algorithm>
#include <cstdint> // IWYU pragma: keep
#include <filesystem> // IWYU pragma: keep
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <glaze/glaze.hpp>

namespace klspw {

// --- Type imports ---

using std::format;
using std::map;
using std::move;
using std::nullopt;
using std::optional;
using std::runtime_error;
using std::set;
using std::size_t;
using std::string;
using std::string_view;
using std::to_string;
using std::variant;
using std::vector;
using std::visit;

namespace r = std::ranges;
namespace v = std::views;
namespace fs = std::filesystem;

using strings = vector<string>;
using opt_string = optional<string>;

// --- Glaze opts ---

/// Write opts: skip null optionals, pretty-print for diffable output.
struct ws_write_opts_t : glz::opts {
    bool prettify = true;
    uint8_t indentation_width = 2;
};
inline constexpr ws_write_opts_t ws_write_opts{};

/// Read opts: ignore unknown keys for forward-compat.
inline constexpr glz::opts ws_read_opts{.error_on_unknown_keys = false};

// --- String utilities ---

/// Trim leading/trailing whitespace (space, \n, \r) from a string_view.
/// Tabs are intentionally not trimmed -- Gradle output uses spaces/newlines.
inline string_view trim(string_view sv) {
    constexpr string_view ws = " \n\r";
    const auto start = sv.find_first_not_of(ws);
    if (start == string_view::npos) {
        return {};
    }
    return sv.substr(start, sv.find_last_not_of(ws) - start + 1);
}

/// Join any range of string-like elements with a separator (C++23).
template <r::input_range R>
    requires std::convertible_to<r::range_value_t<R>, string_view>
inline string join(R&& parts, string_view sep = " ") {
    auto view = std::forward<R>(parts) | v::join_with(sep);
    return {std::from_range, view};
}

inline string join(std::initializer_list<string_view> parts, string_view sep = " ") {
    return join<>(parts, sep);
}

// --- Preconditions ---

namespace detail {

/// Evaluate an argument for formatting:
///   - invocable with no args → std::invoke and return result
///   - fs::path → .string() (no std::formatter for fs::path)
///   - std::error_code → .message() (no std::formatter for error_code)
///   - anything else → pass through by value
template <typename T> auto eval(T&& arg) {
    if constexpr (std::invocable<T>) {
        return std::invoke(std::forward<T>(arg));
    } else if constexpr (std::same_as<std::remove_cvref_t<T>, fs::path>) {
        return arg.string();
    } else if constexpr (std::same_as<std::remove_cvref_t<T>, std::error_code>) {
        return arg.message();
    } else {
        return std::forward<T>(arg);
    }
}

/// The type that eval() produces for a given arg, decayed for use in format_string.
template <typename T> using eval_t = std::remove_cvref_t<decltype(eval(std::declval<T>()))>;

} // namespace detail

/// Throw runtime_error if condition is false. Accepts std::format args.
/// Args may be values, zero-arg callables, fs::path, or error_code (all handled by detail::eval).
///
/// Usage:
///   require(x > 0, "Expected positive, got {}", x);
///   require(fs::exists(p), "Not found: {}", p);  // fs::path auto-converts to string
///   require(ready, "Timeout after {}ms", [&] { return elapsed.count(); });  // lazy eval
template <typename... Args>
inline void require(bool condition, std::format_string<detail::eval_t<Args>...> fmt, Args&&... args) {
    if (!condition) {
        throw runtime_error(format(fmt, detail::eval(std::forward<Args>(args))...));
    }
}

/// Collect elements from a range into a vector, keeping only the first occurrence
/// per key. The key is extracted via a projection function.
/// Usage: unique_by(libs_range, &LibraryData::name)
template <r::input_range R, typename Proj = std::identity> auto unique_by(R&& range, Proj proj = {}) {
    using T = r::range_value_t<R>;
    using Key = std::remove_cvref_t<std::invoke_result_t<Proj, const T&>>;
    std::unordered_set<Key> seen;
    vector<T> result;
    for (auto&& elem : std::forward<R>(range)) {
        if (seen.insert(std::invoke(proj, elem)).second) {
            result.push_back(std::forward<decltype(elem)>(elem));
        }
    }
    return result;
}

/// Extract the substring between two delimiters, trimmed.
/// Returns nullopt if either delimiter is missing.
inline opt_string extract_between(string_view input, string_view open, string_view close) { // NOLINT(bugprone-*)
    const auto begin_pos = input.find(open);
    if (begin_pos == string_view::npos) {
        return nullopt;
    }
    const auto content_start = begin_pos + open.size();
    const auto end_pos = input.find(close, content_start);
    if (end_pos == string_view::npos) {
        return nullopt;
    }
    return string{trim(input.substr(content_start, end_pos - content_start))};
}

// --- File I/O ---

inline string read_file(const fs::path& path) {
    require(!path.empty(), "read_file: empty path");

    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    require(!ec && !std::cmp_equal(size, -1), "Cannot determine file size: {}", path);

    std::ifstream file(path, std::ios::in | std::ios::binary);
    require(file.good(), "Failed to open file: {}", path);

    string content(static_cast<size_t>(size), '\0');
    file.read(content.data(), static_cast<std::streamsize>(size));
    require(file.good(), "Failed to read file (got {}/{}): {}", [&] { return file.gcount(); }, size, path);
    return content;
}

inline void write_file(const fs::path& path, string_view content) {
    require(!path.empty(), "write_file: empty path");

    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    require(file.good(), "Failed to open file for writing: {}", path);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    require(file.good(), "Failed to write file: {}", path);
}

} // namespace klspw
