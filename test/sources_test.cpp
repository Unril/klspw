#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "sources.hpp"
#include "test_common.hpp"

using namespace klspw;

// Helper: create a file (and parent dirs) with minimal content.
static void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    write_file(p, "");
}

// Helper: create a directory (and parents).
static void mkdirs(const fs::path& p) {
    fs::create_directories(p);
}

// --- find_sibling_source_jar ---

TEST_CASE("find_sibling_source_jar finds -sources.jar next to classes jar") {
    const TempDir tmp;
    touch(tmp.path / "guava-33.4.0-jre.jar");
    touch(tmp.path / "guava-33.4.0-jre-sources.jar");

    const auto jar = (tmp.path / "guava-33.4.0-jre.jar").string();
    const auto result = find_sibling_source_jar(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("guava-33.4.0-jre-sources.jar"));
}

TEST_CASE("find_sibling_source_jar returns nullopt when no sources jar exists") {
    const TempDir tmp;
    touch(tmp.path / "guava-33.4.0-jre.jar");

    const auto jar = (tmp.path / "guava-33.4.0-jre.jar").string();
    CHECK_FALSE(find_sibling_source_jar(jar).has_value());
}

TEST_CASE("find_sibling_source_jar returns nullopt for nonexistent jar") {
    CHECK_FALSE(find_sibling_source_jar("/nonexistent/path/foo.jar").has_value());
}

// --- find_pkg_cache_sources ---

TEST_CASE("pkg cache: finds *-sources directory") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    mkdirs(base / "generic-flavor" / "src" / "third-party" / "auto-value-1.11.1-sources");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_pkg_cache_sources(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("-sources"));
}

TEST_CASE("pkg cache: finds *-sources.jar file") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    touch(base / "generic-flavor" / "src" / "netty-4.1-sources.jar");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_pkg_cache_sources(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("-sources.jar"));
}

TEST_CASE("pkg cache: finds src/main/java") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    mkdirs(base / "generic-flavor" / "src" / "submod" / "src" / "main" / "java");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_pkg_cache_sources(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("/src/main/java"));
}

TEST_CASE("pkg cache: finds src/main when no java/ subdir") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    mkdirs(base / "generic-flavor" / "src" / "main");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_pkg_cache_sources(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("/src/main"));
}

TEST_CASE("pkg cache: skips src/main when java/ subdir exists (prefers src/main/java)") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    mkdirs(base / "generic-flavor" / "src" / "main" / "java");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_pkg_cache_sources(jar);

    REQUIRE(result.has_value());
    // Should find src/main/java via find_recursive, not src/main
    CHECK(result->ends_with("/src/main/java"));
}

TEST_CASE("pkg cache: finds src/src as fallback") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    mkdirs(base / "generic-flavor" / "src" / "src");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_pkg_cache_sources(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("/src/src"));
}

TEST_CASE("pkg cache: finds generated-src fallback") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    mkdirs(base / "DEV.STD.PTHREAD" / "build" / "generated-src");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_pkg_cache_sources(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("/build/generated-src"));
}

TEST_CASE("pkg cache: returns nullopt when no sources found") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    mkdirs(base / "generic-flavor");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    CHECK_FALSE(find_pkg_cache_sources(jar).has_value());
}

TEST_CASE("pkg cache: returns nullopt for non-pkg-cache jar") {
    CHECK_FALSE(find_pkg_cache_sources("/home/user/.gradle/caches/foo.jar").has_value());
}

// --- find_sources (combined) ---

TEST_CASE("find_sources prefers pkg cache over sibling") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0-sources.jar");
    mkdirs(base / "generic-flavor" / "src" / "src");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_sources(jar);

    REQUIRE(result.has_value());
    // pkg cache (src/src) should win over sibling -sources.jar
    CHECK(result->ends_with("/src/src"));
}

TEST_CASE("find_sources falls back to sibling for non-pkg-cache jar") {
    const TempDir tmp;
    touch(tmp.path / "kotlin-stdlib-2.0.jar");
    touch(tmp.path / "kotlin-stdlib-2.0-sources.jar");

    const auto jar = (tmp.path / "kotlin-stdlib-2.0.jar").string();
    const auto result = find_sources(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("-sources.jar"));
}

TEST_CASE("find_sources returns nullopt when nothing found") {
    const TempDir tmp;
    touch(tmp.path / "mystery.jar");

    const auto jar = (tmp.path / "mystery.jar").string();
    CHECK_FALSE(find_sources(jar).has_value());
}

// --- Priority: *-sources dir beats src/main/java beats src/src ---

TEST_CASE("pkg cache: *-sources dir has highest priority") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    // All source types present
    mkdirs(base / "generic-flavor" / "src" / "lib-1.0-sources");
    mkdirs(base / "generic-flavor" / "src" / "src" / "main" / "java");
    mkdirs(base / "generic-flavor" / "src" / "src");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_pkg_cache_sources(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("-sources"));
    CHECK_FALSE(result->ends_with("-sources.jar"));
}

TEST_CASE("pkg cache: src/main/java beats src/src") {
    const TempDir tmp;
    const auto base = tmp.path / "Pkg" / "Pkg-1.0";
    touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
    mkdirs(base / "generic-flavor" / "src" / "src" / "main" / "java");
    mkdirs(base / "generic-flavor" / "src" / "src");

    const auto jar = (base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string();
    const auto result = find_pkg_cache_sources(jar);

    REQUIRE(result.has_value());
    CHECK(result->ends_with("/src/main/java"));
}
