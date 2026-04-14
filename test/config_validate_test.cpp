#include <stdexcept>
#include <string>

#include <doctest/doctest.h>

#include "config.hpp"
#include "test_common.hpp"

namespace {

void require_valid(const klspw::Config& cfg) { klspw::ValidateContext::require_valid(cfg); }

}  // namespace

// --- Config::validate ---

TEST_CASE("validate throws on missing root path") {
  const TempConfig tmp(R"(
version: 1
build:
  command: ["./gradlew"]
roots:
  - path: ./nonexistent_dir
)");
  const auto cfg = klspw::Config::load_yaml_file(tmp.path);
  CHECK_THROWS_WITH_AS(require_valid(cfg), doctest::Contains("does not exist"), std::runtime_error);
}

TEST_CASE("validate succeeds with existing root path") {
  const TempDir root_dir;
  const auto yaml = std::format(R"(
version: 1
build:
  command: ["./gradlew"]
roots:
  - path: {}
)",
                                root_dir.path.string());

  const TempConfig tmp(yaml);
  const auto cfg = klspw::Config::load_yaml_file(tmp.path);
  CHECK_NOTHROW(require_valid(cfg));
}

TEST_CASE("validate throws on missing build command") {
  const TempDir root_dir;
  const auto yaml = std::format(R"(
version: 1
roots:
  - path: {}
)",
                                root_dir.path.string());

  const TempConfig tmp(yaml);
  const auto cfg = klspw::Config::load_yaml_file(tmp.path);
  CHECK_THROWS_WITH_AS(require_valid(cfg), doctest::Contains("no build command"), std::runtime_error);
}

TEST_CASE("validate accepts per-root build command without global") {
  const TempDir root_dir;
  const auto yaml = std::format(R"(
version: 1
roots:
  - path: {}
    build:
      command: ["./gradlew"]
)",
                                root_dir.path.string());

  const TempConfig tmp(yaml);
  const auto cfg = klspw::Config::load_yaml_file(tmp.path);
  CHECK_NOTHROW(require_valid(cfg));
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
)",
                                root_dir.path.string());

  const TempConfig tmp(yaml);
  const auto cfg = klspw::Config::load_yaml_file(tmp.path);
  CHECK_THROWS_WITH_AS(require_valid(cfg), doctest::Contains("workspace_file"), std::runtime_error);
}

TEST_CASE("ValidateContext collects multiple errors") {
  const TempDir root_dir;
  const auto yaml = std::format(R"(
version: 99
workspace_file: ./nonexistent_parent/workspace.json
roots:
  - path: {}
)",
                                root_dir.path.string());

  const TempConfig tmp(yaml);

  // Schema validation catches version error
  CHECK_THROWS_WITH_AS(klspw::Config::load_yaml_file(tmp.path), doctest::Contains("version"), std::runtime_error);
}

TEST_CASE("ValidateContext collects filesystem and semantic errors") {
  const TempConfig tmp(R"(
version: 1
roots:
  - path: ./nonexistent_dir_a
  - path: ./nonexistent_dir_b
)");
  const auto cfg = klspw::Config::load_yaml_file(tmp.path);

  klspw::ValidateContext ctx;
  cfg.validate(ctx);

  // Should have at least: 2 missing root paths + no build command
  CHECK(ctx.errors().size() >= 3);
  CHECK_THROWS_AS(ctx.throw_if_errors(), std::runtime_error);
}

// --- ValidateContext::has_errors ---

TEST_CASE("ValidateContext has_errors returns false when no errors") {
  const klspw::ValidateContext ctx;
  CHECK_FALSE(ctx.has_errors());
}

TEST_CASE("ValidateContext has_errors returns true after failed check") {
  klspw::ValidateContext ctx;
  ctx.check(false, "something went wrong");
  CHECK(ctx.has_errors());
}
