#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <doctest/doctest.h>

#include "config.hpp"

namespace fs = std::filesystem;

namespace {

fs::path write_temp_config(const std::string& content) {
    const auto dir = fs::temp_directory_path() / "klspw_test";
    fs::create_directories(dir);
    const auto path = dir / "test_config.yaml";
    std::ofstream out(path);
    out << content;
    return path;
}

} // namespace

TEST_CASE("parses example config file") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");

    CHECK(cfg.version() == 1);
    CHECK(cfg.jvm_target() == "21");

    SUBCASE("build section") {
        CHECK(cfg.build().command().size() == 2);
        CHECK(cfg.build().command()[0] == "mybuild");
        CHECK(cfg.build().command()[1] == "gradle");
        CHECK(cfg.build().gradle_args().size() == 1);
        CHECK(cfg.build().gradle_args()[0] == "--quiet");
    }

    SUBCASE("roots") {
        REQUIRE(cfg.roots().size() == 3);
        CHECK(cfg.roots()[0].kind() == klspw::RootKind::kotlin_gradle);
        CHECK(cfg.roots()[1].kind() == klspw::RootKind::kotlin_gradle);
        CHECK(cfg.roots()[2].kind() == klspw::RootKind::java_binary);
        CHECK(cfg.roots()[2].lib_dir() == fs::path{"build/lib"});
    }

    SUBCASE("options") {
        CHECK(cfg.options().include_tests == false);
        CHECK(cfg.options().attach_sources == true);
        CHECK(cfg.options().follow_symlinks == true);
    }
}

TEST_CASE("normalizes paths relative to config dir") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const auto config_dir = fs::absolute("test/fixtures").lexically_normal();

    CHECK(cfg.workspace_file() == config_dir / "workspace.json");
    CHECK(cfg.roots()[0].path() == config_dir / "src/proj_1");
}

TEST_CASE("RootEntry resolves lib dir against root path") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const auto& java_root = cfg.roots()[2];

    CHECK(java_root.resolved_lib_dir() == java_root.path() / "build/lib");
}

TEST_CASE("applies default values for optional fields") {
    const auto* const yaml = R"(
version: 1
roots:
  - kind: kotlin_gradle
    path: ./src/proj
)";
    const auto path = write_temp_config(yaml);
    const auto cfg = klspw::Config::from_yaml(path);

    CHECK(cfg.jvm_target() == "21");
    CHECK(cfg.options().include_tests == false);
    CHECK(cfg.options().attach_sources == true);
    CHECK(cfg.options().follow_symlinks == true);
    CHECK(cfg.build().command().empty());
    CHECK(cfg.build().gradle_args().empty());

    fs::remove_all(path.parent_path());
}

TEST_CASE("applies default lib_dir for java_binary root") {
    const auto* const yaml = R"(
version: 1
roots:
  - kind: java_binary
    path: ./src/proj
)";
    const auto path = write_temp_config(yaml);
    const auto cfg = klspw::Config::from_yaml(path);

    REQUIRE(cfg.roots().size() == 1);
    CHECK(cfg.roots()[0].lib_dir() == fs::path{"build/lib"});

    fs::remove_all(path.parent_path());
}

TEST_CASE("throws on unsupported config version") {
    const auto* const yaml = R"(
version: 99
roots:
  - kind: kotlin_gradle
    path: ./src/proj
)";
    const auto path = write_temp_config(yaml);

    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(path), "Unsupported config version: 99", std::runtime_error);

    fs::remove_all(path.parent_path());
}

TEST_CASE("throws on root entry missing kind") {
    const auto* const yaml = R"(
version: 1
roots:
  - path: ./src/proj
)";
    const auto path = write_temp_config(yaml);

    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(path), "Config missing required field: kind",
                         std::runtime_error);

    fs::remove_all(path.parent_path());
}

TEST_CASE("throws on root entry missing path") {
    const auto* const yaml = R"(
version: 1
roots:
  - kind: kotlin_gradle
)";
    const auto path = write_temp_config(yaml);

    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(path), "Config missing required field: path",
                         std::runtime_error);

    fs::remove_all(path.parent_path());
}

TEST_CASE("throws on missing version") {
    const auto* const yaml = R"(
roots:
  - kind: kotlin_gradle
    path: ./src/proj
)";
    const auto path = write_temp_config(yaml);

    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(path), "Config missing required field: version",
                         std::runtime_error);

    fs::remove_all(path.parent_path());
}

TEST_CASE("throws on missing roots") {
    const auto* const yaml = R"(
version: 1
)";
    const auto path = write_temp_config(yaml);

    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(path), "Config missing required field: roots",
                         std::runtime_error);

    fs::remove_all(path.parent_path());
}

TEST_CASE("throws on unknown root kind") {
    const auto* const yaml = R"(
version: 1
roots:
  - kind: unknown_kind
    path: ./src/proj
)";
    const auto path = write_temp_config(yaml);

    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(path), "Unknown root kind: unknown_kind", std::runtime_error);

    fs::remove_all(path.parent_path());
}

TEST_CASE("throws on nonexistent file") {
    CHECK_THROWS_AS((void)klspw::Config::from_yaml("/tmp/klspw_nonexistent_config.yaml"), std::runtime_error);
}

TEST_CASE("reads custom jvm_target") {
    const auto* const yaml = R"(
version: 1
jvm_target: "17"
roots:
  - kind: kotlin_gradle
    path: ./src/proj
)";
    const auto path = write_temp_config(yaml);
    const auto cfg = klspw::Config::from_yaml(path);

    CHECK(cfg.jvm_target() == "17");

    fs::remove_all(path.parent_path());
}

TEST_CASE("reads custom lib_dir for java_binary root") {
    const auto* const yaml = R"(
version: 1
roots:
  - kind: java_binary
    path: ./src/proj
    lib_dir: custom/jars
)";
    const auto path = write_temp_config(yaml);
    const auto cfg = klspw::Config::from_yaml(path);

    REQUIRE(cfg.roots().size() == 1);
    CHECK(cfg.roots()[0].lib_dir() == fs::path{"custom/jars"});

    fs::remove_all(path.parent_path());
}
