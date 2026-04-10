#pragma once

/// Source jar/directory discovery for library source attachment.
///
/// SourceResolver finds source artifacts (jars or directories) for a classes jar.
/// Constructed from a jar path, it precomputes derived paths and tries two strategies:
///   1. Package cache with parallel source/build directory tree
///   2. Sibling -sources.jar (standard Gradle/Maven cache)

#include <spdlog/spdlog.h>

#include "common.hpp"

namespace klspw {

/// Discovers source artifacts for a classes jar.
///
/// Constructed from a jar path. Tries package cache layout first
/// (parallel source/build directory tree), then sibling -sources.jar fallback.
class SourceResolver {
  public:
    /// Package cache layout constants.
    static constexpr string_view pkg_cache_marker = "/DEV.STD.PTHREAD/";
    static constexpr string_view pkg_cache_marker_dir = "DEV.STD.PTHREAD";
    static constexpr string_view pkg_cache_source_dir = "generic-flavor";

    explicit SourceResolver(string_view jar) : jar_path_{jar} {
        const auto marker_pos = jar.find(pkg_cache_marker);
        if (marker_pos != string_view::npos) {
            const auto base = fs::path{jar.substr(0, marker_pos)};
            pkg_source_root_ = base / pkg_cache_source_dir;
            pkg_marker_dir_ = base / pkg_cache_marker_dir;
        }
    }

    /// Find source artifact. Returns the path to a source jar or directory if found.
    opt_string find() const {
        if (auto src = find_pkg_cache_sources()) {
            spdlog::trace("  sources (pkg-cache): {} -> {}", jar_path_.string(), *src);
            return src;
        }
        if (auto src = find_sibling_source_jar()) {
            spdlog::trace("  sources (sibling): {} -> {}", jar_path_.string(), *src);
            return src;
        }
        return nullopt;
    }

  private:
    fs::path jar_path_;
    fs::path pkg_source_root_; // empty if jar is not in a package cache
    fs::path pkg_marker_dir_; // empty if jar is not in a package cache

    static constexpr auto to_string = [](const fs::path& p) { return p.string(); };

    bool is_pkg_cache_jar() const { return !pkg_source_root_.empty(); }

    /// Package cache source discovery. Checks locations in priority order:
    ///   1. *-sources directory   2. *-sources.jar   3. src/main/java
    ///   4. src/main (no java/)   5. src/src          6. generated-src
    opt_string find_pkg_cache_sources() const {
        if (!is_pkg_cache_jar()) {
            return nullopt;
        }
        if (fs::is_directory(pkg_source_root_)) {
            if (auto src = find_dir(pkg_source_root_, "-sources").transform(to_string)) {
                return src;
            }
            if (auto src = find_file(pkg_source_root_, "-sources.jar").transform(to_string)) {
                return src;
            }
            if (auto src = find_dir(pkg_source_root_, "/src/main/java").transform(to_string)) {
                return src;
            }
            if (const auto p = pkg_source_root_ / "src" / "main";
                fs::is_directory(p) && !fs::is_directory(p / "java")) {
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
        const auto source_jar = jar_path_.parent_path() / format("{}-sources.jar", jar_path_.stem().string());
        if (fs::is_regular_file(source_jar)) {
            return source_jar.string();
        }
        return nullopt;
    }
};

} // namespace klspw
