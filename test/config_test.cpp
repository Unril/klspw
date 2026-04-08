#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <doctest/doctest.h>

#include "config.hpp"

namespace fs = std::filesystem;

namespace {

struct TempConfig {
    fs::path path;

    explicit TempConfig(const std::string& content) {
        const auto dir = fs::temp_directory_path() / "klspw_test";
        fs::create_directories(dir);
        static std::atomic<int> counter{0};
        path = dir / ("config_" + std::to_string(counter++) + ".yaml");
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("Failed to write temp config: " + path.string());
        }
        out << content;
    }

    ~TempConfig() {
        std::error_code ec;
        fs::remove(path, ec);
    }

    TempConfig(const TempConfig&) = delete;
    TempConfig& operator=(const TempConfig&) = delete;
    TempConfig(TempConfig&&) = delete;
    TempConfig& operator=(TempConfig&&) = delete;
};

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
    const TempConfig cfg(yaml);
    const auto config = klspw::Config::from_yaml(cfg.path);

    CHECK(config.jvm_target() == "21");
    CHECK(config.options().include_tests == false);
    CHECK(config.options().attach_sources == true);
    CHECK(config.options().follow_symlinks == true);
    CHECK(config.build().command().empty());
    CHECK(config.build().gradle_args().empty());
}

TEST_CASE("applies default lib_dir for java_binary root") {
    const auto* const yaml = R"(
version: 1
roots:
  - kind: java_binary
    path: ./src/proj
)";
    const TempConfig cfg(yaml);
    const auto config = klspw::Config::from_yaml(cfg.path);

    REQUIRE(config.roots().size() == 1);
    CHECK(config.roots()[0].lib_dir() == fs::path{"build/lib"});
}

TEST_CASE("throws on unsupported config version") {
    const auto* const yaml = R"(
version: 99
roots:
  - kind: kotlin_gradle
    path: ./src/proj
)";
    const TempConfig cfg(yaml);
    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(cfg.path), "Unsupported config version: 99",
                         std::runtime_error);
}

TEST_CASE("throws on root entry missing kind") {
    const auto* const yaml = R"(
version: 1
roots:
  - path: ./src/proj
)";
    const TempConfig cfg(yaml);
    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(cfg.path), "Config missing required field: kind",
                         std::runtime_error);
}

TEST_CASE("throws on root entry missing path") {
    const auto* const yaml = R"(
version: 1
roots:
  - kind: kotlin_gradle
)";
    const TempConfig cfg(yaml);
    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(cfg.path), "Config missing required field: path",
                         std::runtime_error);
}

TEST_CASE("throws on missing version") {
    const auto* const yaml = R"(
roots:
  - kind: kotlin_gradle
    path: ./src/proj
)";
    const TempConfig cfg(yaml);
    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(cfg.path), "Config missing required field: version",
                         std::runtime_error);
}

TEST_CASE("throws on missing roots") {
    const auto* const yaml = R"(
version: 1
)";
    const TempConfig cfg(yaml);
    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(cfg.path), "Config missing required field: roots",
                         std::runtime_error);
}

TEST_CASE("throws on unknown root kind") {
    const auto* const yaml = R"(
version: 1
roots:
  - kind: unknown_kind
    path: ./src/proj
)";
    const TempConfig cfg(yaml);
    CHECK_THROWS_WITH_AS((void)klspw::Config::from_yaml(cfg.path), "Unknown root kind: unknown_kind",
                         std::runtime_error);
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
    const TempConfig cfg(yaml);
    const auto config = klspw::Config::from_yaml(cfg.path);

    CHECK(config.jvm_target() == "17");
}

TEST_CASE("reads custom lib_dir for java_binary root") {
    const auto* const yaml = R"(
version: 1
roots:
  - kind: java_binary
    path: ./src/proj
    lib_dir: custom/jars
)";
    const TempConfig cfg(yaml);
    const auto config = klspw::Config::from_yaml(cfg.path);

    REQUIRE(config.roots().size() == 1);
    CHECK(config.roots()[0].lib_dir() == fs::path{"custom/jars"});
}
