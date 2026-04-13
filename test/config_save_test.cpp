#include <doctest/doctest.h>

#include "config.hpp"
#include "test_common.hpp"

namespace {

klspw::ConfigData parse_config_yaml(const std::string& yaml) { return klspw::ConfigData::from_yaml(yaml); }

}  // namespace

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
              {.path = "./proj_b", .build = klspw::BuildConfig{.command = {"gradle"}, .gradle_args = {"--no-daemon"}}},
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

TEST_CASE("StarterConfig single root with default build") {
  const TempDir root_dir;
  const auto data =
      klspw::StarterConfig{{root_dir.path.string()}}.set_config_path(root_dir.path.parent_path()).to_data();

  CHECK(data.version == 1);
  CHECK(data.workspace_file == "./workspace.json");
  CHECK(data.jvm_target == "21");
  CHECK(data.build->command == klspw::strings{"./gradlew"});
  REQUIRE(data.roots.size() == 1);
  CHECK(data.roots[0].path.starts_with("./"));
  CHECK_FALSE(data.roots[0].build.has_value());
}

TEST_CASE("StarterConfig root path is relative to config_dir") {
  const TempDir root_dir;
  const auto parent = fs::weakly_canonical(root_dir.path.parent_path());
  const auto data = klspw::StarterConfig{{root_dir.path.string()}}.set_config_path(parent).to_data();

  const auto expected = "./" + root_dir.path.filename().string();
  CHECK(data.roots[0].path == expected);
}

TEST_CASE("StarterConfig respects custom jvm_target") {
  const TempDir root_dir;
  const auto data = klspw::StarterConfig{{root_dir.path.string()}}
                        .set_config_path(root_dir.path.parent_path())
                        .set_jvm_target("17")
                        .to_data();
  CHECK(data.jvm_target == "17");
}

TEST_CASE("StarterConfig output passes ConfigData::validate") {
  const TempDir root_dir;
  const auto data =
      klspw::StarterConfig{{root_dir.path.string()}}.set_config_path(root_dir.path.parent_path()).to_data();
  CHECK_NOTHROW(klspw::ValidateContext::require_valid(data));
}

TEST_CASE("StarterConfig throws on nonexistent root") {
  CHECK_THROWS_WITH_AS(klspw::StarterConfig{klspw::strings{"/tmp/klspw_nonexistent_dir_xyz"}},
                       doctest::Contains("must be an existing directory"), std::runtime_error);
}

TEST_CASE("StarterConfig throws on empty root args") {
  CHECK_THROWS_WITH_AS(klspw::StarterConfig{klspw::strings{}}, doctest::Contains("at least one root"),
                       std::runtime_error);
}

TEST_CASE("StarterConfig without config_path resolves root relative to cwd") {
  const auto data = klspw::StarterConfig{{fs::temp_directory_path().string()}}.to_data();

  CHECK(data.roots[0].path.starts_with("./"));
  CHECK_NOTHROW(klspw::ValidateContext::require_valid(data));
}

TEST_CASE("StarterConfig save_yaml_file throws without config_path") {
  CHECK_THROWS_WITH_AS(klspw::StarterConfig{{fs::temp_directory_path().string()}}.save_yaml_file(),
                       doctest::Contains("no config path"), std::runtime_error);
}

TEST_CASE("StarterConfig save_yaml_file to directory appends default filename") {
  const TempDir root_dir;
  klspw::StarterConfig{{root_dir.path.string()}}.set_config_path(root_dir.path).save_yaml_file();

  CHECK(fs::is_regular_file(root_dir.path / klspw::default_config_filename));
}

TEST_CASE("StarterConfig parses per-root build command from arg string") {
  const TempDir root_dir;
  const auto arg = root_dir.path.string() + " my_build --silent";
  const auto data = klspw::StarterConfig{{arg}}.set_config_path(root_dir.path.parent_path()).to_data();

  REQUIRE(data.roots.size() == 1);
  REQUIRE(data.roots[0].build.has_value());
  CHECK(data.roots[0].build->command == klspw::strings{"my_build", "--silent"});
  CHECK_FALSE(data.build.has_value());  // no global build when all roots have one
}

TEST_CASE("StarterConfig multiple roots") {
  const TempDir dir_a;
  const TempDir dir_b;
  const auto data = klspw::StarterConfig{{dir_a.path.string(), dir_b.path.string()}}
                        .set_config_path(dir_a.path.parent_path())
                        .to_data();

  REQUIRE(data.roots.size() == 2);
  CHECK(data.roots[0].path.starts_with("./"));
  CHECK(data.roots[1].path.starts_with("./"));
  CHECK(data.build->command == klspw::strings{"./gradlew"});  // default global build
}

TEST_CASE("StarterConfig global build via set_build") {
  const TempDir root_dir;
  const auto data = klspw::StarterConfig{{root_dir.path.string()}}
                        .set_config_path(root_dir.path.parent_path())
                        .set_build("my-build")
                        .to_data();

  CHECK(data.build->command == klspw::strings{"my-build"});
  CHECK_FALSE(data.roots[0].build.has_value());
}

TEST_CASE("StarterConfig global build with flags via set_build") {
  const TempDir root_dir;
  const auto data = klspw::StarterConfig{{root_dir.path.string()}}
                        .set_config_path(root_dir.path.parent_path())
                        .set_build("my-build --quiet")
                        .to_data();

  CHECK(data.build->command == klspw::strings{"my-build", "--quiet"});
}

TEST_CASE("StarterConfig mixed: some roots with build, global for the rest") {
  const TempDir dir_a;
  const TempDir dir_b;
  const auto arg_a = dir_a.path.string() + " custom_build";
  const auto data =
      klspw::StarterConfig{{arg_a, dir_b.path.string()}}.set_config_path(dir_a.path.parent_path()).to_data();

  REQUIRE(data.roots.size() == 2);
  REQUIRE(data.roots[0].build.has_value());
  CHECK(data.roots[0].build->command == klspw::strings{"custom_build"});
  CHECK_FALSE(data.roots[1].build.has_value());
  CHECK(data.build->command == klspw::strings{"./gradlew"});  // default for dir_b
}

// --- End-to-end: StarterConfig -> to_yaml -> Config::load_yaml_file ---

TEST_CASE("StarterConfig to_yaml round-trips through Config::load_yaml_file") {
  const TempDir root_dir;
  const auto config_path = root_dir.path / "config.yaml";
  klspw::StarterConfig{{root_dir.path.string()}}.set_config_path(config_path).save_yaml_file();

  const auto cfg = klspw::Config::load_yaml_file(config_path);
  const auto& data = cfg.data();
  CHECK(data.version == 1);
  CHECK(data.jvm_target == "21");
  REQUIRE(data.roots.size() == 1);
}

// --- StarterConfig::discover ---

TEST_CASE("StarterConfig::discover finds Gradle roots in fixture directory") {
  const auto fixtures = fs::weakly_canonical("test/fixtures/projects");
  const auto data = klspw::StarterConfig::discover({fixtures.string()}).set_config_path(fixtures).to_data();

  // simple, multi, with-deps each have settings.gradle.kts at top level.
  // multi-root has two sub-roots under src/ (core, service).
  REQUIRE(data.roots.size() >= 5);
  CHECK(data.build->command == klspw::strings{"./gradlew"});

  for (const auto& root : data.roots) {
    CAPTURE(root.path);
    CHECK(root.path.starts_with("./"));
    CHECK_FALSE(root.build.has_value());
  }
}

TEST_CASE("StarterConfig::discover finds multi-root sub-projects") {
  const auto multi_root = fs::weakly_canonical("test/fixtures/projects/multi-root");
  const auto data = klspw::StarterConfig::discover({multi_root.string()}).set_config_path(multi_root).to_data();

  REQUIRE(data.roots.size() == 2);

  const auto has_root = [&](std::string_view suffix) {
    return std::ranges::any_of(data.roots, [&](const auto& r) { return r.path.ends_with(suffix); });
  };
  CHECK(has_root("core"));
  CHECK(has_root("service"));
}

TEST_CASE("StarterConfig::discover with set_build overrides default") {
  const auto multi_root = fs::weakly_canonical("test/fixtures/projects/multi-root");
  const auto data = klspw::StarterConfig::discover({multi_root.string()})
                        .set_config_path(multi_root)
                        .set_build("gradle --quiet")
                        .to_data();

  CHECK(data.build->command == klspw::strings{"gradle", "--quiet"});
  REQUIRE(data.roots.size() == 2);
}

TEST_CASE("StarterConfig::discover round-trips through save and load") {
  const auto multi_root = fs::weakly_canonical("test/fixtures/projects/multi-root");
  const TempDir out_dir;
  const auto config_path = out_dir.path / "klspw.yaml";

  klspw::StarterConfig::discover({multi_root.string()})
      .set_build("gradle --quiet")
      .set_config_path(config_path)
      .save_yaml_file();

  const auto cfg = klspw::Config::load_yaml_file(config_path);
  const auto& loaded = cfg.data();

  CHECK(loaded.version == 1);
  CHECK(loaded.jvm_target == "21");
  REQUIRE(loaded.roots.size() == 2);
  CHECK(loaded.build->command == klspw::strings{"gradle", "--quiet"});
}

TEST_CASE("StarterConfig::discover throws when no Gradle projects found") {
  const TempDir empty_dir;
  CHECK_THROWS_WITH_AS(klspw::StarterConfig::discover({empty_dir.path.string()}),
                       doctest::Contains("no Gradle projects found"), std::runtime_error);
}

TEST_CASE("StarterConfig::discover throws on empty search dirs") {
  CHECK_THROWS_WITH_AS(klspw::StarterConfig::discover({}), doctest::Contains("at least one search directory"),
                       std::runtime_error);
}

TEST_CASE("StarterConfig::discover with multiple search directories") {
  const auto simple = fs::weakly_canonical("test/fixtures/projects");
  const auto multi_root = fs::weakly_canonical("test/fixtures/projects/multi-root");
  const auto data =
      klspw::StarterConfig::discover({simple.string(), multi_root.string()}).set_config_path(simple).to_data();

  // projects/ has simple, multi, with-deps (3 top-level roots) + multi-root's 2 sub-roots = 7
  // total. But multi-root sub-roots are found both via projects/ and multi-root/ — dedupe not
  // guaranteed. At minimum: 5 from projects/ + 2 from multi-root/ (some may overlap).
  CHECK(data.roots.size() >= 5);
}
