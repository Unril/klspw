#pragma once

/// Shared type aliases, namespace imports, string utilities, and glaze opts.

#include <algorithm>
#include <cstddef>
#include <cstdint> // IWYU pragma: keep
#include <filesystem> // IWYU pragma: keep
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <glaze/containers/ordered_map.hpp>
#include <glaze/glaze.hpp>

namespace klspw {

// --- Type imports ---

using std::format;
using std::nullopt;
using std::optional;
using std::runtime_error;
using std::set;
using std::size_t;
using std::string;
using std::string_view;
using std::variant;
using std::vector;

namespace r = std::ranges;
namespace v = std::views;
namespace fs = std::filesystem;

using strings = vector<string>;
using opt_string = optional<string>;
using string_set = std::unordered_set<string>;

/// Ordered map with O(1) lookup and deterministic iteration order.
/// Used for all string-keyed maps in model types (serialized to JSON objects).
template <typename V> using string_map = glz::ordered_map<string, V>;

/// Pipe adaptor: range | to_vector() materializes into a vector.
template <typename T = void> constexpr auto to_vector() {
    if constexpr (std::is_void_v<T>) {
        return r::to<vector>();
    } else {
        return r::to<vector<T>>();
    }
}

// --- Glaze opts ---

/// Write opts: write null optionals as null (kotlin-lsp requires them), pretty-print for diffable output.
struct ws_write_opts_t : glz::opts {
    bool skip_null_members = false;
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
    return std::forward<R>(parts) | v::join_with(sep) | r::to<string>();
}

inline string join(std::initializer_list<string_view> parts, string_view sep = " ") { return join<>(parts, sep); }

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

/// Dedup elements by key, keeping first occurrence. Pipe adaptor: range | unique_by(proj)
namespace detail {

/// A type T is hashable if std::hash<T>{}(val) produces a std::size_t.
template <typename T>
concept Hashable = requires(T val) {
    { std::hash<T>{}(val) } -> std::convertible_to<std::size_t>;
};

template <typename Proj> struct unique_by_adaptor : r::range_adaptor_closure<unique_by_adaptor<Proj>> {
    Proj proj;

    template <r::input_range R>
        requires Hashable<std::remove_cvref_t<std::invoke_result_t<Proj, const r::range_value_t<R>&>>>
    auto operator()(R&& range) const {
        using Val = r::range_value_t<R>;
        using Key = std::remove_cvref_t<std::invoke_result_t<Proj, const Val&>>;
        std::unordered_set<Key> seen;
        vector<Val> result;
        for (auto&& elem : std::forward<R>(range)) {
            if (seen.insert(std::invoke(proj, elem)).second) {
                result.push_back(std::forward<decltype(elem)>(elem));
            }
        }
        return result;
    }
};

} // namespace detail

/// range | unique_by(&Type::field) -- dedup keeping first occurrence per key.
template <typename Proj = std::identity>
    requires(!r::input_range<Proj>)
auto unique_by(Proj proj = {}) {
    return detail::unique_by_adaptor<Proj>{.proj = std::move(proj)};
}

/// Returns a filter that excludes elements present in the given set.
/// Usage: source_roots | not_in(resources_roots) | ...
/// The returned view captures excluded by reference -- caller must ensure the set outlives the view.
inline auto not_in(const string_set& excluded) {
    return v::filter([&excluded](const auto& val) { return !excluded.contains(val); });
}

/// Strip a known prefix from a path for compact display.
/// Searches for any of the `markers` in `path`. If found, returns the suffix starting at the
/// marker and the prefix before it. If no marker matches, returns the path unchanged with empty prefix.
template <r::input_range Markers>
    requires std::constructible_from<string_view, r::range_reference_t<Markers>>
std::pair<string_view, string_view> strip_prefixes(string_view path, const Markers& markers) {
    for (const auto& m : markers) {
        const string_view marker{m};
        if (const auto pos = path.find(marker); pos != string_view::npos) {
            return {path.substr(pos + marker.size()), path.substr(0, pos + marker.size())};
        }
    }
    return {path, {}};
}

/// A pair of open/close delimiters for extract_between.
struct Delimiters {
    string_view open;
    string_view close;
};

/// Extract the substring between two delimiters, trimmed.
/// Returns nullopt if either delimiter is missing.
opt_string extract_between(string_view input, Delimiters delimiters);

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

} // namespace klspw
