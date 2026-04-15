// SPDX-FileCopyrightText: 2026 Nikolai Fedorov
//
// SPDX-License-Identifier: MIT

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

/// Escape a string for use as a JSON string value. Escapes \ and " characters.
string escape_json(string_view sv);

/// Parsed Maven coordinates: "group:module:version".
struct MavenCoords {
  string group;
  string module;
  string version;

  /// Parse "group:module:version" into components. Returns nullopt if not a valid coordinate.
  static optional<MavenCoords> parse(string_view coords) {
    const auto sep1 = coords.find(':');
    if (sep1 == string_view::npos) {
      return nullopt;
    }
    const auto sep2 = coords.find(':', sep1 + 1);
    if (sep2 == string_view::npos) {
      return nullopt;
    }
    return MavenCoords{
        .group = string{coords.substr(0, sep1)},
        .module = string{coords.substr(sep1 + 1, sep2 - sep1 - 1)},
        .version = string{coords.substr(sep2 + 1)},
    };
  }
};

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

/// Pipe adaptor: range | join_to_string(", ") materializes into a joined string.
/// Works with any range whose elements are convertible to string_view.
namespace detail {

struct join_to_string_adaptor : r::range_adaptor_closure<join_to_string_adaptor> {
  string_view sep;

  template <r::input_range R>
    requires std::convertible_to<r::range_value_t<R>, string_view>
  string operator()(R&& range) const {
    return std::forward<R>(range) | v::join_with(sep) | r::to<string>();
  }
};

}  // namespace detail

inline auto join_to_string(string_view sep = " ") { return detail::join_to_string_adaptor{.sep = sep}; }

}  // namespace klspw
