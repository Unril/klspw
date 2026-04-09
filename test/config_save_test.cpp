#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "config.hpp"
#include "test_common.hpp"

namespace {

klspw::ConfigData parse_config_yaml(const std::string& yaml) {
    return klspw::ConfigData::from_yaml(yaml);
}

} // namespace

// --- ConfigData::to_yaml ---

TEST_CASE("to_yaml produces valid YAML that round-trips") {
    const klspw::ConfigData original{
        .version = 1,
        .workspace_file = "./workspace.json",
        .jvm_target = "21",
        .build = klspw::BuildConfig{.command = {"./gradlew"}, .gradle_args = {"--quiet"}},
        .roots = {{.path = "./src/proj"}},
    };

    const auto parsed = parse_config_yaml(original.to_yaml());

    CHECK(parsed.version == 1);
    CHECK(parsed.workspace_file == "./workspace.json");
    CHECK(parsed.jvm_target == "21");
    CHECK(parsed.build->command == klspw::strings{"./gradlew"});
    CHECK(parsed.build->gradle_args == klspw::strings{"--quiet"});
    REQUIRE(parsed.roots.size() == 1);
    CHECK(parsed.roots[0].path == "./src/proj");
}

TEST_CASE("to_yaml preserves default options through round-trip") {
    const klspw::ConfigData original{
        .version = 1,
        .roots = {{.path = "./src/proj"}},
    };

    const auto parsed = parse_config_yaml(original.to_yaml());

    CHECK(parsed.options.include_tests == true);
    CHECK(parsed.options.attach_sources == true);
    CHECK(parsed.options.follow_symlinks == true);
}

TEST_CASE("to_yaml with per-root build overrides round-trips") {
    const klspw::ConfigData original{
        .version = 1,
        .build = klspw::BuildConfig{.command = {"./gradlew"}},
        .roots =
            {
                {.path = "./proj_a"},
                {.path = "./proj_b",
                 .build = klspw::BuildConfig{.command = {"brazil-build", "gradle"}, .gradle_args = {"--no-daemon"}}},
            },
    };

    const auto parsed = parse_config_yaml(original.to_yaml());

    REQUIRE(parsed.roots.size() == 2);
    CHECK(parsed.roots[0].path == "./proj_a");
    CHECK_FALSE(parsed.roots[0].build.has_value());
    CHECK(parsed.roots[1].path == "./proj_b");
    REQUIRE(parsed.roots[1].build.has_value());
    CHECK(parsed.roots[1].build->command == klspw::strings{"brazil-build", "gradle"});
    CHECK(parsed.roots[1].build->gradle_args == klspw::strings{"--no-daemon"});
}

// --- Config::make_starter ---

TEST_CASE("make_starter produces valid config data") {
    const TempDir root_dir;
    const auto data = klspw::Config::make_starter(root_dir.path, root_dir.path.parent_path());

    CHECK(data.version == 1);
    CHECK(data.workspace_file == "./workspace.json");
    CHECK(data.jvm_target == "21");
    CHECK(data.build->command == klspw::strings{"./gradlew"});
    REQUIRE(data.roots.size() == 1);
    CHECK(data.roots[0].path.starts_with("./"));
}

TEST_CASE("make_starter root path is relative to config_dir") {
    const TempDir root_dir;
    const auto parent = std::filesystem::weakly_canonical(root_dir.path.parent_path());
    const auto data = klspw::Config::make_starter(root_dir.path, parent);

    const auto expected = "./" + root_dir.path.filename().string();
    CHECK(data.roots[0].path == expected);
}

TEST_CASE("make_starter respects custom jvm_target") {
    const TempDir root_dir;
    const auto data = klspw::Config::make_starter(root_dir.path, root_dir.path.parent_path(), "17");
    CHECK(data.jvm_target == "17");
}

TEST_CASE("make_starter output passes ConfigData::validate") {
    const TempDir root_dir;
    const auto data = klspw::Config::make_starter(root_dir.path, root_dir.path.parent_path());
    CHECK_NOTHROW(data.validate());
}

TEST_CASE("make_starter throws on nonexistent root") {
    CHECK_THROWS_AS(klspw::Config::make_starter("/tmp/klspw_nonexistent_dir_xyz", "/tmp"), std::runtime_error);
}

// --- End-to-end: make_starter -> to_yaml -> from_yaml ---

TEST_CASE("make_starter to_yaml round-trips through Config::from_yaml") {
    const TempDir root_dir;
    const auto yaml = klspw::Config::make_starter(root_dir.path, root_dir.path).to_yaml();

    // Write inside root_dir so TempDir RAII handles cleanup.
    const auto config_path = root_dir.path / "config.yaml";
    klspw::write_file(config_path, yaml);

    const auto cfg = klspw::Config::from_yaml(config_path);
    CHECK(cfg.version() == 1);
    CHECK(cfg.jvm_target() == "21");
    REQUIRE(cfg.roots().size() == 1);
}
