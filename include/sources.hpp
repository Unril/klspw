#pragma once

/// Jar path analysis and source artifact discovery.
///
/// JarPath classifies a jar by its origin (Gradle module cache, package cache, or other)
/// and provides derived information: Maven coordinates, library names, source jar locations.
/// Replaces the former SourceResolver with a unified value-object + source discovery API.

#include <regex>

#include <spdlog/spdlog.h>

#include "common.hpp"
#include "describe.hpp"
#include "files.hpp"
#include "strings.hpp"

namespace klspw {

/// Classifies a jar path and provides derived information.
///
/// On construction, detects whether the jar lives in a Gradle module cache
/// or a package cache, and extracts structured fields accordingly.
/// Source discovery (find_sources) tries strategies in priority order:
///   1. Package cache parallel source/build tree
///   2. Sibling -sources.jar in the same directory
///   3. Gradle cache sibling hash directory
class JarPath {
 public:
  explicit JarPath(string_view jar) : jar_path_{jar} {
    detect_gradle_cache(jar);
    detect_pkg_cache(jar);
  }

  // --- Classification ---

  bool is_gradle_cache() const { return !group_.empty(); }

  bool is_pkg_cache() const { return !pkg_source_root_.empty(); }

  // --- Common accessors ---

  const path& jar() const { return jar_path_; }

  string stem() const { return jar_path_.stem().string(); }

  /// Maven coordinates for Gradle cache jars, with classifier when present.
  /// Format: "group:module:version" or "group:module:version:classifier".
  /// The classifier is extracted from the jar filename: {module}-{version}-{classifier}.jar.
  string library_name() const {
    if (is_gradle_cache()) {
      auto coords = coordinates();
      auto cls = classifier();
      if (!cls.empty()) {
        coords += ':';
        coords += cls;
      }
      return coords;
    }
    return stem();
  }

  // --- Gradle cache accessors (meaningful only when is_gradle_cache()) ---

  string_view group() const { return group_; }

  string_view module() const { return module_; }

  string_view version() const { return version_; }

  string coordinates() const { return format("{}:{}:{}", group_, module_, version_); }

  /// Extract the Maven classifier from the jar filename, if present.
  /// For "{module}-{version}-{classifier}.jar", returns "{classifier}".
  /// For "{module}-{version}.jar" or unrelated filenames, returns "".
  string classifier() const {
    if (!is_gradle_cache()) {
      return {};
    }
    const auto filename = jar_path_.stem().string();  // without .jar
    const auto prefix = format("{}-{}-", module_, version_);
    if (filename.starts_with(prefix) && filename.size() > prefix.size()) {
      return filename.substr(prefix.size());
    }
    return {};
  }

  // --- Source discovery ---

  /// Find source artifact. Returns the path to a source jar or directory if found.
  /// Tries strategies in priority order: pkg cache, sibling jar, Gradle cache sibling.
  opt_string find_sources() const {
    if (auto src = find_pkg_cache_sources()) {
      d_trace("  sources (pkg-cache): {} -> {}", jar_path_.string(), *src);
      return src;
    }
    if (auto src = find_sibling_source_jar()) {
      d_trace("  sources (sibling): {} -> {}", jar_path_.string(), *src);
      return src;
    }
    if (auto src = find_gradle_cache_sources()) {
      d_trace("  sources (gradle-cache): {} -> {}", jar_path_.string(), *src);
      return src;
    }
    return nullopt;
  }

  /// Find a sources jar in the Gradle module cache by Maven coordinates.
  /// Searches: {gradle_cache_root}/{group}/{module}/{version}/{hash}/*-sources.jar
  /// The gradle_cache_root is the "files-X.Y" directory (e.g., ".gradle/caches/modules-2/files-2.1").
  static opt_string find_sources_by_coordinates(const path& gradle_cache_root, string_view coords) {
    // Parse "group:module:version"
    const auto sep1 = coords.find(':');
    if (sep1 == string_view::npos) {
      return nullopt;
    }
    const auto sep2 = coords.find(':', sep1 + 1);
    if (sep2 == string_view::npos) {
      return nullopt;
    }
    const auto group = coords.substr(0, sep1);
    const auto module = coords.substr(sep1 + 1, sep2 - sep1 - 1);
    const auto version = coords.substr(sep2 + 1);

    const auto version_dir = gradle_cache_root / string{group} / string{module} / string{version};
    if (!fs::is_directory(version_dir)) {
      return nullopt;
    }

    // Prefer {module}-{version}-sources.jar over other *-sources.jar variants.
    const auto preferred_name = format("{}-{}-sources.jar", module, version);
    opt_string best;
    std::error_code ec;
    for (const auto& hash_entry : fs::directory_iterator(version_dir, ec)) {
      if (!hash_entry.is_directory(ec)) {
        continue;
      }
      for (const auto& file : fs::directory_iterator(hash_entry.path(), ec)) {
        if (!file.is_regular_file(ec)) {
          continue;
        }
        const auto name = file.path().filename().string();
        if (!name.ends_with("-sources.jar")) {
          continue;
        }
        if (name == preferred_name) {
          return file.path().string();
        }
        if (!best) {
          best = file.path().string();
        }
      }
    }
    return best;
  }

  /// Extract the Gradle module cache root directory from a Gradle cache jar path.
  /// Returns the "files-X.Y" directory, or nullopt if the path is not a Gradle cache path.
  static opt_path gradle_cache_root(string_view jar) {
    // Match the regex to find where "files-N.N" starts, then return that directory.
    sv_match match;
    if (!std::regex_search(jar.begin(), jar.end(), match, gradle_cache_regex())) {
      return nullopt;
    }
    // match[0] starts at "/files-N.N/..." — the prefix before match[0] + "/files-N.N" is the root.
    const auto match_start = static_cast<size_t>(match[0].first - jar.begin());
    // Find the next '/' after "/files-" to get the end of the files-N.N segment.
    const auto files_end = jar.find('/', match_start + 1);
    if (files_end == string_view::npos) {
      return nullopt;
    }
    return path{jar.substr(0, files_end)};
  }

 private:
  using sv_match = std::match_results<string_view::const_iterator>;

  path jar_path_;

  // Gradle cache fields (empty when not a Gradle cache jar).
  string group_;
  string module_;
  string version_;

  // Package cache fields (empty when not a package cache jar).
  path pkg_source_root_;
  path pkg_marker_dir_;

  /// Gradle module cache regex. Constructed once (thread-safe static).
  /// Pattern: /files-{N.N}/{group}/{module}/{version}/{40-hex-hash}/{artifact}
  /// The cache directory version (e.g., "2.1") may change across Gradle releases,
  /// so we match any dotted number sequence after "files-".
  /// The 40-char hex hash is the strongest structural validator — it distinguishes
  /// real Gradle cache paths from accidental substring matches.
  static const std::regex& gradle_cache_regex() {
    static const std::regex re{R"(/files-\d+(?:\.\d+)*/([^/]+)/([^/]+)/([^/]+)/([0-9a-f]{40})/[^/]+$)",
                               std::regex::ECMAScript | std::regex::optimize};
    return re;
  }

  void detect_gradle_cache(string_view jar) {
    sv_match match;
    if (std::regex_search(jar.begin(), jar.end(), match, gradle_cache_regex())) {
      group_ = match[1].str();
      module_ = match[2].str();
      version_ = match[3].str();
    }
  }

  /// Package cache layout constants.
  static constexpr string_view pkg_cache_marker = "/DEV.STD.PTHREAD/";
  static constexpr string_view pkg_cache_source_dir = "generic-flavor";

  void detect_pkg_cache(string_view jar) {
    const auto marker_pos = jar.find(pkg_cache_marker);
    if (marker_pos != string_view::npos) {
      const auto base = path{jar.substr(0, marker_pos)};
      pkg_source_root_ = base / pkg_cache_source_dir;
      pkg_marker_dir_ = base / trim(pkg_cache_marker, "/");
    }
  }

  static constexpr auto path_to_string = [](const path& p) { return p.string(); };

  /// Package cache source discovery. Checks locations in priority order:
  ///   1. *-sources directory   2. *-sources.jar   3. src/main/java
  ///   4. src/main (no java/)   5. src/src          6. generated-src
  opt_string find_pkg_cache_sources() const {
    if (!is_pkg_cache()) {
      return nullopt;
    }
    if (fs::is_directory(pkg_source_root_)) {
      if (auto src = find_dir(pkg_source_root_, "-sources").transform(path_to_string)) {
        return src;
      }
      if (auto src = find_file(pkg_source_root_, "-sources.jar").transform(path_to_string)) {
        return src;
      }
      if (auto src = find_dir(pkg_source_root_, "/src/main/java").transform(path_to_string)) {
        return src;
      }
      if (const auto p = pkg_source_root_ / "src" / "main"; fs::is_directory(p) && !fs::is_directory(p / "java")) {
        return p.string();
      }
      if (const auto p = pkg_source_root_ / "src" / "src"; fs::is_directory(p)) {
        return p.string();
      }
    }
    if (const auto p = pkg_marker_dir_ / "build" / "generated-src"; fs::is_directory(p)) {
      return p.string();
    }
    return nullopt;
  }

  /// Sibling -sources.jar lookup: <stem>-sources.jar in the same directory.
  opt_string find_sibling_source_jar() const {
    const auto source_jar = jar_path_.parent_path() / format("{}-sources.jar", stem());
    if (fs::is_regular_file(source_jar)) {
      return source_jar.string();
    }
    return nullopt;
  }

  /// Gradle module cache source discovery.
  /// Searches sibling hash directories within the version directory for *-sources.jar.
  /// Prefers sources jars whose base name contains the module name.
  opt_string find_gradle_cache_sources() const {
    const auto hash_dir = jar_path_.parent_path();
    const auto version_dir = hash_dir.parent_path();
    if (version_dir.empty() || !fs::is_directory(version_dir)) {
      return nullopt;
    }
    const auto mod_name = is_gradle_cache() ? string{module_} : version_dir.parent_path().filename().string();
    const auto preferred = is_gradle_cache() ? format("{}-{}-sources.jar", module_, version_) : string{};
    opt_string best_preferred;  // exact {module}-{version}-sources.jar
    opt_string best_module;  // contains module name
    opt_string best_any;  // any -sources.jar
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(version_dir, ec)) {
      if (!entry.is_directory(ec) || entry.path() == hash_dir) {
        continue;
      }
      for (const auto& file : fs::directory_iterator(entry.path(), ec)) {
        if (!file.is_regular_file(ec)) {
          continue;
        }
        const auto name = file.path().filename().string();
        if (!name.ends_with("-sources.jar")) {
          continue;
        }
        if (!preferred.empty() && name == preferred) {
          best_preferred = file.path().string();
        } else if (!mod_name.empty() && name.contains(mod_name) && !best_module) {
          best_module = file.path().string();
        } else if (!best_any) {
          best_any = file.path().string();
        }
      }
    }
    // Priority: exact preferred > module name match > any sources jar.
    return best_preferred.or_else([&] { return best_module; }).or_else([&] { return best_any; });
  }
};

}  // namespace klspw
