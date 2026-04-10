#include <filesystem>
#include <stdexcept>
#include <string>

#include <doctest/doctest.h>

#include "gradle_runner.hpp"
#include "gradle.hpp"
#include "test_common.hpp"

namespace fs = std::filesystem;

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

// --- GradleRunner ---

TEST_CASE("GradleRunner writes init script to specified temp dir") {
    const TempDir tmp;
    const klspw::GradleRunner runner(tmp.path);

    const auto& path = runner.init_script_path();
    REQUIRE(fs::exists(path));
    CHECK(path.string().starts_with(tmp.path.string()));
    CHECK(path.extension() == ".kts");
}

TEST_CASE("init script contains expected markers") {
    const TempDir tmp;
    const klspw::GradleRunner runner(tmp.path);

    const auto content = klspw::read_file(runner.init_script_path());

    CHECK(content.contains("dumpKotlinLspModel"));
    CHECK(content.contains("KLSPW_BEGIN"));
    CHECK(content.contains("KLSPW_END"));
    CHECK(content.contains("JsonOutput"));
}

TEST_CASE("init script is cleaned up on destruction") {
    const TempDir tmp;
    fs::path script_path;
    {
        const klspw::GradleRunner runner(tmp.path);
        script_path = runner.init_script_path();
        REQUIRE(fs::exists(script_path));
    }
    CHECK_FALSE(fs::exists(script_path));
}

TEST_CASE("GradleRunner uses default temp dir when not specified") {
    const klspw::GradleRunner runner;

    const auto& path = runner.init_script_path();
    REQUIRE(fs::exists(path));
    CHECK(path.string().contains("klspw"));
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
constexpr klspw::GenerationOptions no_tests{.include_tests = false};
constexpr klspw::GenerationOptions with_tests{.include_tests = true};
} // namespace

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
    CHECK(lib_dep_count == 3); // a, b, c (a deduped)
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
    const auto ks = proj.to_kotlin_settings(R"(J{"jvmTarget":"21"})", proj.active_sets(no_tests));

    CHECK(ks.name == "Kotlin");
    CHECK(ks.module == "proj");
    CHECK(ks.external_project_id == ":proj:unspecified");
    CHECK(ks.compiler_arguments == R"(J{"jvmTarget":"21"})");

    REQUIRE(ks.source_roots.size() == 2);

    REQUIRE(ks.pure_kotlin_source_folders.size() == 1);
    CHECK(ks.pure_kotlin_source_folders[0] == "/tmp/proj/src/main/kotlin");
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
    const auto ws = output.to_workspace(R"(J{"jvmTarget":"21"})", no_tests);

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
    const auto ws = output.to_workspace(R"(J{"jvmTarget":"21"})", no_tests);

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
    CHECK(ws1.libraries[0].roots[0].path == "/a/shared.jar"); // first wins
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
