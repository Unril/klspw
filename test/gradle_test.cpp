#include "gradle.hpp"

#include <stdexcept>
#include <string>

#include <doctest/doctest.h>

#include "test_common.hpp"

using namespace klspw;

// --- GradleBuildOutput::from_raw_output ---

TEST_CASE("from_raw_output extracts and parses JSON from mixed Gradle output") {
  const auto* const input = R"(
> Configure project :
Some noisy Gradle output here

> Task :dumpKotlinLspModel
More noise
KLSPW_BEGIN
{
    "rootProject": "/some/path",
    "projects": []
}
KLSPW_END

BUILD SUCCESSFUL in 3s
)";

  const auto output = klspw::GradleBuildOutput::from_raw_output(input);
  CHECK(output.root_project == "/some/path");
  CHECK(output.projects.empty());
}

TEST_CASE("from_raw_output with minimal surrounding content") {
  const auto output =
      klspw::GradleBuildOutput::from_raw_output("KLSPW_BEGIN\n{\"rootProject\":\"/p\",\"projects\":[]}\nKLSPW_END");
  CHECK(output.root_project == "/p");
}

TEST_CASE("from_raw_output throws on missing delimiters") {
  CHECK_THROWS_AS((void)klspw::GradleBuildOutput::from_raw_output("some output\nKLSPW_END\n"), std::runtime_error);
  CHECK_THROWS_AS((void)klspw::GradleBuildOutput::from_raw_output("KLSPW_BEGIN\n{}\n"), std::runtime_error);
  CHECK_THROWS_AS((void)klspw::GradleBuildOutput::from_raw_output(""), std::runtime_error);
}

// --- GradleBuildOutput::from_json ---

TEST_CASE("parses minimal valid JSON") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": []
    })");

  CHECK(output.root_project == "/tmp/proj");
  CHECK(output.projects.empty());
  CHECK(output.active_project_count() == 0);
}

TEST_CASE("parses project with source sets") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": ["org.gradle.api.plugins.JavaPlugin"],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": ["/tmp/proj/src/main/java", "/tmp/proj/src/main/kotlin"],
                "javaSourceRoots": ["/tmp/proj/src/main/java"],
                "resourcesRoots": ["/tmp/proj/src/main/resources"],
                "classesDirs": ["/tmp/proj/build/classes/java/main"],
                "resourcesDir": "/tmp/proj/build/resources/main",
                "compileClasspath": ["/cache/lib-1.0.jar"],
                "runtimeClasspath": ["/cache/lib-1.0.jar", "/cache/rt-2.0.jar"],
                "compileClasspathConfigurationName": "compileClasspath",
                "runtimeClasspathConfigurationName": "runtimeClasspath"
            }]
        }]
    })");

  REQUIRE(output.projects.size() == 1);
  const auto& proj = output.projects[0];
  CHECK(proj.project_path == ":");
  CHECK(proj.project_dir == "/tmp/proj");
  CHECK(proj.kind == "jvm");
  CHECK(proj.plugins.size() == 1);
  CHECK_FALSE(proj.is_skipped());
  CHECK(proj.module_name() == "proj");
  CHECK(output.active_project_count() == 1);

  REQUIRE(proj.source_sets.size() == 1);
  const auto& ss = proj.source_sets[0];
  CHECK(ss.name == "main");
  CHECK(ss.source_roots.size() == 2);
  CHECK(ss.java_source_roots.size() == 1);
  CHECK(ss.resources_roots.size() == 1);
  CHECK(ss.classes_dirs.size() == 1);
  CHECK(ss.resources_dir.has_value());
  CHECK(ss.compile_classpath.size() == 1);
  CHECK(ss.runtime_classpath.size() == 2);
  CHECK(ss.compile_classpath_configuration_name == "compileClasspath");
  CHECK(ss.runtime_classpath_configuration_name == "runtimeClasspath");
  CHECK_FALSE(ss.is_test());
}

TEST_CASE("parses project with skip reason") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":sub",
            "projectDir": "/tmp/proj/sub",
            "kind": "non-jvm",
            "plugins": [],
            "sourceSets": [],
            "skipReason": "No JavaPluginExtension/sourceSets on this project."
        }]
    })");

  REQUIRE(output.projects.size() == 1);
  CHECK(output.projects[0].is_skipped());
  CHECK(*output.projects[0].skip_reason == "No JavaPluginExtension/sourceSets on this project.");
  CHECK(output.projects[0].source_sets.empty());
  CHECK(output.active_project_count() == 0);
}

TEST_CASE("parses project with null resourcesDir") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": [],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": [],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "compileClasspath",
                "runtimeClasspathConfigurationName": "runtimeClasspath"
            }]
        }]
    })");

  REQUIRE(output.projects.size() == 1);
  REQUIRE(output.projects[0].source_sets.size() == 1);
  CHECK_FALSE(output.projects[0].source_sets[0].resources_dir.has_value());
}

TEST_CASE("SourceSet::is_test detects test source sets") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [
                {"name":"main","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"test","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"integrationTest","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}
            ]
        }]
    })");

  const auto& sets = output.projects[0].source_sets;
  REQUIRE(sets.size() == 3);
  CHECK_FALSE(sets[0].is_test());
  CHECK(sets[1].is_test());
  CHECK(sets[2].is_test());
}

TEST_CASE("throws on malformed JSON") {
  CHECK_THROWS_AS((void)klspw::GradleBuildOutput::from_json("{not valid json}"), std::runtime_error);
}

TEST_CASE("parses empty fixture file") {
  const auto output = klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_empty_output.json"));

  CHECK(output.root_project == "/home/dev/projects/my-kotlin-service");
  CHECK(output.projects.empty());
  CHECK(output.active_project_count() == 0);
}

TEST_CASE("parses fixture file") {
  const auto output = klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_output.json"));

  CHECK(output.root_project == "/home/dev/projects/my-kotlin-service");
  CHECK_FALSE(output.projects.empty());

  const auto& root_proj = output.projects[0];
  CHECK(root_proj.project_path == ":");
  CHECK(root_proj.kind == "jvm");
  CHECK_FALSE(root_proj.plugins.empty());
  CHECK(root_proj.source_sets.size() >= 2);

  const auto& main_ss = root_proj.source_sets[0];
  CHECK(main_ss.name == "main");
  CHECK_FALSE(main_ss.compile_classpath.empty());
  CHECK_FALSE(main_ss.source_roots.empty());
}

// --- SourceSet → workspace model conversion ---

TEST_CASE("file_stem extracts stem") {
  CHECK(klspw::file_stem("/path/to/kotlin-stdlib-2.0.0.jar") == "kotlin-stdlib-2.0.0");
  CHECK(klspw::file_stem("/cache/jackson-core-2.15.3.jar") == "jackson-core-2.15.3");
  CHECK(klspw::file_stem("simple.jar") == "simple");
}

TEST_CASE("SourceSet::to_source_roots classifies main sources") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": ["/tmp/proj/src/main/java", "/tmp/proj/src/main/kotlin", "/tmp/proj/src/main/resources"],
                "javaSourceRoots": ["/tmp/proj/src/main/java"],
                "resourcesRoots": ["/tmp/proj/src/main/resources"],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": [],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const auto& ss = output.projects[0].source_sets[0];
  const auto roots = ss.to_source_roots();

  // java + kotlin as java-source, resources as java-resource
  REQUIRE(roots.size() == 3);
  CHECK(roots[0].path == "/tmp/proj/src/main/java");
  CHECK(roots[0].type == "java-source");
  CHECK(roots[1].path == "/tmp/proj/src/main/kotlin");
  CHECK(roots[1].type == "java-source");
  CHECK(roots[2].path == "/tmp/proj/src/main/resources");
  CHECK(roots[2].type == "java-resource");
}

TEST_CASE("SourceSet::to_source_roots classifies test sources") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "test",
                "sourceRoots": ["/tmp/proj/src/test/kotlin", "/tmp/proj/src/test/resources"],
                "javaSourceRoots": [],
                "resourcesRoots": ["/tmp/proj/src/test/resources"],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": [],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const auto& ss = output.projects[0].source_sets[0];
  const auto roots = ss.to_source_roots();

  REQUIRE(roots.size() == 2);
  CHECK(roots[0].type == "java-test");
  CHECK(roots[1].type == "java-test-resource");
}

TEST_CASE("SourceSet::collect_libraries builds one library per jar") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": [],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": ["/cache/kotlin-stdlib-2.0.0.jar", "/cache/jackson-core-2.15.3.jar"],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const auto& ss = output.projects[0].source_sets[0];
  const auto libs = ss.collect_libraries();

  REQUIRE(libs.size() == 2);
  CHECK(libs[0].name == "kotlin-stdlib-2.0.0");
  CHECK(libs[0].roots.size() == 1);
  CHECK(libs[0].roots[0].path == "/cache/kotlin-stdlib-2.0.0.jar");
  CHECK(libs[1].name == "jackson-core-2.15.3");
}

TEST_CASE("SourceSet::collect_library_deps uses correct scope") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [
                {"name":"main","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/a.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"test","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/b.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}
            ]
        }]
    })");

  const auto& main_deps = output.projects[0].source_sets[0].collect_library_deps();
  REQUIRE(main_deps.size() == 1);
  CHECK(main_deps[0].scope == klspw::DependencyScope::compile);

  const auto& test_deps = output.projects[0].source_sets[1].collect_library_deps();
  REQUIRE(test_deps.size() == 1);
  CHECK(test_deps[0].scope == klspw::DependencyScope::test);
}

// --- GradleProject → workspace model conversion ---

namespace {
constexpr klspw::GenerationOptions no_tests{.include_tests = false, .remove_missing_paths = false};
constexpr klspw::GenerationOptions with_tests{.include_tests = true, .remove_missing_paths = false};
}  // namespace

TEST_CASE("GradleProject::to_module builds module with deps and content roots") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": ["/tmp/proj/src/main/kotlin"],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": ["/cache/lib-1.0.jar"],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const auto mod = output.projects[0].to_module(output.projects[0].active_sets(no_tests));

  CHECK(mod.name == "proj");
  CHECK(mod.type == "JAVA_MODULE");

  // Should have: 1 library dep + inheritedSdk + moduleSource
  REQUIRE(mod.dependencies.size() == 3);
  CHECK(std::holds_alternative<klspw::LibraryDep>(mod.dependencies[0]));
  CHECK(std::holds_alternative<klspw::InheritedSdk>(mod.dependencies[1]));
  CHECK(std::holds_alternative<klspw::ModuleSource>(mod.dependencies[2]));

  REQUIRE(mod.content_roots.size() == 1);
  CHECK(mod.content_roots[0].path == "/tmp/proj");
  REQUIRE(mod.content_roots[0].source_roots.size() == 1);
  CHECK(mod.content_roots[0].source_roots[0].type == "java-source");
}

TEST_CASE("GradleProject::to_module excludes test source sets when include_tests=false") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [
                {"name":"main","sourceRoots":["/tmp/proj/src/main/kotlin"],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/a.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"test","sourceRoots":["/tmp/proj/src/test/kotlin"],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/a.jar","/cache/junit.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}
            ]
        }]
    })");

  const auto& proj = output.projects[0];
  const auto mod_no_tests = proj.to_module(proj.active_sets(no_tests));
  // Only main source root, only 1 lib dep (a.jar) + 2 sentinels
  CHECK(mod_no_tests.content_roots[0].source_roots.size() == 1);
  CHECK(mod_no_tests.dependencies.size() == 3);

  const auto mod_with_tests = proj.to_module(proj.active_sets(with_tests));
  // main + test source roots, 2 lib deps (a.jar deduped, junit.jar) + 2 sentinels
  CHECK(mod_with_tests.content_roots[0].source_roots.size() == 2);
  CHECK(mod_with_tests.dependencies.size() == 4);
}

TEST_CASE("GradleProject::to_module deduplicates library deps across source sets") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [
                {"name":"main","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/a.jar","/cache/b.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"test","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/a.jar","/cache/c.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}
            ]
        }]
    })");

  const auto mod = output.projects[0].to_module(output.projects[0].active_sets(with_tests));
  // a.jar (from main, compile), b.jar (from main, compile), c.jar (from test, test) + 2 sentinels
  // a.jar appears in both but should be deduped (first occurrence wins = compile scope)
  size_t lib_dep_count = 0;
  for (const auto& dep : mod.dependencies) {
    if (std::holds_alternative<klspw::LibraryDep>(dep)) {
      lib_dep_count++;
    }
  }
  CHECK(lib_dep_count == 3);  // a, b, c (a deduped)
}

TEST_CASE("GradleProject::to_kotlin_settings builds settings") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": ["/tmp/proj/src/main/java", "/tmp/proj/src/main/kotlin"],
                "javaSourceRoots": ["/tmp/proj/src/main/java"],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": [],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const auto& proj = output.projects[0];
  const auto ks = proj.to_kotlin_settings("21", proj.active_sets(no_tests));

  CHECK(ks.name == "Kotlin");
  CHECK(ks.module == "proj");
  CHECK(ks.external_project_id == ":proj:unspecified");
  CHECK(ks.compiler_arguments == R"(J{"jvmTarget":"21"})");

  REQUIRE(ks.source_roots.size() == 2);

  REQUIRE(ks.pure_kotlin_source_folders.size() == 1);
  CHECK(ks.pure_kotlin_source_folders[0] == "/tmp/proj/src/main/kotlin");
}

TEST_CASE("GradleProject::to_kotlin_settings includes compiler plugin classpath") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": [],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": [],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }],
            "compilerPluginClasspath": ["/plugins/serialization.jar", "/plugins/compose.jar"]
        }]
    })");

  const auto& proj = output.projects[0];
  const auto ks = proj.to_kotlin_settings("21", proj.active_sets(no_tests));

  REQUIRE(ks.compiler_arguments.has_value());
  CHECK(ks.compiler_arguments->contains("pluginClasspaths"));
  CHECK(ks.compiler_arguments->contains("serialization.jar"));
  CHECK(ks.compiler_arguments->contains("compose.jar"));
  CHECK(ks.compiler_arguments->contains("jvmTarget"));
  CHECK(*ks.compiler_arguments ==
        R"(J{"jvmTarget":"21","pluginClasspaths":["/plugins/serialization.jar","/plugins/compose.jar"]})");
}

TEST_CASE("GradleProject::to_kotlin_settings omits pluginClasspaths when empty") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": [],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": [],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const auto& proj = output.projects[0];
  const auto ks = proj.to_kotlin_settings("21", proj.active_sets(no_tests));

  REQUIRE(ks.compiler_arguments.has_value());
  CHECK(*ks.compiler_arguments == R"(J{"jvmTarget":"21"})");
  CHECK_FALSE(ks.compiler_arguments->contains("pluginClasspaths"));
}

TEST_CASE("SourceSet::pure_kotlin_roots excludes java and resource dirs") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": ["/tmp/proj/src/main/java", "/tmp/proj/src/main/kotlin", "/tmp/proj/src/main/resources"],
                "javaSourceRoots": ["/tmp/proj/src/main/java"],
                "resourcesRoots": ["/tmp/proj/src/main/resources"],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": [],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const auto& ss = output.projects[0].source_sets[0];
  const auto pure = ss.pure_kotlin_roots();

  REQUIRE(pure.size() == 1);
  CHECK(pure[0] == "/tmp/proj/src/main/kotlin");
}

TEST_CASE("SourceSet::pure_kotlin_roots returns empty when all dirs are java or resources") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": ["/tmp/proj/src/main/java", "/tmp/proj/src/main/resources"],
                "javaSourceRoots": ["/tmp/proj/src/main/java"],
                "resourcesRoots": ["/tmp/proj/src/main/resources"],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": [],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  CHECK(output.projects[0].source_sets[0].pure_kotlin_roots().empty());
}

TEST_CASE("SourceSet::library_from_jar uses source_classpath when available") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": [],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": ["/cache/kotlin-stdlib-2.0.jar"],
                "runtimeClasspath": [],
                "sourceClasspath": {"/cache/kotlin-stdlib-2.0.jar": "/cache/kotlin-stdlib-2.0-sources.jar"},
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const auto& ss = output.projects[0].source_sets[0];
  const auto libs = ss.collect_libraries_with_sources();

  REQUIRE(libs.size() == 1);
  CHECK(libs[0].name == "kotlin-stdlib-2.0");
  REQUIRE(libs[0].roots.size() == 2);
  CHECK(libs[0].roots[0].path == "/cache/kotlin-stdlib-2.0.jar");
  CHECK(libs[0].roots[0].type == "CLASSES");
  CHECK(libs[0].roots[1].path == "/cache/kotlin-stdlib-2.0-sources.jar");
  CHECK(libs[0].roots[1].type == "SOURCES");
}

TEST_CASE("SourceSet::library_from_jar skips sources when no resolver") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": [],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": ["/cache/kotlin-stdlib-2.0.jar"],
                "runtimeClasspath": [],
                "sourceClasspath": {"/cache/kotlin-stdlib-2.0.jar": "/cache/kotlin-stdlib-2.0-sources.jar"},
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const auto& ss = output.projects[0].source_sets[0];
  const auto libs = ss.collect_libraries();

  REQUIRE(libs.size() == 1);
  CHECK(libs[0].roots.size() == 1);
  CHECK(libs[0].roots[0].type == "CLASSES");
}

// --- GradleBuildOutput → WorkspaceData ---

TEST_CASE("GradleBuildOutput::to_workspace builds complete workspace") {
  const auto output = klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_output.json"));
  const auto ws = output.to_workspace("21", no_tests);

  // One active project → one module
  REQUIRE(ws.modules.size() == 1);
  CHECK(ws.modules[0].name == "my-kotlin-service");
  CHECK(ws.modules[0].type == "JAVA_MODULE");

  REQUIRE_FALSE(ws.modules[0].content_roots.empty());
  CHECK_FALSE(ws.modules[0].content_roots[0].source_roots.empty());

  CHECK(ws.modules[0].dependencies.size() >= 3);
  CHECK(ws.libraries.size() == 2);

  REQUIRE(ws.kotlin_settings.size() == 1);
  CHECK(ws.kotlin_settings[0].module == "my-kotlin-service");
  CHECK(ws.kotlin_settings[0].compiler_arguments == R"(J{"jvmTarget":"21"})");
}

TEST_CASE("GradleBuildOutput::to_workspace deduplicates libraries across projects") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/root",
        "projects": [
            {
                "projectPath": ":a",
                "projectDir": "/tmp/root/a",
                "kind": "jvm",
                "plugins": [],
                "sourceSets": [{"name":"main","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/shared.jar","/cache/only-a.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}]
            },
            {
                "projectPath": ":b",
                "projectDir": "/tmp/root/b",
                "kind": "jvm",
                "plugins": [],
                "sourceSets": [{"name":"main","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/shared.jar","/cache/only-b.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}]
            }
        ]
    })");

  const auto ws = output.to_workspace("", no_tests);

  CHECK(ws.modules.size() == 2);
  // shared.jar deduped: only 3 unique libraries
  CHECK(ws.libraries.size() == 3);
}

TEST_CASE("GradleBuildOutput::to_workspace skips skipped projects") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/root",
        "projects": [
            {
                "projectPath": ":active",
                "projectDir": "/tmp/root/active",
                "kind": "jvm",
                "plugins": [],
                "sourceSets": [{"name":"main","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}]
            },
            {
                "projectPath": ":skipped",
                "projectDir": "/tmp/root/skipped",
                "kind": "non-jvm",
                "plugins": [],
                "sourceSets": [],
                "skipReason": "No JVM"
            }
        ]
    })");

  const auto ws = output.to_workspace("", no_tests);

  CHECK(ws.modules.size() == 1);
  CHECK(ws.modules[0].name == "active");
  CHECK(ws.kotlin_settings.size() == 1);
}

// --- WorkspaceData round-trip through to_workspace ---

TEST_CASE("to_workspace output serializes to valid JSON") {
  const auto output = klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_output.json"));
  const auto ws = output.to_workspace("21", no_tests);

  const auto json_result = glz::write_json(ws);
  REQUIRE(json_result.has_value());
  const auto ws2 = glz::read_json<klspw::WorkspaceData>(json_result.value());
  REQUIRE(ws2.has_value());

  CHECK(ws2->modules.size() == ws.modules.size());
  CHECK(ws2->libraries.size() == ws.libraries.size());
  CHECK(ws2->kotlin_settings.size() == ws.kotlin_settings.size());
}

// --- WorkspaceData::merge ---

TEST_CASE("WorkspaceData::merge combines modules and deduplicates libraries") {
  klspw::WorkspaceData ws1{
      .modules = {{.name = "mod-a"}},
      .libraries = {{.name = "shared-lib", .roots = {{.path = "/a/shared.jar"}}},
                    {.name = "only-a", .roots = {{.path = "/a/only-a.jar"}}}},
  };

  klspw::WorkspaceData ws2{
      .modules = {{.name = "mod-b"}},
      .libraries = {{.name = "shared-lib", .roots = {{.path = "/b/shared.jar"}}},
                    {.name = "only-b", .roots = {{.path = "/b/only-b.jar"}}}},
  };

  ws1.merge(std::move(ws2));

  CHECK(ws1.modules.size() == 2);
  CHECK(ws1.modules[0].name == "mod-a");
  CHECK(ws1.modules[1].name == "mod-b");

  // shared-lib deduped (first occurrence wins), so 3 total.
  REQUIRE(ws1.libraries.size() == 3);
  CHECK(ws1.libraries[0].name == "shared-lib");
  CHECK(ws1.libraries[0].roots[0].path == "/a/shared.jar");  // first wins
  CHECK(ws1.libraries[1].name == "only-a");
  CHECK(ws1.libraries[2].name == "only-b");
}

TEST_CASE("WorkspaceData::merge appends kotlin settings") {
  klspw::WorkspaceData ws1{.kotlin_settings = {{.name = "Kotlin", .module = "a"}}};
  klspw::WorkspaceData ws2{.kotlin_settings = {{.name = "Kotlin", .module = "b"}}};

  ws1.merge(std::move(ws2));

  REQUIRE(ws1.kotlin_settings.size() == 2);
  CHECK(ws1.kotlin_settings[0].module == "a");
  CHECK(ws1.kotlin_settings[1].module == "b");
}

// --- SourceSet::remove_missing_paths ---

TEST_CASE("remove_missing_paths removes nonexistent jars from compile_classpath") {
  klspw::SourceSet ss{.name = "main", .compile_classpath = {"/tmp", "/nonexistent/a.jar", "/nonexistent/b.jar"}};

  ss.remove_missing_paths();

  REQUIRE(ss.compile_classpath.size() == 1);
  CHECK(ss.compile_classpath[0] == "/tmp");
}

TEST_CASE("remove_missing_paths removes nonexistent source roots") {
  klspw::SourceSet ss{.name = "main",
                      .source_roots = {"/tmp", "/nonexistent/src"},
                      .java_source_roots = {"/tmp", "/nonexistent/java"},
                      .resources_roots = {"/tmp", "/nonexistent/res"},
                      .classes_dirs = {"/tmp", "/nonexistent/classes"}};

  ss.remove_missing_paths();

  CHECK(ss.source_roots == klspw::strings{"/tmp"});
  CHECK(ss.java_source_roots == klspw::string_set{"/tmp"});
  CHECK(ss.resources_roots == klspw::string_set{"/tmp"});
  CHECK(ss.classes_dirs == klspw::strings{"/tmp"});
}

TEST_CASE("remove_missing_paths no-op when all paths exist") {
  klspw::SourceSet ss{.name = "main", .source_roots = {"/tmp"}, .compile_classpath = {"/tmp"}};

  ss.remove_missing_paths();

  CHECK(ss.source_roots.size() == 1);
  CHECK(ss.compile_classpath.size() == 1);
}

TEST_CASE("remove_missing_paths handles empty collections") {
  klspw::SourceSet ss{.name = "main"};

  ss.remove_missing_paths();

  CHECK(ss.compile_classpath.empty());
  CHECK(ss.source_roots.empty());
}

// --- KMP+Android fixture: library naming with AGP transforms + Gradle cache metadata ---

TEST_CASE("Android project: active_sets picks one variant to avoid redeclarations") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "android",
            "plugins": ["com.android.build.gradle.LibraryPlugin"],
            "sourceSets": [
                {"name":"debug","sourceRoots":["/tmp/proj/src/main/kotlin","/tmp/proj/src/debug/kotlin"],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/a.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"release","sourceRoots":["/tmp/proj/src/main/kotlin","/tmp/proj/src/release/kotlin"],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/a.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"debugUnitTest","sourceRoots":["/tmp/proj/src/test/kotlin","/tmp/proj/src/debug/kotlin"],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/a.jar","/cache/junit.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"releaseUnitTest","sourceRoots":["/tmp/proj/src/test/kotlin","/tmp/proj/src/release/kotlin"],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":["/cache/a.jar","/cache/junit.jar"],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}
            ]
        }]
    })");

  const auto& proj = output.projects[0];

  SUBCASE("without tests: picks only debug variant") {
    constexpr klspw::GenerationOptions opts{.include_tests = false, .remove_missing_paths = false};
    const auto sets = proj.active_sets(opts);
    REQUIRE(sets.size() == 1);
    CHECK(sets[0].name == "debug");
  }

  SUBCASE("with tests: picks debug + debugUnitTest") {
    constexpr klspw::GenerationOptions opts{.include_tests = true, .remove_missing_paths = false};
    const auto sets = proj.active_sets(opts);
    REQUIRE(sets.size() == 2);
    CHECK(sets[0].name == "debug");
    CHECK(sets[1].name == "debugUnitTest");
  }

  SUBCASE("module has no duplicate source roots from release variant") {
    constexpr klspw::GenerationOptions opts{.include_tests = true, .remove_missing_paths = false};
    const auto mod = proj.to_module(proj.active_sets(opts));
    REQUIRE(!mod.content_roots.empty());
    const auto& roots = mod.content_roots[0].source_roots;
    // Should NOT contain src/release/kotlin
    const auto has_release = std::ranges::any_of(roots, [](const auto& r) { return r.path.contains("release"); });
    CHECK_FALSE(has_release);
    // Should contain src/debug/kotlin
    const auto has_debug = std::ranges::any_of(roots, [](const auto& r) { return r.path.contains("debug"); });
    CHECK(has_debug);
  }
}

TEST_CASE("KMP-Android project: active_sets picks debug + androidDebugUnitTest") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "kmp-android",
            "plugins": ["com.android.build.gradle.LibraryPlugin","org.jetbrains.kotlin.gradle.plugin.KotlinMultiplatformPlugin"],
            "sourceSets": [
                {"name":"debug","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"release","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"androidDebugUnitTest","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"androidReleaseUnitTest","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"debugUnitTest","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"releaseUnitTest","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}
            ]
        }]
    })");

  const auto& proj = output.projects[0];
  constexpr klspw::GenerationOptions opts{.include_tests = true, .remove_missing_paths = false};
  const auto sets = proj.active_sets(opts);

  // Should pick: debug, debugUnitTest, androidDebugUnitTest
  REQUIRE(sets.size() == 3);
  CHECK(sets[0].name == "debug");
  CHECK(sets[1].name == "debugUnitTest");
  CHECK(sets[2].name == "androidDebugUnitTest");
}

TEST_CASE("JVM project: active_sets returns all source sets (no variant filtering)") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [
                {"name":"main","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""},
                {"name":"test","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}
            ]
        }]
    })");

  constexpr klspw::GenerationOptions opts{.include_tests = true, .remove_missing_paths = false};
  const auto sets = output.projects[0].active_sets(opts);
  CHECK(sets.size() == 2);
}

TEST_CASE("KMP+Android: parses fixture with AGP transforms and Gradle cache metadata jars") {
  const auto output =
      klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_kmp_android_output.json"));

  CHECK(output.root_project == "/home/dev/projects/kmp-android");
  REQUIRE(output.projects.size() == 2);
  CHECK(output.projects[0].kind == "kmp-android");
  CHECK(output.active_project_count() == 2);
}

TEST_CASE("KMP+Android: Gradle cache metadata jars get Maven coordinate names") {
  const auto output =
      klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_kmp_android_output.json"));
  constexpr klspw::GenerationOptions opts{
      .include_tests = false, .attach_sources = true, .remove_missing_paths = false};
  const auto ws = output.to_workspace("", opts);

  // Three distinct public-metadata.jar files should produce three distinct library names.
  const auto lib_names = ws.library_names();
  CHECK(lib_names.contains("com.example.platform:presenter-public:1.0.0"));
  CHECK(lib_names.contains("com.example.platform:scope-public:1.0.0"));
  CHECK(lib_names.contains("com.example.logger:logger-public:2.0.0"));

  // AGP transform jars now get Maven coordinate names from classpathCoordinates.
  CHECK(lib_names.contains("com.example.platform:presenter-public-android:1.0.0"));
  CHECK(lib_names.contains("com.example.platform:scope-public-android:1.0.0"));
  CHECK(lib_names.contains("org.jetbrains.kotlin:kotlin-stdlib:2.0.0"));
}

TEST_CASE("KMP+Android: no library name collisions from public-metadata.jar") {
  const auto output =
      klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_kmp_android_output.json"));
  constexpr klspw::GenerationOptions opts{
      .include_tests = false, .attach_sources = true, .remove_missing_paths = false};
  const auto ws = output.to_workspace("", opts);

  // Without Maven coordinate naming, all three would collide as "public-metadata".
  // With it, each gets a unique name. Verify no duplicates.
  std::unordered_set<std::string> seen;
  for (const auto& lib : ws.libraries) {
    CHECK_MESSAGE(seen.insert(lib.name).second, "Duplicate library name: ", lib.name);
  }
}

TEST_CASE("KMP+Android: Gradle cache metadata jars have sources attached via sourceClasspath") {
  const auto output =
      klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_kmp_android_output.json"));
  constexpr klspw::GenerationOptions opts{
      .include_tests = false, .attach_sources = true, .remove_missing_paths = false};
  const auto ws = output.to_workspace("", opts);

  // Find presenter-public and verify it has sources.
  const auto it = std::ranges::find_if(
      ws.libraries, [](const auto& lib) { return lib.name == "com.example.platform:presenter-public:1.0.0"; });
  REQUIRE(it != ws.libraries.end());
  REQUIRE(it->roots.size() == 2);
  CHECK(it->roots[0].type == "CLASSES");
  CHECK(it->roots[0].path.contains("public-metadata.jar"));
  CHECK(it->roots[1].type == "SOURCES");
  CHECK(it->roots[1].path.contains("presenter-public-1.0.0-sources.jar"));
}

TEST_CASE("KMP+Android: AGP transform libraries get unique names from classpathCoordinates") {
  const auto output =
      klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_kmp_android_output.json"));
  constexpr klspw::GenerationOptions opts{
      .include_tests = false, .attach_sources = true, .remove_missing_paths = false};
  const auto ws = output.to_workspace("", opts);

  // Two jetified-public-release-api.jar files with different transform hashes
  // now get distinct Maven coordinate names instead of colliding.
  const auto it = std::ranges::find_if(
      ws.libraries, [](const auto& lib) { return lib.name == "com.example.platform:presenter-public-android:1.0.0"; });
  REQUIRE(it != ws.libraries.end());
  CHECK(it->roots[0].path.contains("transforms/aaa111"));
}

TEST_CASE("KMP+Android: module deps reference both AGP and Maven-named libraries") {
  const auto output =
      klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_kmp_android_output.json"));
  constexpr klspw::GenerationOptions opts{
      .include_tests = false, .attach_sources = true, .remove_missing_paths = false};
  const auto ws = output.to_workspace("", opts);

  // The 'app' module should depend on both AGP and Maven-named libraries.
  const auto& app = ws.modules[0];
  CHECK(app.name == "app");

  auto has_lib_dep = [&](const std::string& name) {
    return std::ranges::any_of(app.dependencies, [&](const auto& d) {
      const auto* lib = std::get_if<klspw::LibraryDep>(&d);
      return lib && lib->name == name;
    });
  };

  CHECK(has_lib_dep("com.example.platform:presenter-public-android:1.0.0"));
  CHECK(has_lib_dep("com.example.platform:presenter-public:1.0.0"));
  CHECK(has_lib_dep("com.example.platform:scope-public:1.0.0"));
  CHECK(has_lib_dep("com.example.logger:logger-public:2.0.0"));
}

TEST_CASE("KMP+Android: referential integrity holds") {
  const auto output =
      klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_kmp_android_output.json"));
  constexpr klspw::GenerationOptions opts{
      .include_tests = false, .attach_sources = true, .remove_missing_paths = false};
  const auto ws = output.to_workspace("", opts);

  const auto lib_names = ws.library_names();
  for (const auto& mod : ws.modules) {
    for (const auto& dep : mod.dependencies) {
      const auto* lib = std::get_if<klspw::LibraryDep>(&dep);
      if (lib != nullptr) {
        CHECK_MESSAGE(lib_names.contains(lib->name), "Module '", mod.name, "' references missing library '", lib->name,
                      "'");
      }
    }
  }
}

TEST_CASE("KMP+Android: R class jars get unique namespace-based library names") {
  const auto output =
      klspw::GradleBuildOutput::from_json(klspw::read_file("test/fixtures/gradle_kmp_android_output.json"));
  constexpr klspw::GenerationOptions opts{
      .include_tests = false, .attach_sources = true, .remove_missing_paths = false};
  const auto ws = output.to_workspace("", opts);

  // Each Android module should have its own R-class library with a unique coordinate name.
  const auto lib_names = ws.library_names();
  CHECK(lib_names.contains("com.example.kmp.app:R:debug"));
  CHECK(lib_names.contains("com.example.kmp.core:R:debug"));

  // Each module should depend on its own R-class library.
  for (const auto& mod : ws.modules) {
    const auto r_name = "com.example.kmp." + mod.name + ":R:debug";
    const auto has_r_dep =
        std::ranges::any_of(mod.deps_of_type<klspw::LibraryDep>(), [&](const auto& dep) { return dep.name == r_name; });
    CHECK_MESSAGE(has_r_dep, "Module '", mod.name, "' should depend on '", r_name, "'");
  }
}

// --- describe ---

TEST_CASE("GradleBuildOutput::describe logs project summary and source sets") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/my-project",
        "projects": [{
            "projectPath": ":app",
            "projectDir": "/tmp/my-project/app",
            "kind": "jvm",
            "plugins": ["org.jetbrains.kotlin.jvm"],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": ["/tmp/my-project/app/src/main/kotlin"],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": ["/cache/lib-1.0.jar", "/cache/lib-2.0.jar"],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  const LogCapture log;
  output.describe();
  const auto out = log.output();

  // Root project path mentioned
  CHECK(out.contains("/tmp/my-project"));
  // Project path mentioned
  CHECK(out.contains(":app"));
  // Source set name mentioned
  CHECK(out.contains("main"));
  // Project count mentioned
  CHECK(out.contains("1"));
}

// --- collect_project_deps ---

TEST_CASE("collect_project_deps returns module-to-dependency mapping") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/root",
        "projects": [
            {
                "projectPath": ":app",
                "projectDir": "/tmp/root/app",
                "kind": "jvm",
                "plugins": [],
                "sourceSets": [{"name":"main","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}],
                "projectDependencies": [":core", ":util"]
            },
            {
                "projectPath": ":core",
                "projectDir": "/tmp/root/core",
                "kind": "jvm",
                "plugins": [],
                "sourceSets": [{"name":"main","sourceRoots":[],"javaSourceRoots":[],"resourcesRoots":[],"classesDirs":[],"resourcesDir":null,"compileClasspath":[],"runtimeClasspath":[],"compileClasspathConfigurationName":"","runtimeClasspathConfigurationName":""}]
            }
        ]
    })");

  const auto deps = output.collect_project_deps();

  // app depends on core and util; core has no project deps so it's absent
  REQUIRE(deps.size() == 1);
  CHECK(deps.contains("app"));
  CHECK(deps.at("app").contains(":core"));
  CHECK(deps.at("app").contains(":util"));
}

TEST_CASE("collect_project_deps skips skipped projects") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/root",
        "projects": [{
            "projectPath": ":skipped",
            "projectDir": "/tmp/root/skipped",
            "kind": "non-jvm",
            "plugins": [],
            "sourceSets": [],
            "skipReason": "No JVM",
            "projectDependencies": [":core"]
        }]
    })");

  CHECK(output.collect_project_deps().empty());
}

// --- to_workspace with attach_sources=false ---

TEST_CASE("to_workspace with attach_sources=false uses collect_libraries without sources") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": [],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": ["/cache/lib-1.0.jar"],
                "runtimeClasspath": [],
                "sourceClasspath": {"/cache/lib-1.0.jar": "/cache/lib-1.0-sources.jar"},
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  constexpr klspw::GenerationOptions opts{.attach_sources = false, .remove_missing_paths = false};
  const auto ws = output.to_workspace("21", opts);

  REQUIRE(ws.libraries.size() == 1);
  // Only CLASSES root, no SOURCES even though sourceClasspath is available
  CHECK(ws.libraries[0].roots.size() == 1);
  CHECK(ws.libraries[0].roots[0].type == "CLASSES");
}

// --- to_workspace with remove_missing_paths=true ---

TEST_CASE("to_workspace with remove_missing_paths=true removes nonexistent paths") {
  const auto output = klspw::GradleBuildOutput::from_json(R"({
        "rootProject": "/tmp/proj",
        "projects": [{
            "projectPath": ":",
            "projectDir": "/tmp/proj",
            "kind": "jvm",
            "plugins": [],
            "sourceSets": [{
                "name": "main",
                "sourceRoots": ["/tmp"],
                "javaSourceRoots": [],
                "resourcesRoots": [],
                "classesDirs": [],
                "resourcesDir": null,
                "compileClasspath": ["/tmp", "/nonexistent/missing.jar"],
                "runtimeClasspath": [],
                "compileClasspathConfigurationName": "",
                "runtimeClasspathConfigurationName": ""
            }]
        }]
    })");

  constexpr klspw::GenerationOptions opts{.attach_sources = false, .remove_missing_paths = true};
  const auto ws = output.to_workspace("21", opts);

  // /nonexistent/missing.jar removed, only /tmp remains
  REQUIRE(ws.libraries.size() == 1);
  CHECK(ws.libraries[0].roots[0].path == "/tmp");
}
