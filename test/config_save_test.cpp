#include <doctest/doctest.h>

#include "config.hpp"
#include "test_common.hpp"

namespace {

klspw::ConfigData parse_config_yaml(const std::string& yaml) { return klspw::ConfigData::from_yaml(yaml); }

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
}

TEST_CASE("to_yaml with per-root build overrides round-trips") {
    const klspw::ConfigData original{
        .version = 1,
        .build = klspw::BuildConfig{.command = {"./gradlew"}},
        .roots =
            {
                {.path = "./proj_a"},
                {.path = "./proj_b",
                    .build = klspw::BuildConfig{.command = {"gradle"}, .gradle_args = {"--no-daemon"}}},
            },
    };

    const auto parsed = parse_config_yaml(original.to_yaml());

    REQUIRE(parsed.roots.size() == 2);
    CHECK(parsed.roots[0].path == "./proj_a");
    CHECK_FALSE(parsed.roots[0].build.has_value());
    CHECK(parsed.roots[1].path == "./proj_b");
    REQUIRE(parsed.roots[1].build.has_value());
    CHECK(parsed.roots[1].build->command == klspw::strings{"gradle"});
    CHECK(parsed.roots[1].build->gradle_args == klspw::strings{"--no-daemon"});
}

// --- StarterConfig ---

TEST_CASE("StarterConfig produces valid config data") {
    const TempDir root_dir;
    const auto data = klspw::StarterConfig{root_dir.path}.set_config_path(root_dir.path.parent_path()).to_config_data();

    CHECK(data.version == 1);
    CHECK(data.workspace_file == "./workspace.json");
    CHECK(data.jvm_target == "21");
    CHECK(data.build->command == klspw::strings{"./gradlew"});
    REQUIRE(data.roots.size() == 1);
    CHECK(data.roots[0].path.starts_with("./"));
}

TEST_CASE("StarterConfig root path is relative to config_dir") {
    const TempDir root_dir;
    const auto parent = std::filesystem::weakly_canonical(root_dir.path.parent_path());
    const auto data = klspw::StarterConfig{root_dir.path}.set_config_path(parent).to_config_data();

    const auto expected = "./" + root_dir.path.filename().string();
    CHECK(data.roots[0].path == expected);
}

TEST_CASE("StarterConfig respects custom jvm_target") {
    const TempDir root_dir;
    const auto data = klspw::StarterConfig{root_dir.path}
                          .set_config_path(root_dir.path.parent_path())
                          .set_jvm_target("17")
                          .to_config_data();
    CHECK(data.jvm_target == "17");
}

TEST_CASE("StarterConfig output passes ConfigData::validate") {
    const TempDir root_dir;
    const auto data = klspw::StarterConfig{root_dir.path}.set_config_path(root_dir.path.parent_path()).to_config_data();
    CHECK_NOTHROW(klspw::ValidateContext::require_valid(data));
}

TEST_CASE("StarterConfig throws on nonexistent root") {
    CHECK_THROWS_WITH_AS(klspw::StarterConfig{"/tmp/klspw_nonexistent_dir_xyz"},
        doctest::Contains("must be an existing directory"),
        std::runtime_error);
}

TEST_CASE("StarterConfig without config_path resolves root relative to cwd") {
    const auto data = klspw::StarterConfig{fs::temp_directory_path()}.to_config_data();

    CHECK(data.roots[0].path.starts_with("./"));
    CHECK_NOTHROW(klspw::ValidateContext::require_valid(data));
}

TEST_CASE("StarterConfig save_yaml_file throws without config_path") {
    CHECK_THROWS_WITH_AS(klspw::StarterConfig{fs::temp_directory_path()}.save_yaml_file(),
        doctest::Contains("no config path"),
        std::runtime_error);
}

TEST_CASE("StarterConfig save_yaml_file to directory appends default filename") {
    const TempDir root_dir;
    klspw::StarterConfig{root_dir.path}.set_config_path(root_dir.path).save_yaml_file();

    CHECK(fs::is_regular_file(root_dir.path / klspw::default_config_filename));
}

// --- End-to-end: StarterConfig -> to_yaml -> Config::load_yaml_file ---

TEST_CASE("StarterConfig to_yaml round-trips through Config::load_yaml_file") {
    const TempDir root_dir;
    const auto config_path = root_dir.path / "config.yaml";
    klspw::StarterConfig{root_dir.path}.set_config_path(config_path).save_yaml_file();

    const auto cfg = klspw::Config::load_yaml_file(config_path);
    const auto& data = cfg.data();
    CHECK(data.version == 1);
    CHECK(data.jvm_target == "21");
    REQUIRE(data.roots.size() == 1);
}
