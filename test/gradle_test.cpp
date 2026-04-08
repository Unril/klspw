#include <filesystem>
#include <fstream>
#include <sstream>
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
    CHECK_THROWS_WITH_AS((void)P::extract_json("some output\nKLSPW_END\n"),
                         "KLSPW_BEGIN delimiter not found in Gradle output", std::runtime_error);
}

TEST_CASE("throws on missing KLSPW_END") {
    CHECK_THROWS_WITH_AS((void)P::extract_json("KLSPW_BEGIN\n{}\n"), "KLSPW_END delimiter not found in Gradle output",
                         std::runtime_error);
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

    CHECK(output.root_project() == fs::path{"/tmp/proj"});
    CHECK(output.projects().empty());
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

    REQUIRE(output.projects().size() == 1);
    const auto& proj = output.projects()[0];
    CHECK(proj.project_path() == ":");
    CHECK(proj.project_dir() == fs::path{"/tmp/proj"});
    CHECK(proj.kind() == "jvm");
    CHECK(proj.plugins().size() == 1);
    CHECK_FALSE(proj.is_skipped());
    CHECK(proj.module_name() == "proj");
    CHECK(output.active_project_count() == 1);

    REQUIRE(proj.source_sets().size() == 1);
    const auto& ss = proj.source_sets()[0];
    CHECK(ss.name() == "main");
    CHECK(ss.source_roots().size() == 2);
    CHECK(ss.java_source_roots().size() == 1);
    CHECK(ss.resources_roots().size() == 1);
    CHECK(ss.classes_dirs().size() == 1);
    CHECK(ss.resources_dir().has_value());
    CHECK(ss.compile_classpath().size() == 1);
    CHECK(ss.runtime_classpath().size() == 2);
    CHECK(ss.compile_classpath_config_name() == "compileClasspath");
    CHECK(ss.runtime_classpath_config_name() == "runtimeClasspath");
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

    REQUIRE(output.projects().size() == 1);
    CHECK(output.projects()[0].is_skipped());
    CHECK(output.projects()[0].skip_reason()->find("No JavaPluginExtension") != std::string::npos);
    CHECK(output.projects()[0].source_sets().empty());
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

    REQUIRE(output.projects().size() == 1);
    REQUIRE(output.projects()[0].source_sets().size() == 1);
    CHECK_FALSE(output.projects()[0].source_sets()[0].resources_dir().has_value());
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

    const auto& sets = output.projects()[0].source_sets();
    REQUIRE(sets.size() == 3);
    CHECK_FALSE(sets[0].is_test());
    CHECK(sets[1].is_test());
    CHECK(sets[2].is_test());
}

TEST_CASE("throws on malformed JSON") {
    CHECK_THROWS_AS((void)P::parse("{not valid json}"), std::runtime_error);
}

TEST_CASE("parses fixture file") {
    std::ifstream file("test/fixtures/gradle_output.json");
    REQUIRE(file.good());
    std::ostringstream buf;
    buf << file.rdbuf();

    const auto output = P::parse(buf.str());

    CHECK(output.root_project().string().find("my-kotlin-service") != std::string::npos);
    CHECK_FALSE(output.projects().empty());

    const auto& root_proj = output.projects()[0];
    CHECK(root_proj.project_path() == ":");
    CHECK(root_proj.kind() == "jvm");
    CHECK_FALSE(root_proj.plugins().empty());
    CHECK(root_proj.source_sets().size() >= 2);

    const auto& main_ss = root_proj.source_sets()[0];
    CHECK(main_ss.name() == "main");
    CHECK_FALSE(main_ss.compile_classpath().empty());
    CHECK_FALSE(main_ss.source_roots().empty());
}

// --- GradleRunner ---

namespace {

fs::path make_test_temp_dir() {
    auto dir = fs::temp_directory_path() / "klspw_gradle_test";
    fs::create_directories(dir);
    return dir;
}

struct TempDir {
    fs::path path;
    TempDir() : path{make_test_temp_dir()} {}
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};

} // namespace

TEST_CASE("GradleRunner writes init script to specified temp dir") {
    TempDir tmp;
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    klspw::GradleRunner runner(cfg.build(), tmp.path);

    const auto& path = runner.init_script_path();
    REQUIRE(fs::exists(path));
    CHECK(path.string().starts_with(tmp.path.string()));
    CHECK(path.extension() == ".kts");
}

TEST_CASE("init script contains expected markers") {
    TempDir tmp;
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    klspw::GradleRunner runner(cfg.build(), tmp.path);

    std::ifstream f(runner.init_script_path());
    REQUIRE(f.good());
    std::ostringstream buf;
    buf << f.rdbuf();
    const auto content = buf.str();

    CHECK(content.find("dumpKotlinLspModel") != std::string::npos);
    CHECK(content.find("KLSPW_BEGIN") != std::string::npos);
    CHECK(content.find("KLSPW_END") != std::string::npos);
    CHECK(content.find("JsonOutput") != std::string::npos);
}

TEST_CASE("init script is cleaned up on destruction") {
    TempDir tmp;
    fs::path script_path;
    {
        const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
        klspw::GradleRunner runner(cfg.build(), tmp.path);
        script_path = runner.init_script_path();
        REQUIRE(fs::exists(script_path));
    }
    CHECK_FALSE(fs::exists(script_path));
}

TEST_CASE("GradleRunner uses default temp dir when not specified") {
    const auto cfg = klspw::Config::from_yaml("test/fixtures/example_config.yaml");
    klspw::GradleRunner runner(cfg.build());

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
