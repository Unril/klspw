#pragma once

/// Shared type aliases, namespace imports, and string utilities.

#include <cstdint>    // IWYU pragma: keep
#include <filesystem> // IWYU pragma: keep
#include <format>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

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
using std::ranges::count_if;

namespace fs = std::filesystem;

using json = nlohmann::json;
using strings = vector<string>;
using opt_string = optional<string>;

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
template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_value_t<R>, string_view>
inline string join(R&& parts, string_view sep = " ") {
    auto view = std::forward<R>(parts) | std::views::join_with(sep);
    return {std::from_range, view};
}

inline string join(std::initializer_list<string_view> parts, string_view sep = " ") {
    return join<>(parts, sep);
}

}
