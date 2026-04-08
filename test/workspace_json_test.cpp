#include <string>

#include <doctest/doctest.h>

#include "workspace_model.hpp"

namespace {

template <typename T>
std::string to_json(const T& val) {
    auto result = glz::write_json(val);
    REQUIRE(result.has_value());
    return std::move(result.value());
}

template <typename T>
T from_json(const std::string& json_str) {
    auto result = glz::read_json<T>(json_str);
    REQUIRE(result.has_value());
    return std::move(result.value());
}

/// Round-trip: file → WorkspaceData → JSON → WorkspaceData → JSON, compare.
/// If the two serializations are identical, the round-trip is lossless.
void assert_round_trip(const std::string& path) {
    const auto ws = from_json<klspw::WorkspaceData>(klspw::read_file(path));

    const auto json1 = to_json(ws);
    REQUIRE_FALSE(json1.empty());

    const auto json2 = to_json(from_json<klspw::WorkspaceData>(json1));
    CHECK(json1 == json2);
}

} // namespace

// --- Round-trip tests against real workspace fixtures ---

TEST_CASE("round-trip root workspace") {
    assert_round_trip("test/fixtures/example_root_workspace.json");
}

TEST_CASE("round-trip proj workspace") {
    assert_round_trip("test/fixtures/example_proj_workspace.json");
}

// --- Structural checks on parsed root workspace ---

TEST_CASE("root workspace structure") {
    const auto json_str = klspw::read_file("test/fixtures/example_root_workspace.json");
    const auto ws = from_json<klspw::WorkspaceData>(json_str);

    CHECK_FALSE(ws.modules.empty());
    CHECK_FALSE(ws.libraries.empty());
    CHECK(ws.sdks.empty());
    CHECK_FALSE(ws.kotlinSettings.empty());
    CHECK(ws.javaSettings.empty());

    SUBCASE("module") {
        const auto& m = ws.modules[0];
        CHECK(m.name == "MyKotlinService-1.0");
        CHECK_FALSE(m.type.has_value());
        CHECK_FALSE(m.dependencies.empty());
        CHECK_FALSE(m.contentRoots.empty());
    }

    SUBCASE("dependencies contain library and inheritedSdk") {
        const auto& deps = ws.modules[0].dependencies;
        bool has_lib = false;
        bool has_isdk = false;
        bool has_msrc = false;
        for (const auto& d : deps) {
            if (std::holds_alternative<klspw::LibraryDep>(d)) { has_lib = true; }
            if (std::holds_alternative<klspw::InheritedSdk>(d)) { has_isdk = true; }
            if (std::holds_alternative<klspw::ModuleSource>(d)) { has_msrc = true; }
        }
        CHECK(has_lib);
        CHECK(has_isdk);
        CHECK(has_msrc);
    }

    SUBCASE("library") {
        const auto& lib = ws.libraries[0];
        CHECK_FALSE(lib.name.empty());
        CHECK_FALSE(lib.roots.empty());
        CHECK(lib.type.has_value());
    }

    SUBCASE("kotlinSettings") {
        const auto& ks = ws.kotlinSettings[0];
        CHECK(ks.name == "Kotlin");
        CHECK_FALSE(ks.module.empty());
        CHECK(ks.version == 5);
        CHECK(ks.kind == "default");
        CHECK(ks.compilerArguments.has_value());
        CHECK(ks.compilerArguments->find("jvmTarget") != std::string::npos);
    }
}

// --- Structural checks on parsed proj workspace ---

TEST_CASE("proj workspace structure") {
    const auto json_str = klspw::read_file("test/fixtures/example_proj_workspace.json");
    const auto ws = from_json<klspw::WorkspaceData>(json_str);

    CHECK_FALSE(ws.modules.empty());
    CHECK_FALSE(ws.libraries.empty());
    CHECK(ws.sdks.empty());
    CHECK(ws.kotlinSettings.empty());

    SUBCASE("module has type") {
        const auto& m = ws.modules[0];
        CHECK(m.type.has_value());
        CHECK(m.type.value() == "JAVA_MODULE");
    }

    SUBCASE("library has level") {
        const auto& lib = ws.libraries[0];
        CHECK(lib.level.has_value());
        CHECK(lib.level.value() == "project");
    }

    SUBCASE("library root has inclusionOptions") {
        bool found = false;
        for (const auto& lib : ws.libraries) {
            for (const auto& root : lib.roots) {
                if (root.inclusionOptions) {
                    CHECK(root.inclusionOptions.value() == "root_itself");
                    found = true;
                    break;
                }
            }
            if (found) { break; }
        }
        CHECK(found);
    }
}

// --- Serialization unit tests ---

TEST_CASE("DependencyScope round-trips") {
    for (auto scope : {klspw::DependencyScope::compile, klspw::DependencyScope::test,
                       klspw::DependencyScope::runtime, klspw::DependencyScope::provided}) {
        const auto json_str = to_json(scope);
        const auto parsed = from_json<klspw::DependencyScope>(json_str);
        CHECK(parsed == scope);
    }
}

TEST_CASE("DependencyData variant round-trips") {
    SUBCASE("library") {
        const klspw::DependencyData d = klspw::LibraryDep{.name = "guava", .scope = klspw::DependencyScope::compile};
        const auto json_str = to_json(d);
        CHECK(json_str.contains("\"type\":\"library\""));
        CHECK(json_str.contains("\"name\":\"guava\""));
        const auto parsed = from_json<klspw::DependencyData>(json_str);
        CHECK(std::holds_alternative<klspw::LibraryDep>(parsed));
        CHECK(std::get<klspw::LibraryDep>(parsed).name == "guava");
    }

    SUBCASE("inheritedSdk") {
        const klspw::DependencyData d = klspw::InheritedSdk{};
        const auto json_str = to_json(d);
        CHECK(json_str.contains("\"type\":\"inheritedSdk\""));
        const auto parsed = from_json<klspw::DependencyData>(json_str);
        CHECK(std::holds_alternative<klspw::InheritedSdk>(parsed));
    }

    SUBCASE("moduleSource") {
        const klspw::DependencyData d = klspw::ModuleSource{};
        const auto json_str = to_json(d);
        CHECK(json_str.contains("\"type\":\"moduleSource\""));
        const auto parsed = from_json<klspw::DependencyData>(json_str);
        CHECK(std::holds_alternative<klspw::ModuleSource>(parsed));
    }
}

TEST_CASE("ModuleDep round-trips") {
    const klspw::DependencyData d = klspw::ModuleDep{
        .name = "core",
        .scope = klspw::DependencyScope::test,
        .isExported = true,
        .isTestJar = true,
    };
    const auto json_str = to_json(d);
    CHECK(json_str.contains("\"type\":\"module\""));
    CHECK(json_str.contains("\"name\":\"core\""));

    const auto parsed = from_json<klspw::DependencyData>(json_str);
    REQUIRE(std::holds_alternative<klspw::ModuleDep>(parsed));
    const auto& m = std::get<klspw::ModuleDep>(parsed);
    CHECK(m.name == "core");
    CHECK(m.scope == klspw::DependencyScope::test);
    CHECK(m.isExported);
    CHECK(m.isTestJar);
}

TEST_CASE("SdkDep round-trips") {
    const klspw::DependencyData d = klspw::SdkDep{.name = "JDK21", .kind = "JavaSDK"};
    const auto json_str = to_json(d);
    CHECK(json_str.contains("\"type\":\"sdk\""));

    const auto parsed = from_json<klspw::DependencyData>(json_str);
    REQUIRE(std::holds_alternative<klspw::SdkDep>(parsed));
    CHECK(std::get<klspw::SdkDep>(parsed).name == "JDK21");
}

TEST_CASE("XmlElement round-trips") {
    const klspw::XmlElement elem{
        .tag = "root",
        .attributes = {{"key", "val"}},
        .children = {{.tag = "child"}},
        .text = "hello",
    };

    const auto json_str = to_json(elem);
    CHECK(json_str.contains("\"tag\":\"root\""));

    const auto parsed = from_json<klspw::XmlElement>(json_str);
    CHECK(parsed.tag == "root");
    CHECK(parsed.attributes.at("key") == "val");
    CHECK(parsed.text.value() == "hello");
    CHECK(parsed.children.size() == 1);
    CHECK(parsed.children[0].tag == "child");
}

TEST_CASE("ContentRootData with excludedPatterns round-trips") {
    const klspw::ContentRootData cr{
        .path = "/src",
        .sourceRoots = {{.path = "/src/main", .type = "java-source"}},
        .excludedPatterns = {"*.bak", "tmp/"},
        .excludedUrls = {"file:///excluded"},
    };

    const auto json_str = to_json(cr);
    const auto parsed = from_json<klspw::ContentRootData>(json_str);
    CHECK(parsed.excludedPatterns.size() == 2);
    CHECK(parsed.excludedUrls[0] == "file:///excluded");
}

TEST_CASE("SdkData round-trips") {
    const klspw::SdkData sdk{
        .name = "JDK21",
        .type = "JavaSDK",
        .version = "21",
        .homePath = "/usr/lib/jvm/java-21",
        .roots = std::vector<klspw::SdkRootData>{{.url = "jar:///rt.jar!/", .type = "classPath"}},
        .additionalData = "extra",
    };

    const auto json_str = to_json(sdk);
    const auto parsed = from_json<klspw::SdkData>(json_str);
    CHECK(parsed.version.value() == "21");
    CHECK(parsed.roots.has_value());
    CHECK(parsed.roots->size() == 1);
    CHECK(parsed.roots->at(0).url == "jar:///rt.jar!/");
}

TEST_CASE("JavaSettingsData round-trips") {
    const klspw::JavaSettingsData js{
        .module = "mymod",
        .inheritedCompilerOutput = false,
        .excludeOutput = false,
        .compilerOutput = "/out",
        .compilerOutputForTests = "/test-out",
        .languageLevelId = "JDK_21",
        .manifestAttributes = {{"Main-Class", "com.example.Main"}},
    };

    const auto json_str = to_json(js);
    const auto parsed = from_json<klspw::JavaSettingsData>(json_str);
    CHECK(parsed.module == "mymod");
    CHECK_FALSE(parsed.inheritedCompilerOutput);
    CHECK(parsed.compilerOutput.value() == "/out");
    CHECK(parsed.languageLevelId.value() == "JDK_21");
    CHECK(parsed.manifestAttributes.at("Main-Class") == "com.example.Main");
}

TEST_CASE("LibraryData with properties round-trips") {
    const klspw::LibraryData lib{
        .name = "mylib",
        .type = "java-imported",
        .roots = {{.path = "/lib.jar"}},
        .excludedRoots = {"/excluded"},
        .properties = klspw::XmlElement{.tag = "properties"},
    };

    const auto json_str = to_json(lib);
    const auto parsed = from_json<klspw::LibraryData>(json_str);
    CHECK(parsed.excludedRoots[0] == "/excluded");
    CHECK(parsed.properties.has_value());
    CHECK(parsed.properties->tag == "properties");
}
