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

/// Shorthand: construct a JarPath for the jar and find sources.
static opt_string resolve(string_view jar) { return JarPath{jar}.find_sources(); }

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

// --- Gradle module cache layout ---
// Layout: {version_dir}/{hash1}/classes.jar, {version_dir}/{hash2}/module-version-sources.jar

TEST_CASE("gradle cache: finds -sources.jar in sibling hash directory") {
  const TempDir tmp;
  // Simulate: .gradle/caches/modules-2/files-2.1/com.example/mylib/1.0/{hash1}/mylib-1.0.jar
  //                                                                    /{hash2}/mylib-1.0-sources.jar
  const auto version_dir = tmp.path / "com.example" / "mylib" / "1.0";
  touch(version_dir / "abc123" / "mylib-1.0.jar");
  touch(version_dir / "def456" / "mylib-1.0-sources.jar");

  const auto result = resolve((version_dir / "abc123" / "mylib-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("mylib-1.0-sources.jar"));
}

TEST_CASE("gradle cache: prefers sources jar matching module name") {
  const TempDir tmp;
  // Simulate: presenter-public/0.0.7/{hash1}/public-metadata.jar (classes)
  //                                  /{hash2}/presenter-public-0.0.7-sources.jar (correct)
  //                                  /{hash3}/unrelated-0.0.7-sources.jar (wrong)
  const auto version_dir = tmp.path / "software.acme" / "presenter-public" / "0.0.7";
  touch(version_dir / "hash1" / "public-metadata.jar");
  touch(version_dir / "hash2" / "presenter-public-0.0.7-sources.jar");
  touch(version_dir / "hash3" / "unrelated-0.0.7-sources.jar");

  const auto result = resolve((version_dir / "hash1" / "public-metadata.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("presenter-public-0.0.7-sources.jar"));
}

TEST_CASE("gradle cache: falls back to any -sources.jar when module name doesn't match") {
  const TempDir tmp;
  const auto version_dir = tmp.path / "com.example" / "mylib" / "1.0";
  touch(version_dir / "hash1" / "classes.jar");
  touch(version_dir / "hash2" / "other-1.0-sources.jar");

  const auto result = resolve((version_dir / "hash1" / "classes.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("-sources.jar"));
}

TEST_CASE("gradle cache: returns nullopt when no -sources.jar in sibling dirs") {
  const TempDir tmp;
  const auto version_dir = tmp.path / "com.example" / "mylib" / "1.0";
  touch(version_dir / "hash1" / "mylib-1.0.jar");
  touch(version_dir / "hash2" / "mylib-1.0.pom");  // not a sources jar

  CHECK_FALSE(resolve((version_dir / "hash1" / "mylib-1.0.jar").string()).has_value());
}

TEST_CASE("gradle cache: ignores -sources.jar in same hash directory (already covered by sibling check)") {
  const TempDir tmp;
  const auto version_dir = tmp.path / "com.example" / "mylib" / "1.0";
  touch(version_dir / "hash1" / "mylib-1.0.jar");
  // Sources in the same hash dir — found by sibling check (find_sibling_source_jar), not gradle cache
  touch(version_dir / "hash1" / "mylib-1.0-sources.jar");

  const auto result = resolve((version_dir / "hash1" / "mylib-1.0.jar").string());

  REQUIRE(result.has_value());
  CHECK(result->ends_with("mylib-1.0-sources.jar"));
}

TEST_CASE("gradle cache: sibling -sources.jar takes priority over gradle cache") {
  const TempDir tmp;
  const auto version_dir = tmp.path / "com.example" / "mylib" / "1.0";
  // Sibling in same dir (found by find_sibling_source_jar)
  touch(version_dir / "hash1" / "mylib-1.0.jar");
  touch(version_dir / "hash1" / "mylib-1.0-sources.jar");
  // Also in sibling hash dir (found by find_gradle_cache_sources)
  touch(version_dir / "hash2" / "mylib-1.0-sources.jar");

  const auto result = resolve((version_dir / "hash1" / "mylib-1.0.jar").string());

  REQUIRE(result.has_value());
  // Should be the sibling (same dir), not the gradle cache one
  CHECK(result->contains("hash1"));
}

TEST_CASE("gradle cache: handles version dir with only one hash directory") {
  const TempDir tmp;
  const auto version_dir = tmp.path / "com.example" / "mylib" / "1.0";
  touch(version_dir / "hash1" / "mylib-1.0.jar");
  // No sibling hash dirs at all

  CHECK_FALSE(resolve((version_dir / "hash1" / "mylib-1.0.jar").string()).has_value());
}

TEST_CASE("gradle cache: handles jar at filesystem root gracefully") {
  // Jar directly in /tmp — version_dir would be /tmp which has no meaningful structure
  const TempDir tmp;
  touch(tmp.path / "random.jar");

  CHECK_FALSE(resolve((tmp.path / "random.jar").string()).has_value());
}

// --- JarPath classification ---

TEST_CASE("JarPath: detects Gradle module cache jar with 40-char hex hash") {
  const JarPath jp{
      "/Users/me/.gradle/caches/modules-2/files-2.1/software.acme.app.platform/presenter-public/0.0.7/"
      "9697f85a6227ae25ed3bc5444e2ba4793743a3ed/public-metadata.jar"};

  CHECK(jp.is_gradle_cache());
  CHECK_FALSE(jp.is_pkg_cache());
  CHECK(jp.group() == "software.acme.app.platform");
  CHECK(jp.module() == "presenter-public");
  CHECK(jp.version() == "0.0.7");
  CHECK(jp.coordinates() == "software.acme.app.platform:presenter-public:0.0.7");
}

TEST_CASE("JarPath: detects dotted group names") {
  const JarPath jp{
      "/home/user/.gradle/caches/modules-2/files-2.1/com.example.mobile.app.platform/last-mile-scopes-public/0.3.7/"
      "4ad82eb93a9f734a1cea975f5f05276b404979ce/public-metadata.jar"};

  CHECK(jp.is_gradle_cache());
  CHECK(jp.coordinates() == "com.example.mobile.app.platform:last-mile-scopes-public:0.3.7");
}

TEST_CASE("JarPath: rejects AGP transform paths (no 40-char hex hash)") {
  const JarPath jp{
      "/Users/me/.gradle/caches/9.1.0/transforms/068c0e8bfa6359dccab93bd9356d7bf9/"
      "transformed/jetified-public-release-api.jar"};

  CHECK_FALSE(jp.is_gradle_cache());
  CHECK_FALSE(jp.is_pkg_cache());
}

TEST_CASE("JarPath: rejects local build paths") {
  const JarPath jp{"/workspace/project/build/libs/tokens-metadata.jar"};

  CHECK_FALSE(jp.is_gradle_cache());
  CHECK_FALSE(jp.is_pkg_cache());
}

TEST_CASE("JarPath: rejects path with files-2.1 but short hash") {
  // Hash is only 8 chars, not 40 — should not match.
  const JarPath jp{"/Users/me/.gradle/caches/modules-2/files-2.1/org.example/lib/1.0/abc12345/lib-1.0.jar"};

  CHECK_FALSE(jp.is_gradle_cache());
}

TEST_CASE("JarPath: rejects path with files-2.1 as substring of longer component") {
  // "files-2.1.backup" is not the real marker.
  const JarPath jp{"/cache/files-2.1.backup/org.example/lib/1.0/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/lib.jar"};

  CHECK_FALSE(jp.is_gradle_cache());
}

TEST_CASE("JarPath: rejects empty path") {
  const JarPath jp{""};

  CHECK_FALSE(jp.is_gradle_cache());
  CHECK_FALSE(jp.is_pkg_cache());
}

TEST_CASE("JarPath: rejects truncated cache path") {
  const JarPath jp{"/Users/me/.gradle/caches/modules-2/files-2.1/group/module"};

  CHECK_FALSE(jp.is_gradle_cache());
}

TEST_CASE("JarPath: detects package cache jar") {
  const JarPath jp{"/pkg/Foo/Foo-1.0/DEV.STD.PTHREAD/build/lib/Foo-1.0.jar"};

  CHECK_FALSE(jp.is_gradle_cache());
  CHECK(jp.is_pkg_cache());
}

TEST_CASE("JarPath: matches future Gradle cache versions (files-3, files-2.2)") {
  const JarPath jp3{
      "/Users/me/.gradle/caches/modules-3/files-3/org.example/lib/1.0/"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/lib-1.0.jar"};
  CHECK(jp3.is_gradle_cache());
  CHECK(jp3.coordinates() == "org.example:lib:1.0");

  const JarPath jp22{
      "/Users/me/.gradle/caches/modules-2/files-2.2/org.example/lib/2.0/"
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/lib-2.0.jar"};
  CHECK(jp22.is_gradle_cache());
  CHECK(jp22.coordinates() == "org.example:lib:2.0");
}

// --- JarPath::library_name ---

TEST_CASE("JarPath: library_name uses Maven coordinates for Gradle cache jars") {
  const JarPath jp{
      "/Users/me/.gradle/caches/modules-2/files-2.1/org.jetbrains/annotations/24.0.0/"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/annotations-24.0.0.jar"};

  CHECK(jp.library_name() == "org.jetbrains:annotations:24.0.0");
}

TEST_CASE("JarPath: library_name falls back to file_stem for non-cache jars") {
  CHECK(JarPath{"transforms/abc123/transformed/jetified-public-release-api.jar"}.library_name() ==
        "jetified-public-release-api");
}

TEST_CASE("JarPath: library_name falls back to file_stem for local build jars") {
  CHECK(JarPath{"/workspace/project/build/libs/tokens-metadata.jar"}.library_name() == "tokens-metadata");
}

TEST_CASE("JarPath: library_name disambiguates KMP metadata jars with same artifact name") {
  const auto name1 =
      JarPath{
          "/Users/me/.gradle/caches/modules-2/files-2.1/software.acme.app.platform/presenter-public/0.0.7/"
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/public-metadata.jar"}
          .library_name();
  const auto name2 =
      JarPath{
          "/Users/me/.gradle/caches/modules-2/files-2.1/com.example.mobile.app.platform/last-mile-scopes-public/0.3.7/"
          "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/public-metadata.jar"}
          .library_name();

  CHECK(name1 != name2);
  CHECK(name1 == "software.acme.app.platform:presenter-public:0.0.7");
  CHECK(name2 == "com.example.mobile.app.platform:last-mile-scopes-public:0.3.7");
}

TEST_CASE("JarPath: stem returns file stem regardless of origin") {
  CHECK(JarPath{"/cache/files-2.1/g/m/v/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/foo-1.0.jar"}.stem() == "foo-1.0");
  CHECK(JarPath{"/workspace/build/libs/bar.jar"}.stem() == "bar");
}

// --- JarPath::classifier ---

TEST_CASE("JarPath: classifier extracts classifier from Gradle cache jar") {
  const JarPath jp{
      "/Users/me/.gradle/caches/modules-2/files-2.1/org.example/mylib/1.0/"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/mylib-1.0-jdk8.jar"};

  CHECK(jp.classifier() == "jdk8");
}

TEST_CASE("JarPath: classifier returns empty when no classifier present") {
  const JarPath jp{
      "/Users/me/.gradle/caches/modules-2/files-2.1/org.example/mylib/1.0/"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/mylib-1.0.jar"};

  CHECK_FALSE(jp.classifier().has_value());
}

TEST_CASE("JarPath: classifier returns nullopt for non-Gradle-cache jars") {
  CHECK_FALSE(JarPath{"/workspace/build/libs/foo-1.0-sources.jar"}.classifier().has_value());
}

TEST_CASE("JarPath: library_name includes classifier when present") {
  const JarPath jp{
      "/Users/me/.gradle/caches/modules-2/files-2.1/org.example/mylib/1.0/"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/mylib-1.0-jdk8.jar"};

  CHECK(jp.library_name() == "org.example:mylib:1.0:jdk8");
}

// --- find_sources_by_coordinates ---

TEST_CASE("find_sources_by_coordinates returns nullopt for coords missing first colon") {
  const TempDir tmp;
  CHECK_FALSE(JarPath::find_sources_by_coordinates(tmp.path, "no-colon-at-all").has_value());
}

TEST_CASE("find_sources_by_coordinates returns nullopt for coords missing second colon") {
  const TempDir tmp;
  CHECK_FALSE(JarPath::find_sources_by_coordinates(tmp.path, "group:module").has_value());
}

TEST_CASE("find_sources_by_coordinates returns nullopt when version dir does not exist") {
  const TempDir tmp;
  CHECK_FALSE(JarPath::find_sources_by_coordinates(tmp.path, "org.example:mylib:9.9.9").has_value());
}

TEST_CASE("find_sources_by_coordinates prefers module-version-sources.jar") {
  const TempDir tmp;
  const auto version_dir = tmp.path / "org.example" / "mylib" / "1.0";
  touch(version_dir / "hash1" / "mylib-1.0-sources.jar");
  touch(version_dir / "hash2" / "other-1.0-sources.jar");

  const auto result = JarPath::find_sources_by_coordinates(tmp.path, "org.example:mylib:1.0");

  REQUIRE(result.has_value());
  CHECK(result->ends_with("mylib-1.0-sources.jar"));
}

TEST_CASE("find_sources_by_coordinates falls back to any sources jar") {
  const TempDir tmp;
  const auto version_dir = tmp.path / "org.example" / "mylib" / "1.0";
  touch(version_dir / "hash1" / "alternate-1.0-sources.jar");

  const auto result = JarPath::find_sources_by_coordinates(tmp.path, "org.example:mylib:1.0");

  REQUIRE(result.has_value());
  CHECK(result->ends_with("-sources.jar"));
}

// --- gradle_cache_root ---

TEST_CASE("gradle_cache_root returns nullopt when path ends at files-N.N with no trailing slash") {
  // Path has /files-2.1 but nothing after it — files_end == npos
  CHECK_FALSE(JarPath::gradle_cache_root("/cache/files-2.1").has_value());
}

// --- find_gradle_cache_sources: best_preferred path ---

TEST_CASE("gradle cache: find_sources prefers exact module-version-sources.jar over module-name match") {
  const TempDir tmp;
  const auto files_dir = tmp.path / "files-2.1";
  const auto version_dir = files_dir / "org.example" / "mylib" / "1.0";
  // Classes jar in hash1
  touch(version_dir / "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" / "mylib-1.0.jar");
  // Exact preferred in hash2
  touch(version_dir / "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" / "mylib-1.0-sources.jar");
  // Module-name match but not exact in hash3
  touch(version_dir / "cccccccccccccccccccccccccccccccccccccccc" / "mylib-custom-sources.jar");

  const auto jar = (version_dir / "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" / "mylib-1.0.jar").string();
  const auto result = resolve(jar);

  REQUIRE(result.has_value());
  CHECK(result->ends_with("mylib-1.0-sources.jar"));
}
