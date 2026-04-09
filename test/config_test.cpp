#include <filesystem>
#include <string>

#include <doctest/doctest.h>

#include "config.hpp"
#include "test_common.hpp"

TEST_CASE("parses example config file") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");

    CHECK(cfg.version() == 1);
    CHECK(cfg.jvm_target() == "21");

    SUBCASE("build section") {
        REQUIRE(cfg.build().has_value());
        CHECK(cfg.build()->command.size() == 2);
        CHECK(cfg.build()->command[0] == "mybuild");
        CHECK(cfg.build()->command[1] == "gradle");
        CHECK(cfg.build()->gradle_args.size() == 1);
        CHECK(cfg.build()->gradle_args[0] == "--quiet");
    }

    SUBCASE("roots") {
        REQUIRE(cfg.roots().size() == 2);
        CHECK(cfg.root_path(cfg.roots()[0]).filename() == "proj_1");
        CHECK(cfg.root_path(cfg.roots()[1]).filename() == "proj_3");
    }

    SUBCASE("options") {
        CHECK(cfg.options().include_tests == false);
        CHECK(cfg.options().attach_sources == true);
        CHECK(cfg.options().follow_symlinks == true);
    }
}

TEST_CASE("stores config file and dir") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    CHECK(cfg.config_file().filename() == "example_config.yaml");
    CHECK(cfg.config_dir().filename() == "fixtures");
}

TEST_CASE("normalizes paths relative to config dir") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const auto config_dir = fs::absolute("test/fixtures").lexically_normal();

    CHECK(cfg.workspace_file() == config_dir / "workspace.json");
    CHECK(cfg.root_path(cfg.roots()[0]) == config_dir / "src/proj_1");
}

TEST_CASE("applies default values for optional fields") {
    const TempConfig tmp(R"(
version: 1
roots:
  - path: ./src/proj
)");
    const auto cfg = klspw::Config::from_yaml(tmp.path);

    CHECK(cfg.jvm_target() == "21");
    CHECK(cfg.options().include_tests == true);
    CHECK(cfg.options().attach_sources == true);
    CHECK(cfg.options().follow_symlinks == true);
    CHECK_FALSE(cfg.build().has_value());
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
      command: ["brazil-build", "gradle"]
      gradle_args: ["--no-daemon"]
)");
    const auto cfg = klspw::Config::from_yaml(tmp.path);

    REQUIRE(cfg.roots().size() == 2);

    SUBCASE("first root inherits global build") {
        const auto& build = cfg.build_for(cfg.roots()[0]);
        CHECK(build.command == klspw::strings{"./gradlew"});
        CHECK(build.gradle_args == klspw::strings{"--quiet"});
    }

    SUBCASE("second root uses per-root override") {
        const auto& build = cfg.build_for(cfg.roots()[1]);
        CHECK(build.command == klspw::strings{"brazil-build", "gradle"});
        CHECK(build.gradle_args == klspw::strings{"--no-daemon"});
    }
}

TEST_CASE("throws on unsupported config version") {
    const TempConfig tmp(R"(
version: 99
roots:
  - path: ./src/proj
)");
    CHECK_THROWS_AS((void)klspw::Config::from_yaml(tmp.path), std::runtime_error);
}

TEST_CASE("throws on root entry missing path") {
    const TempConfig tmp(R"(
version: 1
roots:
  - command: ["./gradlew"]
)");
    CHECK_THROWS_AS((void)klspw::Config::from_yaml(tmp.path), std::runtime_error);
}

TEST_CASE("throws on missing version") {
    const TempConfig tmp(R"(
roots:
  - path: ./src/proj
)");
    CHECK_THROWS_AS((void)klspw::Config::from_yaml(tmp.path), std::runtime_error);
}

TEST_CASE("throws on missing roots") {
    const TempConfig tmp(R"(
version: 1
)");
    CHECK_THROWS_AS((void)klspw::Config::from_yaml(tmp.path), std::runtime_error);
}

TEST_CASE("throws on nonexistent file") {
    CHECK_THROWS_AS((void)klspw::Config::from_yaml("/tmp/klspw_nonexistent_config.yaml"), std::runtime_error);
}

TEST_CASE("reads custom jvm_target") {
    const TempConfig tmp(R"(
version: 1
jvm_target: "17"
roots:
  - path: ./src/proj
)");
    const auto cfg = klspw::Config::from_yaml(tmp.path);
    CHECK(cfg.jvm_target() == "17");
}

TEST_CASE("compiler_arguments_json formats J-prefixed JSON") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const auto args = cfg.compiler_arguments_json();

    CHECK(args.starts_with("J{"));
    CHECK(args.ends_with("}"));
    CHECK(args.contains("jvmTarget"));
}

TEST_CASE("BuildConfig::args_for produces correct argument order") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const auto args = cfg.build()->args_for("/tmp/proj", "/tmp/init.gradle.kts");

    REQUIRE(args.size() == 8);
    CHECK(args[0] == "mybuild");
    CHECK(args[1] == "gradle");
    CHECK(args[2] == "--init-script");
    CHECK(args[3] == "/tmp/init.gradle.kts");
    CHECK(args[4] == "--quiet");
    CHECK(args[5] == "-p");
    CHECK(args[6] == "/tmp/proj");
    CHECK(args[7] == "dumpKotlinLspModel");
}

TEST_CASE("parses per-root build config without global build") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_per_root_build_config.yaml");

    CHECK(cfg.version() == 1);
    CHECK(cfg.jvm_target() == "21");
    CHECK_FALSE(cfg.build().has_value());
    CHECK(cfg.options().include_tests == true);

    REQUIRE(cfg.roots().size() == 2);

    SUBCASE("first root has its own build command") {
        const auto& root = cfg.roots()[0];
        CHECK(cfg.root_path(root).filename() == "proj_1");
        const auto& build = cfg.build_for(root);
        CHECK(build.command == klspw::strings{"./gradlew"});
        CHECK(build.gradle_args == klspw::strings{"--quiet"});
    }

    SUBCASE("second root has different build command") {
        const auto& root = cfg.roots()[1];
        CHECK(cfg.root_path(root).filename() == "proj_3");
        const auto& build = cfg.build_for(root);
        CHECK(build.command == klspw::strings{"brazil-build", "gradle"});
        CHECK(build.gradle_args == klspw::strings{"--no-daemon", "--stacktrace"});
    }
}
