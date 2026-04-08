#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <doctest/doctest.h>

#include "gradle.hpp"
#include "gradle_output.hpp"

namespace fs = std::filesystem;
using P = klspw::GradleOutputParser;

// --- GradleOutputParser::extract_json ---

TEST_CASE("extracts JSON block from mixed Gradle output") {
    const auto* const input = R"(
> Configure project :
Some noisy Gradle output here
Upgrading CoralCore 1.0 -> 1.1

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

    const auto json = P::extract_json(input);

    CHECK(json.front() == '{');
    CHECK(json.back() == '}');
    CHECK(json.find("rootProject") != std::string::npos);
    CHECK(json.find("KLSPW_BEGIN") == std::string::npos);
    CHECK(json.find("KLSPW_END") == std::string::npos);
}

TEST_CASE("extracts JSON block with minimal surrounding content") {
    CHECK(P::extract_json("KLSPW_BEGIN\n{}\nKLSPW_END") == "{}");
}

TEST_CASE("throws on missing KLSPW_BEGIN") {
    CHECK_THROWS_AS((void)P::extract_json("some output\nKLSPW_END\n"), std::runtime_error);
}

TEST_CASE("throws on missing KLSPW_END") {
    CHECK_THROWS_AS((void)P::extract_json("KLSPW_BEGIN\n{}\n"), std::runtime_error);
}

TEST_CASE("throws on empty input") {
    CHECK_THROWS_AS((void)P::extract_json(""), std::runtime_error);
}

// --- GradleOutputParser::parse ---

TEST_CASE("parses minimal valid JSON") {
    const auto output = P::parse(R"({
        "rootProject": "/tmp/proj",
        "projects": []
    })");

    CHECK(output.root_project == "/tmp/proj");
    CHECK(output.projects.empty());
    CHECK(output.active_project_count() == 0);
}

TEST_CASE("parses project with source sets") {
    const auto output = P::parse(R"({
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
    CHECK(ss.compile_classpath_config_name == "compileClasspath");
    CHECK(ss.runtime_classpath_config_name == "runtimeClasspath");
    CHECK_FALSE(ss.is_test());
}

TEST_CASE("parses project with skip reason") {
    const auto output = P::parse(R"({
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
    const auto output = P::parse(R"({
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
    const auto output = P::parse(R"({
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
    CHECK_THROWS_AS((void)P::parse("{not valid json}"), std::runtime_error);
}

TEST_CASE("parses empty fixture file") {
    const auto output = P::parse(klspw::read_file("test/fixtures/gradle_empty_output.json"));

    CHECK(output.root_project == "/home/dev/projects/my-kotlin-service");
    CHECK(output.projects.empty());
    CHECK(output.active_project_count() == 0);
}

TEST_CASE("parses fixture file") {
    const auto output = P::parse(klspw::read_file("test/fixtures/gradle_output.json"));

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

namespace {

struct TempDir {
    fs::path path;

    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("klspw_gradle_test_" + std::to_string(counter++));
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
};

} // namespace

TEST_CASE("GradleRunner writes init script to specified temp dir") {
    const TempDir tmp;
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const klspw::GradleRunner runner(cfg.build(), tmp.path);

    const auto& path = runner.init_script_path();
    REQUIRE(fs::exists(path));
    CHECK(path.string().starts_with(tmp.path.string()));
    CHECK(path.extension() == ".kts");
}

TEST_CASE("init script contains expected markers") {
    const TempDir tmp;
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const klspw::GradleRunner runner(cfg.build(), tmp.path);

    const auto content = klspw::read_file(runner.init_script_path());

    CHECK(content.find("dumpKotlinLspModel") != std::string::npos);
    CHECK(content.find("KLSPW_BEGIN") != std::string::npos);
    CHECK(content.find("KLSPW_END") != std::string::npos);
    CHECK(content.find("JsonOutput") != std::string::npos);
}

TEST_CASE("init script is cleaned up on destruction") {
    const TempDir tmp;
    fs::path script_path;
    {
        const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
        const klspw::GradleRunner runner(cfg.build(), tmp.path);
        script_path = runner.init_script_path();
        REQUIRE(fs::exists(script_path));
    }
    CHECK_FALSE(fs::exists(script_path));
}

TEST_CASE("GradleRunner uses default temp dir when not specified") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const klspw::GradleRunner runner(cfg.build());

    const auto& path = runner.init_script_path();
    REQUIRE(fs::exists(path));
    CHECK(path.string().contains("klspw"));
}

// --- BuildConfig ---

TEST_CASE("BuildConfig::gradle_args_for produces correct argument order") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const auto args = cfg.build().gradle_args_for("/tmp/proj", "/tmp/init.gradle.kts");

    // command: [mybuild, gradle], gradle_args: [--quiet]
    // mybuild gradle --init-script /tmp/init.gradle.kts --quiet -p /tmp/proj dumpKotlinLspModel
    REQUIRE(args.size() == 8);
    CHECK(args[0] == "mybuild");
    CHECK(args[1] == "gradle");
    CHECK(args[2] == "--init-script");
    CHECK(args[3] == "/tmp/init.gradle.kts");
    CHECK(args[4] == "--quiet");
    CHECK(args[5] == "-p");
    CHECK(args[6] == "/tmp/proj");
    CHECK(args[7] == "dumpKotlinLspModel");
}

// --- SourceSet → workspace model conversion ---

TEST_CASE("SourceSet::library_name_for_jar extracts stem") {
    using SS = klspw::SourceSet;
    CHECK(SS::library_name_for_jar("/path/to/kotlin-stdlib-2.0.0.jar") == "kotlin-stdlib-2.0.0");
    CHECK(SS::library_name_for_jar("/cache/jackson-core-2.15.3.jar") == "jackson-core-2.15.3");
    CHECK(SS::library_name_for_jar("simple.jar") == "simple");
}

TEST_CASE("SourceSet::to_source_roots classifies main sources") {
    const auto output = P::parse(R"({
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
    const auto output = P::parse(R"({
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
    const auto output = P::parse(R"({
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
    const auto output = P::parse(R"({
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
    const auto& main_dep = std::get<klspw::LibraryDep>(main_deps[0]);
    CHECK(main_dep.scope == klspw::DependencyScope::compile);

    const auto& test_deps = output.projects[0].source_sets[1].collect_library_deps();
    REQUIRE(test_deps.size() == 1);
    const auto& test_dep = std::get<klspw::LibraryDep>(test_deps[0]);
    CHECK(test_dep.scope == klspw::DependencyScope::test);
}

// --- GradleProject → workspace model conversion ---

TEST_CASE("GradleProject::to_module builds module with deps and content roots") {
    const auto output = P::parse(R"({
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

    const auto mod = output.projects[0].to_module(false);

    CHECK(mod.name == "proj");
    CHECK(mod.type == "JAVA_MODULE");

    // Should have: 1 library dep + inheritedSdk + moduleSource
    REQUIRE(mod.dependencies.size() == 3);
    CHECK(std::holds_alternative<klspw::LibraryDep>(mod.dependencies[0]));
    CHECK(std::holds_alternative<klspw::InheritedSdk>(mod.dependencies[1]));
    CHECK(std::holds_alternative<klspw::ModuleSource>(mod.dependencies[2]));

    REQUIRE(mod.contentRoots.size() == 1);
    CHECK(mod.contentRoots[0].path == "/tmp/proj");
    REQUIRE(mod.contentRoots[0].sourceRoots.size() == 1);
    CHECK(mod.contentRoots[0].sourceRoots[0].type == "java-source");
}

TEST_CASE("GradleProject::to_module excludes test source sets when include_tests=false") {
    const auto output = P::parse(R"({
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

    const auto mod_no_tests = output.projects[0].to_module(false);
    // Only main source root, only 1 lib dep (a.jar) + 2 sentinels
    CHECK(mod_no_tests.contentRoots[0].sourceRoots.size() == 1);
    CHECK(mod_no_tests.dependencies.size() == 3);

    const auto mod_with_tests = output.projects[0].to_module(true);
    // main + test source roots, 2 lib deps (a.jar deduped, junit.jar) + 2 sentinels
    CHECK(mod_with_tests.contentRoots[0].sourceRoots.size() == 2);
    CHECK(mod_with_tests.dependencies.size() == 4);
}

TEST_CASE("GradleProject::to_module deduplicates library deps across source sets") {
    const auto output = P::parse(R"({
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

    const auto mod = output.projects[0].to_module(true);
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
    const auto output = P::parse(R"({
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

    const auto ks = output.projects[0].to_kotlin_settings(R"(J{"jvmTarget":"21"})", false);

    CHECK(ks.name == "Kotlin");
    CHECK(ks.module == "proj");
    CHECK(ks.externalProjectId == ":proj:unspecified");
    CHECK(ks.compilerArguments == R"(J{"jvmTarget":"21"})");

    // source_roots: both java and kotlin dirs
    REQUIRE(ks.sourceRoots.size() == 2);

    // pure kotlin: only the kotlin dir (not the java dir)
    REQUIRE(ks.pureKotlinSourceFolders.size() == 1);
    CHECK(ks.pureKotlinSourceFolders[0] == "/tmp/proj/src/main/kotlin");
}

// --- GradleBuildOutput → WorkspaceData ---

TEST_CASE("GradleBuildOutput::to_workspace builds complete workspace") {
    const auto output = P::parse(klspw::read_file("test/fixtures/gradle_output.json"));
    const auto ws = output.to_workspace(R"(J{"jvmTarget":"21"})", false);

    // One active project → one module
    REQUIRE(ws.modules.size() == 1);
    CHECK(ws.modules[0].name == "my-kotlin-service");
    CHECK(ws.modules[0].type == "JAVA_MODULE");

    // Module has content roots with source roots
    REQUIRE_FALSE(ws.modules[0].contentRoots.empty());
    CHECK_FALSE(ws.modules[0].contentRoots[0].sourceRoots.empty());

    // Module has library deps + inheritedSdk + moduleSource
    CHECK(ws.modules[0].dependencies.size() >= 3);

    // Libraries from main compile classpath (kotlin-stdlib + jackson-core)
    CHECK(ws.libraries.size() == 2);

    // Kotlin settings for the module
    REQUIRE(ws.kotlinSettings.size() == 1);
    CHECK(ws.kotlinSettings[0].module == "my-kotlin-service");
    CHECK(ws.kotlinSettings[0].compilerArguments == R"(J{"jvmTarget":"21"})");
}

TEST_CASE("GradleBuildOutput::to_workspace deduplicates libraries across projects") {
    const auto output = P::parse(R"({
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

    const auto ws = output.to_workspace("", false);

    CHECK(ws.modules.size() == 2);
    // shared.jar deduped: only 3 unique libraries
    CHECK(ws.libraries.size() == 3);
}

TEST_CASE("GradleBuildOutput::to_workspace skips skipped projects") {
    const auto output = P::parse(R"({
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

    const auto ws = output.to_workspace("", false);

    CHECK(ws.modules.size() == 1);
    CHECK(ws.modules[0].name == "active");
    CHECK(ws.kotlinSettings.size() == 1);
}

// --- Config::compiler_arguments_json ---

TEST_CASE("Config::compiler_arguments_json formats J-prefixed JSON") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    const auto args = cfg.compiler_arguments_json();

    CHECK(args.starts_with("J{"));
    CHECK(args.ends_with("}"));
    CHECK(args.contains("jvmTarget"));
}

// --- WorkspaceData round-trip through to_workspace ---

TEST_CASE("to_workspace output serializes to valid JSON") {
    const auto output = P::parse(klspw::read_file("test/fixtures/gradle_output.json"));
    const auto ws = output.to_workspace(R"(J{"jvmTarget":"21"})", false);

    // Serialize to JSON and back via glaze
    const auto json_result = glz::write_json(ws);
    REQUIRE(json_result.has_value());
    const auto ws2 = glz::read_json<klspw::WorkspaceData>(json_result.value());
    REQUIRE(ws2.has_value());

    CHECK(ws2->modules.size() == ws.modules.size());
    CHECK(ws2->libraries.size() == ws.libraries.size());
    CHECK(ws2->kotlinSettings.size() == ws.kotlinSettings.size());
}
