#include "config.hpp"

#include <filesystem>
#include <string>

#include <doctest/doctest.h>

#include "test_common.hpp"

TEST_CASE("parses example config file") {
  const auto cfg = klspw::Config::load_yaml_file("test/fixtures/example_config.yaml");
  const auto& data = cfg.data();

  CHECK(data.version == 1);
  CHECK(data.jvm_target == "21");

  SUBCASE("build section") {
    REQUIRE(data.build.has_value());
    CHECK(data.build->command.size() == 2);
    CHECK(data.build->command[0] == "mybuild");
    CHECK(data.build->command[1] == "gradle");
    CHECK(data.build->gradle_args.size() == 1);
    CHECK(data.build->gradle_args[0] == "--quiet");
  }

  SUBCASE("roots") {
    REQUIRE(data.roots.size() == 2);
    CHECK(cfg.root_path(data.roots[0]).filename() == "proj_1");
    CHECK(cfg.root_path(data.roots[1]).filename() == "proj_3");
  }

  SUBCASE("options") {
    CHECK(data.options.include_tests == false);
    CHECK(data.options.attach_sources == true);
  }
}

TEST_CASE("stores config file and dir") {
  const auto cfg = klspw::Config::load_yaml_file("test/fixtures/example_config.yaml");
  CHECK(cfg.config_file().filename() == "example_config.yaml");
  CHECK(cfg.config_dir().filename() == "fixtures");
}

TEST_CASE("normalizes paths relative to config dir") {
  const auto cfg = klspw::Config::load_yaml_file("test/fixtures/example_config.yaml");
  const auto config_dir = fs::absolute("test/fixtures").lexically_normal();

  REQUIRE(cfg.workspace_file().has_value());
  CHECK(*cfg.workspace_file() == config_dir / "workspace.json");
  CHECK(cfg.root_path(cfg.data().roots[0]) == config_dir / "src/proj_1");
}

TEST_CASE("applies default values for optional fields") {
  const TempConfig tmp(R"(
version: 1
roots:
  - path: ./src/proj
)");
  const auto cfg = klspw::Config::load_yaml_file(tmp.path);
  const auto& data = cfg.data();

  CHECK(data.jvm_target == "21");
  CHECK(data.options.include_tests == true);
  CHECK(data.options.attach_sources == true);
  CHECK_FALSE(data.build.has_value());
}

TEST_CASE("per-root build override") {
  const TempConfig tmp(R"(
version: 1
build:
  command: ["./gradlew"]
  gradle_args: ["--quiet"]
roots:
  - path: ./src/proj_a
  - path: ./src/proj_b
    build:
      command: ["gradle"]
      gradle_args: ["--no-daemon"]
)");
  const auto cfg = klspw::Config::load_yaml_file(tmp.path);
  const auto& data = cfg.data();

  REQUIRE(data.roots.size() == 2);

  SUBCASE("first root inherits global build") {
    const auto& build = data.build_for(data.roots[0]);
    CHECK(build.command == klspw::strings{"./gradlew"});
    CHECK(build.gradle_args == klspw::strings{"--quiet"});
  }

  SUBCASE("second root uses per-root override") {
    const auto& build = data.build_for(data.roots[1]);
    CHECK(build.command == klspw::strings{"gradle"});
    CHECK(build.gradle_args == klspw::strings{"--no-daemon"});
  }
}

TEST_CASE("throws on unsupported config version") {
  const TempConfig tmp(R"(
version: 99
roots:
  - path: ./src/proj
)");
  CHECK_THROWS_WITH_AS((void)klspw::Config::load_yaml_file(tmp.path), doctest::Contains("version"), std::runtime_error);
}

TEST_CASE("throws on root entry missing path") {
  const TempConfig tmp(R"(
version: 1
roots:
  - command: ["./gradlew"]
)");
  CHECK_THROWS_WITH_AS((void)klspw::Config::load_yaml_file(tmp.path), doctest::Contains("path"), std::runtime_error);
}

TEST_CASE("throws on missing version") {
  const TempConfig tmp(R"(
roots:
  - path: ./src/proj
)");
  CHECK_THROWS_WITH_AS((void)klspw::Config::load_yaml_file(tmp.path), doctest::Contains("version"), std::runtime_error);
}

TEST_CASE("throws on missing roots") {
  const TempConfig tmp(R"(
version: 1
)");
  CHECK_THROWS_WITH_AS((void)klspw::Config::load_yaml_file(tmp.path), doctest::Contains("roots"), std::runtime_error);
}

TEST_CASE("throws on nonexistent file") {
  CHECK_THROWS_WITH_AS((void)klspw::Config::load_yaml_file("/tmp/klspw_nonexistent_config.yaml"),
                       doctest::Contains("not found"), std::runtime_error);
}

TEST_CASE("reads custom jvm_target") {
  const TempConfig tmp(R"(
version: 1
jvm_target: "17"
roots:
  - path: ./src/proj
)");
  const auto cfg = klspw::Config::load_yaml_file(tmp.path);
  CHECK(cfg.data().jvm_target == "17");
}

TEST_CASE("compiler_arguments_json formats J-prefixed JSON") {
  const auto cfg = klspw::Config::load_yaml_file("test/fixtures/example_config.yaml");
  const auto args = cfg.data().compiler_arguments_json();

  CHECK(args.starts_with("J{"));
  CHECK(args.ends_with("}"));
  CHECK(args.contains("jvmTarget"));
}

TEST_CASE("BuildConfig::args_for produces correct argument order") {
  const auto cfg = klspw::Config::load_yaml_file("test/fixtures/example_config.yaml");
  const auto args = cfg.data().build->args_for("/tmp/init.gradle.kts");

  // mybuild gradle --no-configuration-cache --init-script /tmp/init.gradle.kts --quiet
  // dumpKotlinLspModel
  REQUIRE(args.size() == 7);
  CHECK(args[0] == "mybuild");
  CHECK(args[1] == "gradle");
  CHECK(args[2] == "--no-configuration-cache");
  CHECK(args[3] == "--init-script");
  CHECK(args[4] == "/tmp/init.gradle.kts");
  CHECK(args[5] == "--quiet");
  CHECK(args[6] == "dumpKotlinLspModel");
}

TEST_CASE("parses per-root build config without global build") {
  const auto cfg = klspw::Config::load_yaml_file("test/fixtures/example_per_root_build_config.yaml");
  const auto& data = cfg.data();

  CHECK(data.version == 1);
  CHECK(data.jvm_target == "21");
  CHECK_FALSE(data.build.has_value());
  CHECK(data.options.include_tests == true);

  REQUIRE(data.roots.size() == 2);

  SUBCASE("first root has its own build command") {
    const auto& root = data.roots[0];
    CHECK(cfg.root_path(root).filename() == "proj_1");
    const auto& build = data.build_for(root);
    CHECK(build.command == klspw::strings{"./gradlew"});
    CHECK(build.gradle_args == klspw::strings{"--quiet"});
  }

  SUBCASE("second root has different build command") {
    const auto& root = data.roots[1];
    CHECK(cfg.root_path(root).filename() == "proj_3");
    const auto& build = data.build_for(root);
    CHECK(build.command == klspw::strings{"gradle"});
    CHECK(build.gradle_args == klspw::strings{"--no-daemon", "--stacktrace"});
  }
}

TEST_CASE("throws on malformed YAML") {
  const TempConfig tmp("{{{{ not valid yaml at all ::::");
  CHECK_THROWS_WITH_AS((void)klspw::Config::load_yaml_file(tmp.path), doctest::Contains("Failed to parse config YAML"),
                       std::runtime_error);
}
