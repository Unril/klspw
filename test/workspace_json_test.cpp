#include <string>

#include <doctest/doctest.h>

#include "workspace_model.hpp"

namespace {

template <typename T> std::string to_json(const T& val) {
    auto result = glz::write_json(val);
    REQUIRE(result.has_value());
    return result.value();
}

template <typename T> T from_json(const std::string& json_str) {
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

} // namespace

TEST_CASE("round-trip root workspace") {
    assert_round_trip("test/fixtures/example_root_workspace.json");
}
TEST_CASE("round-trip proj workspace") {
    assert_round_trip("test/fixtures/example_proj_workspace.json");
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

    SUBCASE("module has type") {
        CHECK(ws.modules[0].type == "JAVA_MODULE");
    }

    SUBCASE("library has level") {
        CHECK(ws.libraries[0].level == "project");
    }

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
