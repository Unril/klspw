#pragma once

/// Source jar/directory discovery for library source attachment.
///
/// Given a classes jar path, attempts to find the corresponding source artifact.
/// Supports two layouts:
///   1. Standard Gradle/Maven cache: <stem>-sources.jar in the same directory
///   2. Package cache with parallel source tree: jars live under a build variant
///      directory (identified by a configurable marker), sources live under a
///      sibling directory (e.g., generic-flavor/)

#include "common.hpp"

#include <spdlog/spdlog.h>

namespace klspw {

/// Marker that identifies the build variant directory in a package cache jar path.
/// Everything before this marker is the package version root; the sibling
/// directory "generic-flavor/" contains source artifacts.
inline constexpr string_view pkg_cache_marker = "/DEV.STD.PTHREAD/";

/// Sibling directory name under the package version root that contains sources.
inline constexpr string_view pkg_cache_source_dir = "generic-flavor";

/// Find the first entry under `root` whose path ends with `suffix`.
/// Searches recursively. Matches against the full path string, so multi-component
/// suffixes like "/src/main/java" work. Returns nullopt on no match or I/O errors.
inline opt_string find_recursive(const fs::path& root, string_view suffix, bool want_dir) {
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (want_dir ? entry.is_directory(ec) : entry.is_regular_file(ec)) {
            if (entry.path().string().ends_with(suffix)) {
                return entry.path().string();
            }
        }
    }
    return nullopt;
}

/// Find sources in a package cache with parallel source/build directory layout.
///
/// Some package managers store compiled jars and source artifacts in parallel
/// directory trees under a common version root. The jar path contains a build
/// variant marker; sources live under a sibling directory. Source locations
/// checked in order of preference:
///   1. {source_dir}/.../*-sources       (unpacked sources directory)
///   2. {source_dir}/.../*-sources.jar   (sources jar file)
///   3. {source_dir}/.../src/main/java   (Maven project structure -- most specific)
///   4. {source_dir}/src/main            (package dirs in main/, no java/ subdir)
///   5. {source_dir}/src/src             (flat source tree -- broad fallback)
///   6. {marker_dir}/build/generated-src (generated code)
inline opt_string find_pkg_cache_sources(string_view jar) {
    const auto marker_pos = jar.find(pkg_cache_marker);
    if (marker_pos == string_view::npos) {
        return nullopt;
    }

    const fs::path base{jar.substr(0, marker_pos)};
    const auto source_root = base / pkg_cache_source_dir;

    if (fs::is_directory(source_root)) {
        // 1. *-sources directory (most common for third-party repackaged artifacts)
        if (auto src = find_recursive(source_root, "-sources", true)) {
            return src;
        }
        // 2. *-sources.jar
        if (auto src = find_recursive(source_root, "-sources.jar", false)) {
            return src;
        }
        // 3. Maven project structure: src/main/java (most specific source dir)
        if (auto src = find_recursive(source_root, "/src/main/java", true)) {
            return src;
        }
        // 4. src/main (when no java/ subdir exists -- package dirs directly in main/)
        if (const auto p = source_root / "src" / "main";
            fs::is_directory(p) && !fs::is_directory(p / "java")) {
            return p.string();
        }
        // 5. Flat source tree: src/src (broad fallback)
        if (const auto p = source_root / "src" / "src"; fs::is_directory(p)) {
            return p.string();
        }
    }

    // 6. Fallback: generated-src for generated code packages
    const auto marker_dir = string{pkg_cache_marker.substr(1, pkg_cache_marker.size() - 2)};
    if (const auto p = base / marker_dir / "build" / "generated-src"; fs::is_directory(p)) {
        return p.string();
    }

    return nullopt;
}

/// Find a source jar for a standard Gradle/Maven cache jar.
/// Looks for "<stem>-sources.jar" in the same directory.
inline opt_string find_sibling_source_jar(string_view jar) {
    const fs::path jar_path{jar};
    const auto source_jar = jar_path.parent_path() / format("{}-sources.jar", jar_path.stem().string());
    if (fs::is_regular_file(source_jar)) {
        return source_jar.string();
    }
    return nullopt;
}

/// Find source artifact for a classes jar.
/// Tries package cache layout first, then standard sibling jar.
/// Returns the path to a source jar or directory if found.
inline opt_string find_sources(string_view jar) {
    if (auto src = find_pkg_cache_sources(jar)) {
        spdlog::debug("  sources (pkg-cache): {} -> {}", jar, *src);
        return src;
    }
    if (auto src = find_sibling_source_jar(jar)) {
        spdlog::debug("  sources (sibling): {} -> {}", jar, *src);
        return src;
    }
    return nullopt;
}

} // namespace klspw
