#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "pipeline.hpp"
#include "test_common.hpp"

namespace {

// --- Workspace cache: run Gradle once per project, reuse across subcases ---

/// Run the pipeline for a single-root fixture project. Cached per project name.
const klspw::WorkspaceData& cached_workspace(const std::string& project_name) {
    static std::unordered_map<std::string, klspw::WorkspaceData> cache;
    if (auto it = cache.find(project_name); it != cache.end()) {
        return it->second;
    }

    const auto project_path = fs::weakly_canonical("test/fixtures/projects/" + project_name);
    klspw::require(fs::is_directory(project_path), "Fixture project not found: {}", project_path);

    const TempDir tmp;
    const klspw::ConfigData data{
        .version = 1,
        .workspace_file = (tmp.path / "workspace.json").string(),
        .build = klspw::BuildConfig{.command = {"gradle"}, .gradle_args = {"--quiet"}},
        .roots = {{.path = project_path.string()}},
    };
    const auto config_path = tmp.path / "klspw.yaml";
    klspw::write_file(config_path, data.to_yaml());
    auto cfg = klspw::Config::from_yaml(config_path);

    klspw::GradleRunner runner;
    const klspw::Pipeline pipeline(std::move(cfg), std::ref(runner));
    auto [it, _] = cache.emplace(project_name, pipeline.build_workspace());
    return it->second;
}

/// Run the pipeline for the multi-root fixture. Cached.
const klspw::WorkspaceData& cached_multi_root_workspace() {
    static std::optional<klspw::WorkspaceData> cache;
    if (cache) {
        return *cache;
    }

    const auto base = fs::weakly_canonical("test/fixtures/projects/multi-root/src");
    const TempDir tmp;
    const klspw::ConfigData data{
        .version = 1,
        .workspace_file = (tmp.path / "workspace.json").string(),
        .build = klspw::BuildConfig{.command = {"gradle"}, .gradle_args = {"--quiet"}},
        .roots = {{.path = (base / "core").string()}, {.path = (base / "service").string()}},
    };
    const auto config_path = tmp.path / "klspw.yaml";
    klspw::write_file(config_path, data.to_yaml());
    auto cfg = klspw::Config::from_yaml(config_path);

    klspw::GradleRunner runner;
    const klspw::Pipeline pipeline(std::move(cfg), std::ref(runner));
    cache.emplace(pipeline.build_workspace());
    return *cache;
}

// --- Helpers ---

const klspw::ModuleData& find_module(const klspw::WorkspaceData& ws, std::string_view name) {
    const auto it = std::ranges::find(ws.modules, name, &klspw::ModuleData::name);
    REQUIRE_MESSAGE(it != ws.modules.end(), "Module not found: ", name);
    return *it;
}

const klspw::KotlinSettingsData& find_kotlin_settings(const klspw::WorkspaceData& ws, std::string_view module_name) {
    const auto it = std::ranges::find(ws.kotlin_settings, module_name, &klspw::KotlinSettingsData::module);
    REQUIRE_MESSAGE(it != ws.kotlin_settings.end(), "KotlinSettings not found for module: ", module_name);
    return *it;
}

bool has_lib_dep(const klspw::ModuleData& mod, std::string_view name_prefix) {
    return std::ranges::any_of(mod.dependencies, [&](const auto& dep) {
        if (const auto* lib = std::get_if<klspw::LibraryDep>(&dep)) {
            return lib->name.starts_with(name_prefix);
        }
        return false;
    });
}

bool has_library(const klspw::WorkspaceData& ws, std::string_view name_prefix) {
    return std::ranges::any_of(ws.libraries, [&](const auto& lib) { return lib.name.starts_with(name_prefix); });
}

bool has_sources_root(const klspw::LibraryData& lib) {
    return std::ranges::any_of(lib.roots, [](const auto& r) { return r.type == "SOURCES"; });
}

bool has_inherited_sdk(const klspw::ModuleData& mod) {
    return std::ranges::any_of(mod.dependencies,
        [](const auto& d) { return std::holds_alternative<klspw::InheritedSdk>(d); });
}

bool has_module_source(const klspw::ModuleData& mod) {
    return std::ranges::any_of(mod.dependencies,
        [](const auto& d) { return std::holds_alternative<klspw::ModuleSource>(d); });
}

} // namespace

// --- simple: single module, no external deps ---

TEST_CASE("integration: simple project") {
    const auto& ws = cached_workspace("simple");

    SUBCASE("produces one module named 'simple'") {
        REQUIRE(ws.modules.size() == 1);
        CHECK(ws.modules[0].name == "simple");
    }

    SUBCASE("module has kotlin-stdlib dependency") {
        const auto& mod = find_module(ws, "simple");
        CHECK(has_lib_dep(mod, "kotlin-stdlib"));
    }

    SUBCASE("module has sentinel dependencies") {
        const auto& mod = find_module(ws, "simple");
        CHECK(has_inherited_sdk(mod));
        CHECK(has_module_source(mod));
    }

    SUBCASE("module has content roots with source dirs") {
        const auto& mod = find_module(ws, "simple");
        REQUIRE(!mod.content_roots.empty());
        CHECK(!mod.content_roots[0].source_roots.empty());
    }

    SUBCASE("has kotlin settings for 'simple'") {
        REQUIRE(ws.kotlin_settings.size() == 1);
        const auto& ks = find_kotlin_settings(ws, "simple");
        CHECK(ks.name == "Kotlin");
        CHECK(!ks.source_roots.empty());
        CHECK(ks.compiler_arguments.has_value());
        CHECK(ks.compiler_arguments->contains("jvmTarget"));
    }

    SUBCASE("libraries include kotlin-stdlib") { CHECK(has_library(ws, "kotlin-stdlib")); }
}

// --- with-deps: guava + test deps ---

TEST_CASE("integration: with-deps project") {
    const auto& ws = cached_workspace("with-deps");

    SUBCASE("produces one module named 'with-deps'") {
        REQUIRE(ws.modules.size() == 1);
        CHECK(ws.modules[0].name == "with-deps");
    }

    SUBCASE("module has guava and kotlin-stdlib dependencies") {
        const auto& mod = find_module(ws, "with-deps");
        CHECK(has_lib_dep(mod, "guava"));
        CHECK(has_lib_dep(mod, "kotlin-stdlib"));
    }

    SUBCASE("libraries include guava and kotlin-stdlib") {
        CHECK(has_library(ws, "guava"));
        CHECK(has_library(ws, "kotlin-stdlib"));
    }

    SUBCASE("libraries have more than just kotlin-stdlib") { CHECK(ws.libraries.size() > 5); }

    SUBCASE("source jars attached to libraries") {
        const auto with_sources =
            std::ranges::count_if(ws.libraries, [](const auto& lib) { return has_sources_root(lib); });
        CHECK(with_sources > 0);
    }

    SUBCASE("kotlin settings has pure kotlin source folders") {
        const auto& ks = find_kotlin_settings(ws, "with-deps");
        CHECK(!ks.pure_kotlin_source_folders.empty());
        for (const auto& folder : ks.pure_kotlin_source_folders) {
            CHECK_FALSE(folder.contains("resources"));
        }
    }
}

// --- multi: multi-project build (app + lib subprojects) ---

TEST_CASE("integration: multi-project build") {
    const auto& ws = cached_workspace("multi");

    SUBCASE("produces modules for subprojects") {
        CHECK(ws.modules.size() >= 2);
        CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "app"; }));
        CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "lib"; }));
    }

    SUBCASE("kotlin settings for each module with source sets") { CHECK(ws.kotlin_settings.size() >= 2); }

    SUBCASE("libraries are deduplicated across subprojects") {
        const auto stdlib_count =
            std::ranges::count_if(ws.libraries, [](const auto& lib) { return lib.name.starts_with("kotlin-stdlib"); });
        CHECK(stdlib_count <= 1);
    }
}

// --- multi-root: two separate Gradle roots merged ---

TEST_CASE("integration: multi-root project merges two Gradle roots") {
    const auto& ws = cached_multi_root_workspace();

    SUBCASE("produces two modules") {
        REQUIRE(ws.modules.size() == 2);
        CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "core"; }));
        CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "service"; }));
    }

    SUBCASE("produces two kotlin settings") { CHECK(ws.kotlin_settings.size() == 2); }

    SUBCASE("libraries are deduplicated across roots") {
        const auto stdlib_count =
            std::ranges::count_if(ws.libraries, [](const auto& lib) { return lib.name.starts_with("kotlin-stdlib"); });
        CHECK(stdlib_count == 1);
    }

    SUBCASE("each module has sentinel dependencies") {
        for (const auto& mod : ws.modules) {
            CHECK(has_inherited_sdk(mod));
            CHECK(has_module_source(mod));
        }
    }
}

// --- write round-trip: workspace.json can be deserialized back ---

TEST_CASE("integration: write_workspace produces valid deserializable JSON") {
    // This test needs its own pipeline run since it tests write + read-back.
    const auto project_path = fs::weakly_canonical("test/fixtures/projects/with-deps");
    const TempDir tmp;
    const auto ws_path = tmp.path / "workspace.json";

    const klspw::ConfigData data{
        .version = 1,
        .workspace_file = ws_path.string(),
        .build = klspw::BuildConfig{.command = {"gradle"}, .gradle_args = {"--quiet"}},
        .roots = {{.path = project_path.string()}},
    };
    const auto config_path = tmp.path / "klspw.yaml";
    klspw::write_file(config_path, data.to_yaml());
    auto cfg = klspw::Config::from_yaml(config_path);

    klspw::GradleRunner runner;
    const klspw::Pipeline pipeline(std::move(cfg), std::ref(runner));
    pipeline.write_workspace();

    const auto json = klspw::read_file(ws_path);
    klspw::WorkspaceData ws;
    const auto ec = glz::read<klspw::ws_read_opts>(ws, json);
    REQUIRE_MESSAGE(!ec, "Failed to parse workspace JSON");

    CHECK(ws.modules.size() == 1);
    CHECK(ws.modules[0].name == "with-deps");
    CHECK(has_library(ws, "kotlin-stdlib"));
    CHECK(!ws.kotlin_settings.empty());

    for (const auto& lib : ws.libraries) {
        CAPTURE(lib.name);
        CHECK(lib.type.value_or("") == "java-imported");
    }
}
