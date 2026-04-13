#include "files.hpp"

#include <array>

#include <doctest/doctest.h>

#include "test_common.hpp"

using namespace klspw;

// --- find_dirs_with_markers ---

namespace {

constexpr std::array markers = {"settings.gradle"sv, "settings.gradle.kts"sv};

/// Create a marker file inside a directory.
void touch(const path& dir, std::string_view filename) { write_file(dir / filename, ""); }

}  // namespace

TEST_CASE("find_dirs_with_markers discovers directory with first marker") {
  const TempDir root;
  const auto proj = root.path / "proj";
  fs::create_directories(proj);
  touch(proj, "settings.gradle");

  const auto found = find_dirs_with_markers(root.path, markers);

  REQUIRE(found.size() == 1);
  CHECK(found[0] == fs::weakly_canonical(proj));
}

TEST_CASE("find_dirs_with_markers discovers directory with second marker") {
  const TempDir root;
  const auto proj = root.path / "proj";
  fs::create_directories(proj);
  touch(proj, "settings.gradle.kts");

  const auto found = find_dirs_with_markers(root.path, markers);

  REQUIRE(found.size() == 1);
  CHECK(found[0] == fs::weakly_canonical(proj));
}

TEST_CASE("find_dirs_with_markers discovers multiple roots sorted") {
  const TempDir root;
  const auto b = root.path / "b_proj";
  const auto a = root.path / "a_proj";
  fs::create_directories(a);
  fs::create_directories(b);
  touch(a, "settings.gradle.kts");
  touch(b, "settings.gradle");

  const auto found = find_dirs_with_markers(root.path, markers);

  REQUIRE(found.size() == 2);
  CHECK(found[0] == fs::weakly_canonical(a));
  CHECK(found[1] == fs::weakly_canonical(b));
}

TEST_CASE("find_dirs_with_markers skips hidden directories") {
  const TempDir root;
  const auto hidden = root.path / ".hidden";
  fs::create_directories(hidden);
  touch(hidden, "settings.gradle");

  const auto found = find_dirs_with_markers(root.path, markers);

  CHECK(found.empty());
}

TEST_CASE("find_dirs_with_markers does not descend into matched directories") {
  const TempDir root;
  const auto parent = root.path / "parent";
  const auto child = parent / "child";
  fs::create_directories(child);
  touch(parent, "settings.gradle.kts");
  touch(child, "settings.gradle.kts");

  const auto found = find_dirs_with_markers(root.path, markers);

  REQUIRE(found.size() == 1);
  CHECK(found[0] == fs::weakly_canonical(parent));
}

TEST_CASE("find_dirs_with_markers returns empty for no matches") {
  const TempDir root;
  const auto proj = root.path / "proj";
  fs::create_directories(proj);
  touch(proj, "build.gradle");  // not a marker

  const auto found = find_dirs_with_markers(root.path, markers);

  CHECK(found.empty());
}

TEST_CASE("find_dirs_with_markers discovers nested roots at different depths") {
  const TempDir root;
  const auto shallow = root.path / "shallow";
  const auto deep = root.path / "src" / "deep";
  fs::create_directories(shallow);
  fs::create_directories(deep);
  touch(shallow, "settings.gradle");
  touch(deep, "settings.gradle.kts");

  const auto found = find_dirs_with_markers(root.path, markers);

  CHECK(found.size() == 2);
}

TEST_CASE("find_dirs_with_markers throws on nonexistent search directory") {
  CHECK_THROWS_WITH_AS(find_dirs_with_markers("/tmp/klspw_nonexistent_xyz", markers),
                       doctest::Contains("does not exist"), std::runtime_error);
}

TEST_CASE("find_dirs_with_markers throws on empty markers") {
  const TempDir root;
  CHECK_THROWS_WITH_AS(find_dirs_with_markers(root.path, {}), doctest::Contains("at least one marker"),
                       std::runtime_error);
}
