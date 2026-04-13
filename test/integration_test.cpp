#include <doctest/doctest.h>

#include "pipeline.hpp"
#include "test_common.hpp"

namespace {

fs::path fixtures() { return "test/fixtures/projects"; }

/// Load config from a fixture project's klspw.yaml and build the workspace.
klspw::WorkspaceData build_fixture(const std::string& project_name) {
  const auto config_path = fs::weakly_canonical(fixtures() / project_name / "klspw.yaml");
  auto cfg = klspw::Config::load_yaml_file(config_path);

  klspw::GradleRunner runner;
  const klspw::Pipeline pipeline(std::move(cfg), std::ref(runner));
  return pipeline.build_workspace();
}

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

bool has_mod_dep(const klspw::ModuleData& mod, std::string_view dep_name) {
  return std::ranges::any_of(mod.dependencies, [&](const auto& dep) {
    if (const auto* m = std::get_if<klspw::ModuleDep>(&dep)) {
      return m->name == dep_name;
    }
    return false;
  });
}

}  // namespace

TEST_CASE("integration: simple project") {
  const auto ws = build_fixture("simple");
  const auto& mod = find_module(ws, "simple");
  const auto& ks = find_kotlin_settings(ws, "simple");

  REQUIRE(ws.modules.size() == 1);
  CHECK(has_lib_dep(mod, "kotlin-stdlib"));
  CHECK(has_inherited_sdk(mod));
  CHECK(has_module_source(mod));
  REQUIRE(!mod.content_roots.empty());
  CHECK(!mod.content_roots[0].source_roots.empty());

  REQUIRE(ws.kotlin_settings.size() == 1);
  CHECK(ks.name == "Kotlin");
  CHECK(!ks.source_roots.empty());
  CHECK(ks.compiler_arguments.has_value());
  CHECK(ks.compiler_arguments->contains("jvmTarget"));
  CHECK(has_library(ws, "kotlin-stdlib"));
}

TEST_CASE("integration: with-deps project") {
  const auto ws = build_fixture("with-deps");
  const auto& mod = find_module(ws, "with-deps");
  const auto& ks = find_kotlin_settings(ws, "with-deps");

  REQUIRE(ws.modules.size() == 1);
  CHECK(has_lib_dep(mod, "okhttp"));
  CHECK(has_lib_dep(mod, "kotlin-stdlib"));
  CHECK(has_library(ws, "okhttp"));
  CHECK(has_library(ws, "kotlin-stdlib"));
  CHECK(ws.libraries.size() > 5);

  // Source jars attached
  const auto with_sources = std::ranges::count_if(ws.libraries, [](const auto& lib) { return has_sources_root(lib); });
  CHECK(with_sources > 0);

  // Pure kotlin folders exclude resources
  CHECK(!ks.pure_kotlin_source_folders.empty());
  for (const auto& folder : ks.pure_kotlin_source_folders) {
    CAPTURE(folder);
    CHECK_FALSE(folder.contains("resources"));
  }
}

TEST_CASE("integration: multi-project build") {
  const auto ws = build_fixture("multi");

  REQUIRE(ws.modules.size() == 2);
  CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "app"; }));
  CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "lib"; }));
  CHECK(ws.kotlin_settings.size() == 2);

  const auto stdlib_count =
      std::ranges::count_if(ws.libraries, [](const auto& lib) { return lib.name.starts_with("kotlin-stdlib"); });
  CHECK(stdlib_count == 1);

  // app depends on :lib — should be promoted to a module dependency
  const auto& app = find_module(ws, "app");
  CHECK(has_mod_dep(app, "lib"));
}

TEST_CASE("integration: multi-root project merges two Gradle roots") {
  const auto ws = build_fixture("multi-root");

  REQUIRE(ws.modules.size() == 2);
  CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "core"; }));
  CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "service"; }));
  CHECK(ws.kotlin_settings.size() == 2);

  const auto stdlib_count =
      std::ranges::count_if(ws.libraries, [](const auto& lib) { return lib.name.starts_with("kotlin-stdlib"); });
  CHECK(stdlib_count == 1);

  for (const auto& mod : ws.modules) {
    CAPTURE(mod.name);
    CHECK(has_inherited_sdk(mod));
    CHECK(has_module_source(mod));
  }
}

TEST_CASE("integration: write_workspace produces valid deserializable JSON") {
  const auto config_path = fs::weakly_canonical(fixtures() / "with-deps" / "klspw.yaml");
  auto cfg = klspw::Config::load_yaml_file(config_path);
  const auto ws_path = cfg.workspace_file();

  klspw::GradleRunner runner;
  const klspw::Pipeline pipeline(std::move(cfg), std::ref(runner));
  pipeline.write_workspace();

  const TempFile cleanup{ws_path};
  const auto ws = klspw::WorkspaceData::load_json_file(ws_path);

  CHECK(ws.modules.size() == 1);
  CHECK(ws.modules[0].name == "with-deps");
  CHECK(has_library(ws, "kotlin-stdlib"));
  CHECK(!ws.kotlin_settings.empty());

  for (const auto& lib : ws.libraries) {
    CAPTURE(lib.name);
    CHECK(lib.type.value_or("") == "java-imported");
  }
}

TEST_CASE("integration: discover multi-root and build workspace") {
  const auto multi_root = fs::weakly_canonical(fixtures().parent_path() / "projects" / "multi-root");
  const TempDir out_dir;
  const auto config_path = out_dir.path / "klspw.yaml";

  // Discover roots, set the same build command as the hand-written fixture config.
  klspw::StarterConfig::discover({multi_root.string()})
      .set_build("gradle --quiet")
      .set_config_path(config_path)
      .save_yaml_file();

  auto cfg = klspw::Config::load_yaml_file(config_path);

  klspw::GradleRunner runner;
  const klspw::Pipeline pipeline(std::move(cfg), std::ref(runner));
  const auto ws = pipeline.build_workspace();

  REQUIRE(ws.modules.size() == 2);
  CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "core"; }));
  CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "service"; }));
  CHECK(ws.kotlin_settings.size() == 2);
}

TEST_CASE("integration: kmp multi-module project") {
  const auto ws = build_fixture("kmp");

  // Two KMP subprojects: core and app (root project has no source sets)
  REQUIRE(ws.modules.size() == 2);
  CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "core"; }));
  CHECK(std::ranges::any_of(ws.modules, [](const auto& m) { return m.name == "app"; }));

  const auto& core = find_module(ws, "core");
  const auto& app = find_module(ws, "app");

  // Both modules should have source roots including commonMain and jvmMain
  const auto has_source_containing = [](const klspw::ModuleData& mod, std::string_view fragment) {
    return std::ranges::any_of(mod.content_roots, [&](const auto& cr) {
      return std::ranges::any_of(cr.source_roots, [&](const auto& sr) { return sr.path.contains(fragment); });
    });
  };
  CHECK(has_source_containing(core, "commonMain"));
  CHECK(has_source_containing(core, "jvmMain"));
  CHECK(has_source_containing(app, "commonMain"));
  CHECK(has_source_containing(app, "jvmMain"));

  // External library dependencies
  CHECK(has_library(ws, "kotlin-stdlib"));
  CHECK(has_lib_dep(core, "kotlinx-coroutines-core"));
  CHECK(has_lib_dep(app, "okio"));

  // app depends on :core — should be a module dependency, not a library dependency.
  CHECK(has_mod_dep(app, "core"));
  CHECK_FALSE(has_lib_dep(app, "core"));

  // KotlinSettings should exist for both modules
  CHECK(ws.kotlin_settings.size() == 2);
}
