#pragma once

/// String utilities: trim, join, strip_prefixes, extract_between.

#include "common.hpp"

namespace klspw {

/// Trim leading/trailing characters from a string_view.
/// Default chars: space, \n, \r. Tabs intentionally excluded -- Gradle output uses spaces/newlines.
inline string_view trim(string_view sv, string_view chars = " \n\r") {
    const auto start = sv.find_first_not_of(chars);
    if (start == string_view::npos) {
        return {};
    }
    return sv.substr(start, sv.find_last_not_of(chars) - start + 1);
}

/// Join any range of string-like elements with a separator (C++23).
template <r::input_range R>
    requires std::convertible_to<r::range_value_t<R>, string_view>
inline string join(R&& parts, string_view sep = " ") {
    return std::forward<R>(parts) | v::join_with(sep) | r::to<string>();
}

inline string join(std::initializer_list<string_view> parts, string_view sep = " ") { return join<>(parts, sep); }

/// Split a string into words on spaces, skipping empty segments.
inline strings split_words(string_view sv) {
    const auto non_empty = [](auto&& word) { return !r::empty(word); };
    const auto to_string = [](auto&& word) { return string(word.begin(), word.end()); };
    return trim(sv) | v::split(' ') | v::filter(non_empty) | v::transform(to_string) | r::to<strings>();
}

/// Strip a known prefix from a path for compact display.
/// Searches for any of the `markers` in `path`. If found, returns the suffix after the
/// marker and the prefix up to and including the marker. If no marker matches, returns
/// the path unchanged with empty prefix.
inline std::pair<string_view, string_view> strip_prefixes(string_view path, string_views markers) {
    for (const auto marker : markers) {
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

    void validate() const {
        require(!open.empty(), "extract_between: open delimiter must not be empty");
        require(!close.empty(), "extract_between: close delimiter must not be empty");
    }
};

/// Extract the substring between two delimiters, trimmed.
/// Returns nullopt if either delimiter is missing.
opt_string extract_between(string_view input, Delimiters delimiters);

} // namespace klspw
