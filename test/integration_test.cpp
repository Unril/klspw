#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "pipeline.hpp"
#include "test_common.hpp"

namespace {

/// Build a Config pointing at a fixture project.
/// Writes a ConfigData as YAML to a temp dir so Config can resolve paths.
struct IntegrationFixture {
    TempDir output_dir;
    klspw::Config cfg;

    explicit IntegrationFixture(const std::string& project_name) : cfg{make_config(project_name, output_dir.path)} {}

    fs::path workspace_file() const { return cfg.workspace_file(); }

  private:
    static klspw::Config make_config(const std::string& project_name, const fs::path& out_dir) {
        const auto project_path = fs::weakly_canonical("test/fixtures/projects/" + project_name);
        klspw::require(fs::is_directory(project_path), "Fixture project not found: {}", project_path);

        const klspw::ConfigData data{
            .version = 1,
            .workspace_file = (out_dir / "workspace.json").string(),
            .build = klspw::BuildConfig{.command = {"gradle"}, .gradle_args = {"--quiet"}},
            .roots = {{.path = project_path.string()}},
        };

        const auto config_path = out_dir / "klspw.yaml";
        klspw::write_file(config_path, data.to_yaml());
        return klspw::Config::from_yaml(config_path);
    }
};

void check_workspace(const std::string& project_name) {
    IntegrationFixture fix(project_name);
    const auto ws_path = fix.workspace_file();
    klspw::GradleRunner runner;
    const klspw::Pipeline pipeline(std::move(fix.cfg), std::ref(runner));

    pipeline.write_workspace();

    const auto json = klspw::read_file(ws_path);
    CHECK(!json.empty());
    CHECK(json.contains("modules"));
    CHECK(json.contains("libraries"));
    CHECK(json.contains("kotlinSettings"));
}

} // namespace

TEST_CASE("integration: fixture projects produce non-empty workspace") {
    SUBCASE("simple") {
        check_workspace("simple");
    }
    SUBCASE("with-deps") {
        check_workspace("with-deps");
    }
    SUBCASE("multi") {
        check_workspace("multi");
    }
}

TEST_CASE("integration: multi-root project merges two Gradle roots") {
    TempDir const out;
    const auto base = fs::weakly_canonical("test/fixtures/projects/multi-root/src");
    const auto ws_path = out.path / "workspace.json";

    const klspw::ConfigData data{
        .version = 1,
        .workspace_file = ws_path.string(),
        .build = klspw::BuildConfig{.command = {"gradle"}, .gradle_args = {"--quiet"}},
        .roots = {{.path = (base / "core").string()}, {.path = (base / "service").string()}},
    };

    const auto config_path = out.path / "klspw.yaml";
    klspw::write_file(config_path, data.to_yaml());
    auto cfg = klspw::Config::from_yaml(config_path);

    klspw::GradleRunner runner;
    const klspw::Pipeline pipeline(std::move(cfg), std::ref(runner));

    pipeline.write_workspace();

    const auto json = klspw::read_file(ws_path);
    CHECK(!json.empty());
    CHECK(json.contains("modules"));
    CHECK(json.contains("kotlinSettings"));
}
