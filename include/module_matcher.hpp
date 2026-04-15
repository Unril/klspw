// SPDX-FileCopyrightText: 2026 Nikolai Fedorov
//
// SPDX-License-Identifier: MIT

#pragma once

/// Matches library and classpath names against workspace module names.
/// Handles exact matches, versioned suffixes ("CoreLib-1.0" -> "CoreLib"),
/// and AGP jetified artifacts ("jetified-CoreLib-1.0" -> "CoreLib").

#include "common.hpp"
#include "files.hpp"
#include "ranges.hpp"
#include "strings.hpp"

namespace klspw {

/// Extract the module component from a Maven coordinate string ("group:module:version" -> "module").
/// Returns nullopt if the input is not a valid Maven coordinate.
inline opt_string maven_module_component(string_view coords) {
  return MavenCoords::parse(coords).transform([](const MavenCoords& m) { return m.module; });
}

/// Resolves library names to workspace module names using longest-match-first.
/// Constructed from a set of known module names; immutable after construction.
class ModuleMatcher {
  vector<string> sorted_names_;  ///< Module names sorted by descending length.

 public:
  /// Longest first so "CoreLib" matches before "Core".
  explicit ModuleMatcher(vector<string> names) : sorted_names_{std::move(names)} {
    r::sort(sorted_names_, std::greater{}, &string::size);
  }

  /// Construct from a set of module names. Empty set produces a matcher that never matches.
  explicit ModuleMatcher(const string_set& module_names) : ModuleMatcher(module_names | r::to<vector<string>>()) {}

  /// Construct from an initializer list of string-like values (convenience for tests).
  ModuleMatcher(std::initializer_list<string_view> names) : ModuleMatcher(names | r::to<vector<string>>()) {}

  /// Check if a library name corresponds to a specific module name.
  /// Matches exact ("lib" == "lib"), prefix-with-dash ("core-jvm" starts with "core" + "-"),
  /// jetified Android artifacts ("jetified-public-release-api" matches "public"),
  /// or Maven coordinate module component ("com.example:CoreLib:1.0" matches "CoreLib").
  static bool matches_name(string_view lib_name, string_view module_name) {
    if (lib_name == module_name) {
      return true;
    }
    const auto prefix = string{module_name} + "-";
    if (lib_name.starts_with(prefix)) {
      return true;
    }
    // AGP jetification: "jetified-{module}-..." -> strip prefix and re-check.
    constexpr auto jetified = "jetified-"sv;
    if (lib_name.starts_with(jetified)) {
      return matches_name(lib_name.substr(jetified.size()), module_name);
    }
    // Maven coordinates: "group:module:version" -> extract module component and re-check.
    if (const auto maven_mod = maven_module_component(lib_name)) {
      return matches_name(*maven_mod, module_name);
    }
    return false;
  }

  /// Find the module name that matches a library name, or nullopt.
  /// Uses longest-match-first to prefer "CoreLib" over "Core".
  opt_string find_module(string_view lib_name) const {
    return find_opt(sorted_names_, [&](const auto& name) { return matches_name(lib_name, name); });
  }

  /// Find the module to promote a library dep to, excluding self-references.
  /// Returns the target module name if the library should be promoted, nullopt otherwise.
  opt_string promote_target(string_view lib_name, string_view owning_module) const {  // NOLINT(*-swappable-parameters)
    return find_module(lib_name).and_then(
        [&](const string& target) -> opt_string { return target == owning_module ? nullopt : opt_string{target}; });
  }

  /// Check if a classpath entry's file stem matches any known module name.
  bool classpath_matches(string_view classpath_entry) const {
    return find_module(file_stem(classpath_entry)).has_value();
  }

  bool empty() const { return sorted_names_.empty(); }
};

}  // namespace klspw
