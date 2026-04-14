#pragma once

/// Matches library and classpath names against workspace module names.
/// Handles exact matches, versioned suffixes ("CoreLib-1.0" -> "CoreLib"),
/// and AGP jetified artifacts ("jetified-CoreLib-1.0" -> "CoreLib").

#include "common.hpp"
#include "files.hpp"

namespace klspw {

/// Extract the module component from a Maven coordinate string ("group:module:version" -> "module").
/// Returns empty string_view if the input is not a valid Maven coordinate.
inline string_view maven_module_component(string_view coords) {
  const auto colon1 = coords.find(':');
  if (colon1 == string_view::npos) {
    return {};
  }
  const auto colon2 = coords.find(':', colon1 + 1);
  if (colon2 == string_view::npos) {
    return {};
  }
  return coords.substr(colon1 + 1, colon2 - colon1 - 1);
}

/// Resolves library names to workspace module names using longest-match-first.
/// Constructed from a set of known module names; immutable after construction.
class ModuleMatcher {
  vector<string> sorted_names_;  ///< Module names sorted by descending length.

 public:
  /// Construct from a set of module names. Empty set produces a matcher that never matches.
  explicit ModuleMatcher(const string_set& module_names) : sorted_names_(module_names | r::to<vector<string>>()) {
    // Longest first so "CoreLib" matches before "Core".
    r::sort(sorted_names_, std::greater{}, &string::size);
  }

  /// Construct from an initializer list of string-like values (convenience for tests).
  ModuleMatcher(std::initializer_list<string_view> names) : sorted_names_(names | r::to<vector<string>>()) {
    r::sort(sorted_names_, std::greater{}, &string::size);
  }

  /// Check if a library name corresponds to a specific module name.
  /// Matches exact ("lib" == "lib"), prefix-with-dash ("core-jvm" starts with "core" + "-"),
  /// jetified Android artifacts ("jetified-public-release-api" matches "public"),
  /// or Maven coordinate module component ("com.example:CoreLib:1.0" matches "CoreLib").
  static bool matches_name(string_view lib_name, string_view module_name) {
    if (lib_name == module_name) {
      return true;
    }
    if (lib_name.size() > module_name.size() && lib_name.starts_with(module_name) &&
        lib_name[module_name.size()] == '-') {
      return true;
    }
    // AGP jetification: "jetified-{module}-..." -> strip prefix and re-check.
    constexpr auto jetified = "jetified-"sv;
    if (lib_name.starts_with(jetified)) {
      return matches_name(lib_name.substr(jetified.size()), module_name);
    }
    // Maven coordinates: "group:module:version" -> extract module component and re-check.
    if (const auto maven_mod = maven_module_component(lib_name); !maven_mod.empty()) {
      return matches_name(maven_mod, module_name);
    }
    return false;
  }

  /// Find the module name that matches a library name, or nullopt.
  /// Uses longest-match-first to prefer "CoreLib" over "Core".
  opt_string find_module(string_view lib_name) const {
    const auto it = r::find_if(sorted_names_, [&](const auto& name) { return matches_name(lib_name, name); });
    return it != sorted_names_.end() ? opt_string{*it} : nullopt;
  }

  /// Find the module to promote a library dep to, excluding self-references.
  /// Returns the target module name if the library should be promoted, nullopt otherwise.
  opt_string promote_target(string_view lib_name, string_view owning_module) const {  // NOLINT(*-swappable-parameters)
    auto target = find_module(lib_name);
    if (target && *target == owning_module) {
      return nullopt;
    }
    return target;
  }

  /// Check if a classpath entry's file stem matches any known module name.
  bool classpath_matches(string_view classpath_entry) const {
    return find_module(file_stem(classpath_entry)).has_value();
  }

  bool empty() const { return sorted_names_.empty(); }
};

}  // namespace klspw
