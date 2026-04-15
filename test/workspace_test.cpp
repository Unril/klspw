#include "workspace.hpp"

#include <string>

#include <doctest/doctest.h>

#include "files.hpp"
#include "gradle.hpp"
#include "test_common.hpp"

namespace {

template <typename T>
std::string to_json(const T& val) {
  auto result = glz::write_json(val);
  REQUIRE(result.has_value());
  return result.value();
}

template <typename T>
T from_json(const std::string& json_str) {
  auto result = glz::read_json<T>(json_str);
  REQUIRE(result.has_value());
  return result.value();
}

void assert_round_trip(const std::string& path) {
  const auto ws = from_json<klspw::WorkspaceData>(klspw::read_file(path));
  const auto json1 = to_json(ws);
  REQUIRE_FALSE(json1.empty());
  const auto json2 = to_json(from_json<klspw::WorkspaceData>(json1));
  CHECK(json1 == json2);
}

}  // namespace

TEST_CASE("round-trip root workspace") { assert_round_trip("test/fixtures/example_root_workspace.json"); }

TEST_CASE("round-trip proj workspace") { assert_round_trip("test/fixtures/example_proj_workspace.json"); }

// --- WorkspaceData::to_json / from_json ---

TEST_CASE("WorkspaceData::to_json and from_json round-trip") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "app"});
  ws.libraries.push_back({.name = "guava", .roots = {{.path = "/cache/guava.jar"}}});
  ws.kotlin_settings.push_back({.name = "Kotlin", .module = "app"});

  const auto json = ws.to_json();
  const auto parsed = klspw::WorkspaceData::from_json(json);

  REQUIRE(parsed.modules.size() == 1);
  CHECK(parsed.modules[0].name == "app");
  REQUIRE(parsed.libraries.size() == 1);
  CHECK(parsed.libraries[0].name == "guava");
  REQUIRE(parsed.kotlin_settings.size() == 1);
  CHECK(parsed.kotlin_settings[0].module == "app");
}

TEST_CASE("WorkspaceData::from_json throws on malformed JSON") {
  CHECK_THROWS_AS(klspw::WorkspaceData::from_json("{not valid}"), std::runtime_error);
}

// --- WorkspaceData::save_json_file / load_json_file ---

TEST_CASE("WorkspaceData save and load round-trip through file") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "mod"});
  ws.libraries.push_back({.name = "lib", .roots = {{.path = "/lib.jar"}}});

  const TempDir dir;
  const auto path = dir.path / "workspace.json";
  ws.save_json_file(path);

  const auto loaded = klspw::WorkspaceData::load_json_file(path);
  CHECK(loaded.modules.size() == 1);
  CHECK(loaded.modules[0].name == "mod");
  CHECK(loaded.libraries.size() == 1);
}

TEST_CASE("root workspace structure") {
  const auto ws = from_json<klspw::WorkspaceData>(klspw::read_file("test/fixtures/example_root_workspace.json"));

  CHECK_FALSE(ws.modules.empty());
  CHECK_FALSE(ws.libraries.empty());
  CHECK(ws.sdks.empty());
  CHECK_FALSE(ws.kotlin_settings.empty());
  CHECK(ws.java_settings.empty());

  SUBCASE("module") {
    const auto& m = ws.modules[0];
    CHECK(m.name == "MyKotlinService-1.0");
    CHECK(m.type == "JAVA_MODULE");
    CHECK_FALSE(m.dependencies.empty());
    CHECK_FALSE(m.content_roots.empty());
  }

  SUBCASE("dependencies contain library and inheritedSdk") {
    const auto& deps = ws.modules[0].dependencies;
    CHECK(std::ranges::any_of(deps, [](const auto& d) { return std::holds_alternative<klspw::LibraryDep>(d); }));
    CHECK(std::ranges::any_of(deps, [](const auto& d) { return std::holds_alternative<klspw::InheritedSdk>(d); }));
    CHECK(std::ranges::any_of(deps, [](const auto& d) { return std::holds_alternative<klspw::ModuleSource>(d); }));
  }

  SUBCASE("library") {
    const auto& lib = ws.libraries[0];
    CHECK_FALSE(lib.name.empty());
    CHECK_FALSE(lib.roots.empty());
    CHECK(lib.type.has_value());
  }

  SUBCASE("kotlin settings") {
    const auto& ks = ws.kotlin_settings[0];
    CHECK(ks.name == "Kotlin");
    CHECK_FALSE(ks.module.empty());
    CHECK(ks.version == 5);
    CHECK(ks.kind == "default");
    CHECK(ks.compiler_arguments.has_value());
    CHECK(ks.compiler_arguments->contains("jvmTarget"));
  }
}

TEST_CASE("proj workspace structure") {
  const auto ws = from_json<klspw::WorkspaceData>(klspw::read_file("test/fixtures/example_proj_workspace.json"));

  CHECK_FALSE(ws.modules.empty());
  CHECK_FALSE(ws.libraries.empty());
  CHECK(ws.sdks.empty());
  CHECK(ws.kotlin_settings.empty());

  SUBCASE("module has type") { CHECK(ws.modules[0].type == "JAVA_MODULE"); }

  SUBCASE("library has level") { CHECK(ws.libraries[0].level == "project"); }

  SUBCASE("library root has inclusion_options") {
    using std::ranges::any_of;
    const bool found = any_of(ws.libraries, [](const auto& lib) {
      return any_of(lib.roots, [](const auto& root) { return root.inclusion_options == "root_itself"; });
    });
    CHECK(found);
  }
}

TEST_CASE("DependencyScope round-trips") {
  for (auto scope : {klspw::DependencyScope::compile, klspw::DependencyScope::test, klspw::DependencyScope::runtime,
                     klspw::DependencyScope::provided}) {
    CAPTURE(scope);
    CHECK(from_json<klspw::DependencyScope>(to_json(scope)) == scope);
  }
}

TEST_CASE("DependencyData variant round-trips") {
  SUBCASE("library") {
    const klspw::DependencyData d = klspw::LibraryDep{.name = "guava"};
    const auto json_str = to_json(d);
    CHECK(json_str.contains("\"type\":\"library\""));
    CHECK(std::get<klspw::LibraryDep>(from_json<klspw::DependencyData>(json_str)).name == "guava");
  }

  SUBCASE("inheritedSdk") {
    const auto json_str = to_json(klspw::DependencyData{klspw::InheritedSdk{}});
    CHECK(json_str.contains("\"type\":\"inheritedSdk\""));
    CHECK(std::holds_alternative<klspw::InheritedSdk>(from_json<klspw::DependencyData>(json_str)));
  }

  SUBCASE("moduleSource") {
    const auto json_str = to_json(klspw::DependencyData{klspw::ModuleSource{}});
    CHECK(json_str.contains("\"type\":\"moduleSource\""));
    CHECK(std::holds_alternative<klspw::ModuleSource>(from_json<klspw::DependencyData>(json_str)));
  }
}

TEST_CASE("ModuleDep round-trips") {
  const klspw::DependencyData d =
      klspw::ModuleDep{.name = "core", .scope = klspw::DependencyScope::test, .isExported = true, .isTestJar = true};
  const auto json_str = to_json(d);
  CHECK(json_str.contains("\"type\":\"module\""));

  const auto parsed = from_json<klspw::DependencyData>(json_str);
  const auto& m = std::get<klspw::ModuleDep>(parsed);
  CHECK(m.name == "core");
  CHECK(m.scope == klspw::DependencyScope::test);
  CHECK(m.isExported);
  CHECK(m.isTestJar);
}

TEST_CASE("SdkDep round-trips") {
  const klspw::DependencyData d = klspw::SdkDep{.name = "JDK21", .kind = "JavaSDK"};
  CHECK(std::get<klspw::SdkDep>(from_json<klspw::DependencyData>(to_json(d))).name == "JDK21");
}

TEST_CASE("XmlElement round-trips") {
  const klspw::XmlElement elem{
      .tag = "root", .attributes = {{"key", "val"}}, .children = {{.tag = "child"}}, .text = "hello"};
  const auto parsed = from_json<klspw::XmlElement>(to_json(elem));
  CHECK(parsed.tag == "root");
  CHECK(parsed.attributes.at("key") == "val");
  CHECK(parsed.text.value() == "hello");
  CHECK(parsed.children[0].tag == "child");
}

TEST_CASE("ContentRootData round-trips") {
  const klspw::ContentRootData cr{
      .path = "/src",
      .source_roots = {{.path = "/src/main", .type = "java-source"}},
      .excluded_patterns = {"*.bak", "tmp/"},
      .excluded_urls = {"file:///excluded"},
  };
  const auto parsed = from_json<klspw::ContentRootData>(to_json(cr));
  CHECK(parsed.excluded_patterns.size() == 2);
  CHECK(parsed.excluded_urls[0] == "file:///excluded");
}

TEST_CASE("SdkData round-trips") {
  const klspw::SdkData sdk{
      .name = "JDK21",
      .type = "JavaSDK",
      .version = "21",
      .home_path = "/usr/lib/jvm/java-21",
      .roots = std::vector<klspw::SdkRootData>{{.url = "jar:///rt.jar!/", .type = "classPath"}},
      .additional_data = "extra",
  };
  const auto parsed = from_json<klspw::SdkData>(to_json(sdk));
  CHECK(parsed.version.value() == "21");
  CHECK(parsed.roots->at(0).url == "jar:///rt.jar!/");
}

TEST_CASE("JavaSettingsData round-trips") {
  const klspw::JavaSettingsData js{
      .module = "mymod",
      .inherited_compiler_output = false,
      .exclude_output = false,
      .compiler_output = "/out",
      .compiler_output_for_tests = "/test-out",
      .language_level_id = "JDK_21",
      .manifest_attributes = {{"Main-Class", "com.example.Main"}},
  };
  const auto parsed = from_json<klspw::JavaSettingsData>(to_json(js));
  CHECK(parsed.module == "mymod");
  CHECK_FALSE(parsed.inherited_compiler_output);
  CHECK(parsed.compiler_output.value() == "/out");
  CHECK(parsed.language_level_id.value() == "JDK_21");
  CHECK(parsed.manifest_attributes.at("Main-Class") == "com.example.Main");
}

TEST_CASE("LibraryData with properties round-trips") {
  const klspw::LibraryData lib{
      .name = "mylib",
      .type = "java-imported",
      .roots = {{.path = "/lib.jar"}},
      .excluded_roots = {"/excluded"},
      .properties = klspw::XmlElement{.tag = "properties"},
  };
  const auto parsed = from_json<klspw::LibraryData>(to_json(lib));
  CHECK(parsed.excluded_roots[0] == "/excluded");
  CHECK(parsed.properties->tag == "properties");
}

// --- promote_module_deps ---

TEST_CASE("promote_module_deps converts library dep to module dep") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib"});
  ws.modules.push_back(
      {.name = "App1",
       .dependencies = {klspw::LibraryDep{.name = "CoreLib-1.0", .scope = klspw::DependencyScope::compile}}});
  ws.modules.push_back(
      {.name = "App2",
       .dependencies = {klspw::LibraryDep{.name = "CoreLib-1.0", .scope = klspw::DependencyScope::test}}});
  ws.libraries.push_back({.name = "CoreLib-1.0", .roots = {{.path = "/lib/CoreLib-1.0.jar"}}});

  ws.promote_module_deps();

  // Library dep promoted to module dep in both consuming modules.
  const auto& dep1 = ws.modules[1].dependencies[0];
  REQUIRE(std::holds_alternative<klspw::ModuleDep>(dep1));
  CHECK(std::get<klspw::ModuleDep>(dep1).name == "CoreLib");
  CHECK(std::get<klspw::ModuleDep>(dep1).scope == klspw::DependencyScope::compile);

  const auto& dep2 = ws.modules[2].dependencies[0];
  REQUIRE(std::holds_alternative<klspw::ModuleDep>(dep2));
  CHECK(std::get<klspw::ModuleDep>(dep2).name == "CoreLib");
  CHECK(std::get<klspw::ModuleDep>(dep2).scope == klspw::DependencyScope::test);

  // Library removed.
  CHECK(ws.libraries.empty());
}

TEST_CASE("promote_module_deps leaves unrelated libraries untouched") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "App", .dependencies = {klspw::LibraryDep{.name = "guava-33.0"}}});
  ws.libraries.push_back({.name = "guava-33.0", .roots = {{.path = "/lib/guava-33.0.jar"}}});

  ws.promote_module_deps();

  REQUIRE(std::holds_alternative<klspw::LibraryDep>(ws.modules[0].dependencies[0]));
  CHECK(ws.libraries.size() == 1);
}

TEST_CASE("promote_module_deps preserves non-library deps") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib"});
  ws.modules.push_back({.name = "App",
                        .dependencies = {
                            klspw::InheritedSdk{},
                            klspw::ModuleSource{},
                            klspw::LibraryDep{.name = "CoreLib-1.0"},
                        }});
  ws.libraries.push_back({.name = "CoreLib-1.0", .roots = {{.path = "/lib/CoreLib-1.0.jar"}}});

  ws.promote_module_deps();

  CHECK(std::holds_alternative<klspw::InheritedSdk>(ws.modules[1].dependencies[0]));
  CHECK(std::holds_alternative<klspw::ModuleSource>(ws.modules[1].dependencies[1]));
  CHECK(std::holds_alternative<klspw::ModuleDep>(ws.modules[1].dependencies[2]));
}

TEST_CASE("promote_module_deps handles multi-dash module names") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "My-Cool-Lib"});
  ws.modules.push_back({.name = "App", .dependencies = {klspw::LibraryDep{.name = "My-Cool-Lib-2.3.1"}}});
  ws.libraries.push_back({.name = "My-Cool-Lib-2.3.1", .roots = {{.path = "/lib.jar"}}});

  ws.promote_module_deps();

  const auto& dep = ws.modules[1].dependencies[0];
  REQUIRE(std::holds_alternative<klspw::ModuleDep>(dep));
  CHECK(std::get<klspw::ModuleDep>(dep).name == "My-Cool-Lib");
  CHECK(ws.libraries.empty());
}

TEST_CASE("promote_module_deps no-op when no modules") {
  klspw::WorkspaceData ws;
  ws.libraries.push_back({.name = "foo-1.0", .roots = {{.path = "/foo.jar"}}});

  ws.promote_module_deps();

  CHECK(ws.libraries.size() == 1);
}

TEST_CASE("promote_module_deps skips self-dependency") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib", .dependencies = {klspw::LibraryDep{.name = "CoreLib-1.0"}}});
  ws.libraries.push_back({.name = "CoreLib-1.0", .roots = {{.path = "/lib.jar"}}});

  ws.promote_module_deps();

  // Should remain a library dep, not promote to self-referencing module dep.
  CHECK(std::holds_alternative<klspw::LibraryDep>(ws.modules[0].dependencies[0]));
}

TEST_CASE("promote_module_deps keeps library when self-referencing module still needs it") {
  // Regression: module "public" has jetified-public-release-api as a self-referencing lib dep.
  // Module "impl" also has it. After promotion, impl gets a module dep on public,
  // but public keeps its lib dep — the library must NOT be removed from the list.
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "public",
                        .dependencies = {klspw::LibraryDep{.name = "jetified-public-release-api"},
                                         klspw::LibraryDep{.name = "jetified-public-api"}}});
  ws.modules.push_back(
      {.name = "impl",
       .dependencies = {klspw::LibraryDep{.name = "jetified-public-release-api"},
                        klspw::LibraryDep{.name = "jetified-public-api"}, klspw::LibraryDep{.name = "guava-33.0"}}});
  ws.libraries.push_back({.name = "jetified-public-release-api", .roots = {{.path = "/pub-release.jar"}}});
  ws.libraries.push_back({.name = "jetified-public-api", .roots = {{.path = "/pub.jar"}}});
  ws.libraries.push_back({.name = "guava-33.0", .roots = {{.path = "/guava.jar"}}});

  const auto promoted = ws.promote_module_deps();
  CHECK(promoted == 2);

  // public: self-referencing lib deps remain (not promoted to self module dep).
  const auto& pub_deps = ws.modules[0].dependencies;
  CHECK(std::holds_alternative<klspw::LibraryDep>(pub_deps[0]));
  CHECK(std::holds_alternative<klspw::LibraryDep>(pub_deps[1]));

  // impl: promoted to module deps on public, guava untouched.
  const auto& impl_deps = ws.modules[1].dependencies;
  const auto impl_mod_deps = ws.modules[1].dep_count<klspw::ModuleDep>();
  const auto impl_lib_deps = ws.modules[1].dep_count<klspw::LibraryDep>();
  CHECK(impl_mod_deps == 2);
  CHECK(impl_lib_deps == 1);
  CHECK(std::ranges::any_of(impl_deps, [](const auto& d) {
    const auto* m = std::get_if<klspw::ModuleDep>(&d);
    return m && m->name == "public";
  }));
  CHECK(std::ranges::any_of(impl_deps, [](const auto& d) {
    const auto* l = std::get_if<klspw::LibraryDep>(&d);
    return l && l->name == "guava-33.0";
  }));

  // Libraries: jetified-public-* kept (still referenced by public), guava kept.
  CHECK(ws.libraries.size() == 3);
  CHECK(std::ranges::any_of(ws.libraries, [](const auto& l) { return l.name == "jetified-public-release-api"; }));
  CHECK(std::ranges::any_of(ws.libraries, [](const auto& l) { return l.name == "jetified-public-api"; }));
  CHECK(std::ranges::any_of(ws.libraries, [](const auto& l) { return l.name == "guava-33.0"; }));
}

TEST_CASE("promote_module_deps preserves isExported") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib"});
  ws.modules.push_back({.name = "App", .dependencies = {klspw::LibraryDep{.name = "CoreLib-1.0", .isExported = true}}});
  ws.libraries.push_back({.name = "CoreLib-1.0", .roots = {{.path = "/lib.jar"}}});

  ws.promote_module_deps();

  const auto& dep = ws.modules[1].dependencies[0];
  REQUIRE(std::holds_alternative<klspw::ModuleDep>(dep));
  CHECK(std::get<klspw::ModuleDep>(dep).isExported);
}

// --- module_names ---

TEST_CASE("module_names returns set of module names") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "App"});
  ws.modules.push_back({.name = "CoreLib"});

  const auto names = ws.module_names();

  CHECK(names.size() == 2);
  CHECK(names.contains("App"));
  CHECK(names.contains("CoreLib"));
}

TEST_CASE("module_names empty when no modules") {
  const klspw::WorkspaceData ws;
  CHECK(ws.module_names().empty());
}

// --- promote_module_deps with project_deps ---

TEST_CASE("promote_module_deps with project_deps gates indirect Maven matches") {
  // App depends on "com.example:CoreLib:1.0" — Maven module "CoreLib" matches module "CoreLib"
  // indirectly (prefix-dash). Without project_deps confirming the dep, it should be blocked.
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib"});
  ws.modules.push_back({.name = "App", .dependencies = {klspw::LibraryDep{.name = "com.example:CoreLib-android:1.0"}}});
  ws.libraries.push_back({.name = "com.example:CoreLib-android:1.0", .roots = {{.path = "/lib.jar"}}});

  // project_deps says App does NOT depend on CoreLib
  const klspw::string_map<klspw::string_set> project_deps = {{"App", {}}};
  ws.promote_module_deps(project_deps);

  // Should NOT be promoted — indirect Maven match blocked by project_deps
  CHECK(std::holds_alternative<klspw::LibraryDep>(ws.modules[1].dependencies[0]));
  CHECK(ws.libraries.size() == 1);
}

TEST_CASE("promote_module_deps with project_deps blocks when module absent from map") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib"});
  ws.modules.push_back({.name = "App", .dependencies = {klspw::LibraryDep{.name = "com.example:CoreLib-android:1.0"}}});
  ws.libraries.push_back({.name = "com.example:CoreLib-android:1.0", .roots = {{.path = "/lib.jar"}}});

  // App is not in project_deps at all — find() returns end()
  const klspw::string_map<klspw::string_set> project_deps = {{"Other", {"CoreLib"}}};
  ws.promote_module_deps(project_deps);

  CHECK(std::holds_alternative<klspw::LibraryDep>(ws.modules[1].dependencies[0]));
  CHECK(ws.libraries.size() == 1);
}

TEST_CASE("promote_module_deps with project_deps allows indirect Maven match when confirmed") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib"});
  ws.modules.push_back({.name = "App", .dependencies = {klspw::LibraryDep{.name = "com.example:CoreLib-android:1.0"}}});
  ws.libraries.push_back({.name = "com.example:CoreLib-android:1.0", .roots = {{.path = "/lib.jar"}}});

  // project_deps confirms App depends on CoreLib
  const klspw::string_map<klspw::string_set> project_deps = {{"App", {"CoreLib"}}};
  ws.promote_module_deps(project_deps);

  // Should be promoted — project_deps confirms the dependency
  const auto& dep = ws.modules[1].dependencies[0];
  REQUIRE(std::holds_alternative<klspw::ModuleDep>(dep));
  CHECK(std::get<klspw::ModuleDep>(dep).name == "CoreLib");
  CHECK(ws.libraries.empty());
}

TEST_CASE("promote_module_deps with project_deps allows exact Maven module match without gate") {
  // "com.example:CoreLib:1.0" — Maven module component "CoreLib" exactly matches target "CoreLib".
  // Exact match bypasses the project_deps gate.
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib"});
  ws.modules.push_back({.name = "App", .dependencies = {klspw::LibraryDep{.name = "com.example:CoreLib:1.0"}}});
  ws.libraries.push_back({.name = "com.example:CoreLib:1.0", .roots = {{.path = "/lib.jar"}}});

  // project_deps does NOT list CoreLib for App — but exact Maven match bypasses the gate
  const klspw::string_map<klspw::string_set> project_deps = {{"App", {}}};
  ws.promote_module_deps(project_deps);

  const auto& dep = ws.modules[1].dependencies[0];
  REQUIRE(std::holds_alternative<klspw::ModuleDep>(dep));
  CHECK(std::get<klspw::ModuleDep>(dep).name == "CoreLib");
}

TEST_CASE("promote_module_deps with project_deps always promotes non-Maven libraries") {
  // Library without ':' (local build output) — always promoted regardless of project_deps.
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib"});
  ws.modules.push_back({.name = "App", .dependencies = {klspw::LibraryDep{.name = "CoreLib-1.0"}}});
  ws.libraries.push_back({.name = "CoreLib-1.0", .roots = {{.path = "/lib.jar"}}});

  // project_deps is empty — but non-Maven libs skip the gate
  const klspw::string_map<klspw::string_set> project_deps = {{"App", {}}};
  ws.promote_module_deps(project_deps);

  const auto& dep = ws.modules[1].dependencies[0];
  REQUIRE(std::holds_alternative<klspw::ModuleDep>(dep));
  CHECK(std::get<klspw::ModuleDep>(dep).name == "CoreLib");
}

TEST_CASE("promote_module_deps returns promoted count") {
  klspw::WorkspaceData ws;
  ws.modules.push_back({.name = "CoreLib"});
  ws.modules.push_back({.name = "Utils"});
  ws.modules.push_back(
      {.name = "App",
       .dependencies = {klspw::LibraryDep{.name = "CoreLib-1.0"}, klspw::LibraryDep{.name = "Utils-2.0"},
                        klspw::LibraryDep{.name = "guava-33.0"}}});
  ws.libraries.push_back({.name = "CoreLib-1.0", .roots = {{.path = "/a.jar"}}});
  ws.libraries.push_back({.name = "Utils-2.0", .roots = {{.path = "/b.jar"}}});
  ws.libraries.push_back({.name = "guava-33.0", .roots = {{.path = "/c.jar"}}});

  const auto count = ws.promote_module_deps();

  CHECK(count == 2);
  CHECK(ws.libraries.size() == 1);  // only guava remains
}

// --- composite build (includeBuild) regression ---

TEST_CASE("promote_module_deps promotes composite build project deps via classpath_coordinates") {
  // Regression: composite builds (includeBuild with dependencySubstitution) produce
  // classes.jar on the classpath with no Maven coordinates. Without the init script fix,
  // the library gets named "classes" (the file stem) and can't be matched to a module.
  // After the fix, classpath_coordinates maps the jar to the project name, and
  // project_dependencies includes the composite build dep.
  //
  // Simulates: Android root depends on KMP root's "impl" module via composite build.
  // The init script now emits classpath_coordinates: {"/path/classes.jar": "impl"}
  // and project_dependencies: ["impl"].

  // Build two GradleBuildOutputs (one per root) and merge, like Pipeline does.
  const auto android_output = klspw::GradleBuildOutput::from_json(R"({
    "rootProject": "/workspace/AndroidApp",
    "projects": [{
      "projectPath": ":",
      "projectName": "AndroidApp",
      "projectDir": "/workspace/AndroidApp",
      "kind": "android",
      "plugins": [],
      "sourceSets": [{
        "name": "debug",
        "sourceRoots": ["/workspace/AndroidApp/src/main/kotlin"],
        "javaSourceRoots": [],
        "resourcesRoots": [],
        "classesDirs": [],
        "resourcesDir": null,
        "compileClasspath": [
          "/workspace/KMP/impl/build/intermediates/classes.jar",
          "/workspace/KMP/public/build/intermediates/classes.jar",
          "/cache/guava-33.0.jar"
        ],
        "runtimeClasspath": [],
        "classpathCoordinates": {
          "/workspace/KMP/impl/build/intermediates/classes.jar": "impl",
          "/workspace/KMP/public/build/intermediates/classes.jar": "public"
        },
        "compileClasspathConfigurationName": "debugCompileClasspath",
        "runtimeClasspathConfigurationName": "debugRuntimeClasspath"
      }],
      "projectDependencies": ["AndroidApp", "impl", "public"]
    }]
  })");

  const auto kmp_output = klspw::GradleBuildOutput::from_json(R"({
    "rootProject": "/workspace/KMP",
    "projects": [
      {
        "projectPath": ":impl",
        "projectName": "impl",
        "projectDir": "/workspace/KMP/impl",
        "kind": "kmp-android",
        "plugins": [],
        "sourceSets": [{
          "name": "debug",
          "sourceRoots": ["/workspace/KMP/impl/src/commonMain/kotlin", "/workspace/KMP/impl/src/androidMain/kotlin"],
          "javaSourceRoots": [],
          "resourcesRoots": [],
          "classesDirs": [],
          "resourcesDir": null,
          "compileClasspath": [],
          "runtimeClasspath": [],
          "compileClasspathConfigurationName": "",
          "runtimeClasspathConfigurationName": ""
        }]
      },
      {
        "projectPath": ":public",
        "projectName": "public",
        "projectDir": "/workspace/KMP/public",
        "kind": "kmp-android",
        "plugins": [],
        "sourceSets": [{
          "name": "debug",
          "sourceRoots": ["/workspace/KMP/public/src/commonMain/kotlin"],
          "javaSourceRoots": [],
          "resourcesRoots": [],
          "classesDirs": [],
          "resourcesDir": null,
          "compileClasspath": [],
          "runtimeClasspath": [],
          "compileClasspathConfigurationName": "",
          "runtimeClasspathConfigurationName": ""
        }]
      }
    ]
  })");

  constexpr klspw::GenerationOptions opts{.include_tests = false, .remove_missing_paths = false};
  auto ws = android_output.to_workspace("17", opts);
  ws.merge(kmp_output.to_workspace("17", opts));

  // Collect project deps from both roots and merge.
  auto all_project_deps = android_output.collect_project_deps();
  for (auto&& [k, v] : kmp_output.collect_project_deps()) {
    all_project_deps[k].merge(std::move(v));
  }

  // Before promotion: AndroidApp should have library deps named "impl", "public", "guava-33.0".
  const auto& android_mod = ws.modules[0];
  CHECK(android_mod.name == "AndroidApp");
  const auto lib_deps_before = android_mod.dep_count<klspw::LibraryDep>();
  CHECK(lib_deps_before == 3);

  // Promote.
  const auto promoted = ws.promote_module_deps(all_project_deps);

  // After promotion: "impl" and "public" should be module deps, "guava-33.0" stays as library.
  CHECK(promoted == 2);

  const auto& deps = ws.modules[0].dependencies;
  const auto mod_dep_count = ws.modules[0].dep_count<klspw::ModuleDep>();
  const auto lib_dep_count = ws.modules[0].dep_count<klspw::LibraryDep>();
  CHECK(mod_dep_count == 2);
  CHECK(lib_dep_count == 1);

  CHECK(std::ranges::any_of(deps, [](const auto& d) {
    const auto* m = std::get_if<klspw::ModuleDep>(&d);
    return m && m->name == "impl";
  }));
  CHECK(std::ranges::any_of(deps, [](const auto& d) {
    const auto* m = std::get_if<klspw::ModuleDep>(&d);
    return m && m->name == "public";
  }));
  CHECK(std::ranges::any_of(deps, [](const auto& d) {
    const auto* l = std::get_if<klspw::LibraryDep>(&d);
    return l && l->name == "guava-33.0";
  }));
}

TEST_CASE("Android databinding generated sources appear in module content roots") {
  // Regression: databinding generates binding classes in
  // build/generated/data_binding_base_class_source_out/{variant}/out/
  // which must be included as source roots for kotlin-lsp to resolve them.
  const auto output = klspw::GradleBuildOutput::from_json(R"({
    "rootProject": "/workspace/App",
    "projects": [{
      "projectPath": ":",
      "projectName": "App",
      "projectDir": "/workspace/App",
      "kind": "android",
      "plugins": [],
      "sourceSets": [{
        "name": "debug",
        "sourceRoots": [
          "/workspace/App/build/generated/data_binding_base_class_source_out/debug/out",
          "/workspace/App/build/generated/ksp/debug",
          "/workspace/App/src/debug/kotlin",
          "/workspace/App/src/main/kotlin"
        ],
        "javaSourceRoots": [],
        "resourcesRoots": ["/workspace/App/src/main/res"],
        "classesDirs": [],
        "resourcesDir": null,
        "compileClasspath": [],
        "runtimeClasspath": [],
        "compileClasspathConfigurationName": "",
        "runtimeClasspathConfigurationName": ""
      }]
    }]
  })");

  constexpr klspw::GenerationOptions opts{.include_tests = false, .remove_missing_paths = false};
  const auto ws = output.to_workspace("17", opts);

  REQUIRE(ws.modules.size() == 1);
  REQUIRE_FALSE(ws.modules[0].content_roots.empty());
  const auto& roots = ws.modules[0].content_roots[0].source_roots;

  // The databinding dir should be present as a java-source root.
  const auto has_databinding = std::ranges::any_of(roots, [](const auto& r) {
    return r.path.contains("data_binding_base_class_source_out") && r.type == "java-source";
  });
  CHECK(has_databinding);
}

TEST_CASE("SourceSet::jar_library_name uses classpath_coordinates for project names") {
  // Verify that classpath_coordinates with bare project names (no ':') work correctly.
  const auto output = klspw::GradleBuildOutput::from_json(R"({
    "rootProject": "/tmp/proj",
    "projects": [{
      "projectPath": ":",
      "projectDir": "/tmp/proj",
      "kind": "android",
      "plugins": [],
      "sourceSets": [{
        "name": "debug",
        "sourceRoots": [],
        "javaSourceRoots": [],
        "resourcesRoots": [],
        "classesDirs": [],
        "resourcesDir": null,
        "compileClasspath": ["/other/build/intermediates/classes.jar"],
        "runtimeClasspath": [],
        "classpathCoordinates": {
          "/other/build/intermediates/classes.jar": "impl"
        },
        "compileClasspathConfigurationName": "",
        "runtimeClasspathConfigurationName": ""
      }]
    }]
  })");

  const auto& ss = output.projects[0].source_sets[0];
  CHECK(ss.jar_library_name("/other/build/intermediates/classes.jar") == "impl");
}

// --- describe ---

TEST_CASE("WorkspaceData::describe logs module and library counts") {
  const klspw::WorkspaceData ws{
      .modules = {{.name = "app",
                   .dependencies = {klspw::LibraryDep{.name = "guava"}, klspw::InheritedSdk{}, klspw::ModuleSource{}},
                   .content_roots = {{.path = "/src/app",
                                      .source_roots = {{.path = "/src/app/main/kotlin", .type = "java-source"}}}}}},
      .libraries = {{.name = "guava", .roots = {{.path = "/cache/guava.jar"}}}},
      .kotlin_settings = {{.name = "Kotlin", .module = "app"}},
  };

  const LogCapture log;
  ws.describe();
  const auto out = log.output();

  // Module count and library count mentioned
  CHECK(out.contains("1 module"));
  CHECK(out.contains("1 library"));
  // Module name mentioned
  CHECK(out.contains("app"));
}
