#include "sources.hpp"

#include <doctest/doctest.h>

#include "test_common.hpp"

using namespace klspw;

// Helper: create a file (and parent dirs) with minimal content.
static void touch(const path& p) {
  fs::create_directories(p.parent_path());
  write_file(p, "");
}

// Helper: create a directory (and parents).
static void mkdirs(const path& p) { fs::create_directories(p); }

/// Shorthand: construct a SourceResolver for the jar and find sources.
static opt_string resolve(string_view jar) { return SourceResolver{jar}.find(); }

// --- Sibling source jar (standard Gradle/Maven cache) ---

TEST_CASE("finds -sources.jar next to classes jar") {
  const TempDir tmp;
  touch(tmp.path / "guava-33.4.0-jre.jar");
  touch(tmp.path / "guava-33.4.0-jre-sources.jar");

  const auto jar = (tmp.path / "guava-33.4.0-jre.jar").string();
  const auto result = resolve(jar);

  REQUIRE(result.has_value());
  CHECK(result->ends_with("guava-33.4.0-jre-sources.jar"));
}

TEST_CASE("returns nullopt when no sources jar exists") {
  const TempDir tmp;
  touch(tmp.path / "guava-33.4.0-jre.jar");

  CHECK_FALSE(resolve((tmp.path / "guava-33.4.0-jre.jar").string()).has_value());
}

TEST_CASE("returns nullopt for nonexistent jar") { CHECK_FALSE(resolve("/nonexistent/path/foo.jar").has_value()); }

// --- Package cache layout ---

TEST_CASE("pkg cache: finds *-sources directory") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  mkdirs(base / "generic-flavor" / "src" / "third-party" / "auto-value-1.11.1-sources");

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("-sources"));
}

TEST_CASE("pkg cache: finds *-sources.jar file") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  touch(base / "generic-flavor" / "src" / "netty-4.1-sources.jar");

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("-sources.jar"));
}

TEST_CASE("pkg cache: finds src/main/java") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  mkdirs(base / "generic-flavor" / "src" / "submod" / "src" / "main" / "java");

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("/src/main/java"));
}

TEST_CASE("pkg cache: finds src/main when no java/ subdir") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  mkdirs(base / "generic-flavor" / "src" / "main");

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("/src/main"));
}

TEST_CASE("pkg cache: prefers src/main/java over src/main") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  mkdirs(base / "generic-flavor" / "src" / "main" / "java");

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("/src/main/java"));
}

TEST_CASE("pkg cache: finds src/src as fallback") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  mkdirs(base / "generic-flavor" / "src" / "src");

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("/src/src"));
}

TEST_CASE("pkg cache: finds generated-src fallback") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  mkdirs(base / "DEV.STD.PTHREAD" / "build" / "generated-src");

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("/build/generated-src"));
}

TEST_CASE("pkg cache: returns nullopt when no sources found") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  mkdirs(base / "generic-flavor");

  CHECK_FALSE(resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string()).has_value());
}

TEST_CASE("pkg cache: returns nullopt for non-pkg-cache jar") {
  CHECK_FALSE(resolve("/home/user/.gradle/caches/foo.jar").has_value());
}

// --- Priority ---

TEST_CASE("prefers pkg cache over sibling -sources.jar") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0-sources.jar");
  mkdirs(base / "generic-flavor" / "src" / "src");

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("/src/src"));
}

TEST_CASE("pkg cache: *-sources dir has highest priority") {
  const TempDir tmp;
  const auto base = tmp.path / "Pkg" / "Pkg-1.0";
  touch(base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar");
  mkdirs(base / "generic-flavor" / "src" / "lib-1.0-sources");
  mkdirs(base / "generic-flavor" / "src" / "src" / "main" / "java");
  mkdirs(base / "generic-flavor" / "src" / "src");

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

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

  const auto result = resolve((base / "DEV.STD.PTHREAD" / "build" / "lib" / "Pkg-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("/src/main/java"));
}
