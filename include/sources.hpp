// SPDX-FileCopyrightText: 2026 Nikolai Fedorov
//
// SPDX-License-Identifier: MIT

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
    gradle_ = detect_gradle_cache(jar);
    pkg_ = detect_pkg_cache(jar);
  }

  // --- Classification ---

  bool is_gradle_cache() const { return gradle_.has_value(); }

  bool is_pkg_cache() const { return pkg_.has_value(); }

  // --- Common accessors ---

  const path& jar() const { return jar_path_; }

  string stem() const { return jar_path_.stem().string(); }

  /// Maven coordinates for Gradle cache jars, with classifier when present.
  /// Format: "group:module:version" or "group:module:version:classifier".
  /// The classifier is extracted from the jar filename: {module}-{version}-{classifier}.jar.
  string library_name() const {
    if (gradle_) {
      auto coords = coordinates();
      if (auto cls = classifier()) {
        coords += ':';
        coords += *cls;
      }
      return coords;
    }
    return stem();
  }

  // --- Gradle cache accessors (meaningful only when is_gradle_cache()) ---

  string_view group() const { return gradle_ ? gradle_->group : ""sv; }

  string_view module() const { return gradle_ ? gradle_->module : ""sv; }

  string_view version() const { return gradle_ ? gradle_->version : ""sv; }

  string coordinates() const { return format("{}:{}:{}", group(), module(), version()); }

  /// Extract the Maven classifier from the jar filename, if present.
  /// For "{module}-{version}-{classifier}.jar", returns "{classifier}".
  /// For "{module}-{version}.jar" or unrelated filenames, returns "".
  opt_string classifier() const {
    if (!gradle_) {
      return nullopt;
    }
    const auto filename = jar_path_.stem().string();
    const auto prefix = format("{}-{}-", gradle_->module, gradle_->version);
    if (filename.starts_with(prefix) && filename.size() > prefix.size()) {
      return filename.substr(prefix.size());
    }
    return nullopt;
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
    const auto maven = MavenCoords::parse(coords);
    if (!maven) {
      return nullopt;
    }

    const auto version_dir = gradle_cache_root / maven->group / maven->module / maven->version;
    if (!fs::is_directory(version_dir)) {
      return nullopt;
    }

    return find_sources_jar_in_version_dir(version_dir, format("{}-{}-sources.jar", maven->module, maven->version));
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

  /// Search subdirectories of a Gradle cache version directory for *-sources.jar.
  /// Returns the preferred name if found, then falls back to a jar containing the hint,
  /// then any *-sources.jar. Optionally skips a specific subdirectory (the jar's own hash dir).
  static opt_string find_sources_jar_in_version_dir(const path& version_dir, string_view preferred_name,
                                                    string_view hint = {}, const path& skip_dir = {}) {
    opt_string best_hint;
    opt_string best_any;
    std::error_code ec;
    for (const auto& hash_entry : fs::directory_iterator(version_dir, ec)) {
      if (!hash_entry.is_directory(ec) || (!skip_dir.empty() && hash_entry.path() == skip_dir)) {
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
        if (!preferred_name.empty() && name == preferred_name) {
          return file.path().string();
        }
        if (!hint.empty() && name.contains(hint) && !best_hint) {
          best_hint = file.path().string();
        } else if (!best_any) {
          best_any = file.path().string();
        }
      }
    }
    return best_hint.or_else([&] { return best_any; });
  }

  path jar_path_;

  /// Gradle module cache detection result.
  struct GradleCacheInfo {
    string group;
    string module;
    string version;
  };

  /// Package cache detection result.
  struct PkgCacheInfo {
    path source_root;
    path marker_dir;
  };

  optional<GradleCacheInfo> gradle_;
  optional<PkgCacheInfo> pkg_;

  /// Gradle module cache regex. Constructed once (thread-safe static).
  static const std::regex& gradle_cache_regex() {
    static const std::regex re{R"(/files-\d+(?:\.\d+)*/([^/]+)/([^/]+)/([^/]+)/([0-9a-f]{40})/[^/]+$)",
                               std::regex::ECMAScript | std::regex::optimize};
    return re;
  }

  static optional<GradleCacheInfo> detect_gradle_cache(string_view jar) {
    sv_match match;
    if (std::regex_search(jar.begin(), jar.end(), match, gradle_cache_regex())) {
      return GradleCacheInfo{.group = match[1].str(), .module = match[2].str(), .version = match[3].str()};
    }
    return nullopt;
  }

  /// Package cache layout constants.
  static constexpr string_view pkg_cache_marker = "/DEV.STD.PTHREAD/";
  static constexpr string_view pkg_cache_source_dir = "generic-flavor";

  static optional<PkgCacheInfo> detect_pkg_cache(string_view jar) {
    const auto marker_pos = jar.find(pkg_cache_marker);
    if (marker_pos == string_view::npos) {
      return nullopt;
    }
    const auto base = path{jar.substr(0, marker_pos)};
    return PkgCacheInfo{
        .source_root = base / pkg_cache_source_dir,
        .marker_dir = base / trim(pkg_cache_marker, "/"),
    };
  }

  /// Package cache source discovery. Checks locations in priority order:
  ///   1. *-sources directory   2. *-sources.jar   3. src/main/java
  ///   4. src/main (no java/)   5. src/src          6. generated-src
  opt_string find_pkg_cache_sources() const {
    if (!pkg_) {
      return nullopt;
    }
    if (fs::is_directory(pkg_->source_root)) {
      if (auto src = find_dir(pkg_->source_root, "-sources").transform(to_string)) {
        return src;
      }
      if (auto src = find_file(pkg_->source_root, "-sources.jar").transform(to_string)) {
        return src;
      }
      if (auto src = find_dir(pkg_->source_root, "/src/main/java").transform(to_string)) {
        return src;
      }
      if (const auto p = pkg_->source_root / "src" / "main"; fs::is_directory(p) && !fs::is_directory(p / "java")) {
        return p.string();
      }
      if (const auto p = pkg_->source_root / "src" / "src"; fs::is_directory(p)) {
        return p.string();
      }
    }
    if (const auto p = pkg_->marker_dir / "build" / "generated-src"; fs::is_directory(p)) {
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
    const auto preferred = gradle_ ? format("{}-{}-sources.jar", gradle_->module, gradle_->version) : string{};
    const auto hint = gradle_ ? string{gradle_->module} : version_dir.parent_path().filename().string();
    return find_sources_jar_in_version_dir(version_dir, preferred, hint, hash_dir);
  }
};

}  // namespace klspw
