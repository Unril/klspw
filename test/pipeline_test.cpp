#include <stdexcept>
#include <string>

#include <doctest/doctest.h>

#include "config.hpp"
#include "test_common.hpp"

// --- Config::validate ---

TEST_CASE("validate throws on missing root path") {
    const TempConfig tmp(R"(
version: 1
build:
  command: ["./gradlew"]
roots:
  - path: ./nonexistent_dir
)");
    const auto cfg = klspw::Config::from_yaml(tmp.path);
    CHECK_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("validate succeeds with existing root path") {
    const TempDir root_dir;
    const auto yaml = std::format(R"(
version: 1
build:
  command: ["./gradlew"]
roots:
  - path: {}
)", root_dir.path.string());

    const TempConfig tmp(yaml);
    const auto cfg = klspw::Config::from_yaml(tmp.path);
    CHECK_NOTHROW(cfg.validate());
}

TEST_CASE("validate throws on missing build command") {
    const TempDir root_dir;
    const auto yaml = std::format(R"(
version: 1
roots:
  - path: {}
)", root_dir.path.string());

    const TempConfig tmp(yaml);
    const auto cfg = klspw::Config::from_yaml(tmp.path);
    CHECK_THROWS_AS(cfg.validate(), std::runtime_error);
}

TEST_CASE("validate accepts per-root build command without global") {
    const TempDir root_dir;
    const auto yaml = std::format(R"(
version: 1
roots:
  - path: {}
    command: ["./gradlew"]
)", root_dir.path.string());

    const TempConfig tmp(yaml);
    const auto cfg = klspw::Config::from_yaml(tmp.path);
    CHECK_NOTHROW(cfg.validate());
}

TEST_CASE("validate throws on missing workspace_file parent") {
    const TempDir root_dir;
    const auto yaml = std::format(R"(
version: 1
workspace_file: ./nonexistent_parent/workspace.json
build:
  command: ["./gradlew"]
roots:
  - path: {}
)", root_dir.path.string());

    const TempConfig tmp(yaml);
    const auto cfg = klspw::Config::from_yaml(tmp.path);
    CHECK_THROWS_AS(cfg.validate(), std::runtime_error);
}
