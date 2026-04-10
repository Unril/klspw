#pragma once

/// Shared type aliases, namespace imports, glaze opts, and preconditions.

#include <cstddef>
#include <cstdint> // IWYU pragma: keep
#include <filesystem> // IWYU pragma: keep
#include <format>
#include <functional>
#include <optional>
#include <ranges> // IWYU pragma: keep (namespace aliases r, v)
#include <set>
#include <span>
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
using std::string_literals::operator""s;
using std::string_view;
using std::string_view_literals::operator""sv;
using std::variant;
using std::vector;

namespace r = std::ranges;
namespace v = std::views;
namespace fs = std::filesystem;

using strings = vector<string>;
using opt_string = optional<string>;
using string_set = std::unordered_set<string>;
using string_views = std::span<const std::string_view>;

/// Ordered map with O(1) lookup and deterministic iteration order.
/// Used for all string-keyed maps in model types (serialized to JSON objects).
template <typename V> using string_map = glz::ordered_map<string, V>;

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
template <typename... Args>
inline void require(bool condition, std::format_string<detail::eval_t<Args>...> fmt, Args&&... args) {
    if (!condition) {
        throw runtime_error(format(fmt, detail::eval(std::forward<Args>(args))...));
    }
}

} // namespace klspw
