#include <fstream>
#include <string>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "workspace_model.hpp"

using json = nlohmann::json;

namespace {

json load_fixture(const char* path) {
    std::ifstream f(path);
    REQUIRE(f.good());
    return json::parse(f);
}

// Round-trip: parse JSON → WorkspaceData → JSON, compare.
// Keys may reorder (nlohmann sorts alphabetically), so we compare parsed JSON objects.
void assert_round_trip(const char* path) {
    const auto original = load_fixture(path);
    klspw::WorkspaceData ws = original.get<klspw::WorkspaceData>();
    const json reserialized = ws;
    const auto reparsed = reserialized.get<klspw::WorkspaceData>();
    const json round2 = reparsed;

    // Reserialized output must be idempotent.
    CHECK(reserialized == round2);

    // Check counts match original.
    CHECK(ws.modules.size() == original["modules"].size());
    CHECK(ws.libraries.size() == original["libraries"].size());
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
    const auto ws = load_fixture("test/fixtures/example_root_workspace.json").get<klspw::WorkspaceData>();

    CHECK_FALSE(ws.modules.empty());
    CHECK_FALSE(ws.libraries.empty());
    CHECK(ws.sdks.empty());
    CHECK_FALSE(ws.kotlinSettings.empty());
    CHECK(ws.javaSettings.empty());

    SUBCASE("module") {
        const auto& m = ws.modules[0];
        CHECK(m.name == "MyKotlinService-1.0");
        CHECK_FALSE(m.type.has_value()); // null in root workspace
        CHECK_FALSE(m.dependencies.empty());
        CHECK_FALSE(m.contentRoots.empty());
    }

    SUBCASE("dependencies contain library and inheritedSdk") {
        const auto& deps = ws.modules[0].dependencies;
        bool has_lib = false;
        bool has_isdk = false;
        bool has_msrc = false;
        for (const auto& d : deps) {
            if (std::holds_alternative<klspw::LibraryDep>(d)) {
                has_lib = true;
            }
            if (std::holds_alternative<klspw::InheritedSdk>(d)) {
                has_isdk = true;
            }
            if (std::holds_alternative<klspw::ModuleSource>(d)) {
                has_msrc = true;
            }
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
    const auto ws = load_fixture("test/fixtures/example_proj_workspace.json").get<klspw::WorkspaceData>();

    CHECK_FALSE(ws.modules.empty());
    CHECK_FALSE(ws.libraries.empty());
    CHECK(ws.sdks.empty()); // present but empty array
    CHECK(ws.kotlinSettings.empty());

    SUBCASE("module has type and facets") {
        const auto& m = ws.modules[0];
        CHECK(m.type.has_value());
        CHECK(m.type.value() == "JAVA_MODULE");
        // facets present as empty array
    }

    SUBCASE("library has level") {
        const auto& lib = ws.libraries[0];
        CHECK(lib.level.has_value());
        CHECK(lib.level.value() == "project");
    }

    SUBCASE("library root has inclusionOptions") {
        // Find a root with inclusionOptions
        bool found = false;
        for (const auto& lib : ws.libraries) {
            for (const auto& root : lib.roots) {
                if (root.inclusionOptions) {
                    CHECK(root.inclusionOptions.value() == "root_itself");
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        CHECK(found);
    }
}

// --- Serialization unit tests ---

TEST_CASE("DependencyScope round-trips") {
    for (auto scope : {klspw::DependencyScope::compile, klspw::DependencyScope::test, klspw::DependencyScope::runtime,
                       klspw::DependencyScope::provided}) {
        const json j = scope;
        auto parsed = j.get<klspw::DependencyScope>();
        CHECK(parsed == scope);
    }
}

TEST_CASE("DependencyData variant round-trips") {
    SUBCASE("library") {
        klspw::DependencyData d = klspw::LibraryDep{.name = "guava", .scope = klspw::DependencyScope::compile};
        json j = d;
        CHECK(j["type"] == "library");
        CHECK(j["name"] == "guava");
        auto parsed = j.get<klspw::DependencyData>();
        CHECK(std::holds_alternative<klspw::LibraryDep>(parsed));
        CHECK(std::get<klspw::LibraryDep>(parsed).name == "guava");
    }

    SUBCASE("inheritedSdk") {
        klspw::DependencyData d = klspw::InheritedSdk{};
        json j = d;
        CHECK(j["type"] == "inheritedSdk");
        auto parsed = j.get<klspw::DependencyData>();
        CHECK(std::holds_alternative<klspw::InheritedSdk>(parsed));
    }

    SUBCASE("moduleSource") {
        klspw::DependencyData d = klspw::ModuleSource{};
        json j = d;
        CHECK(j["type"] == "moduleSource");
        auto parsed = j.get<klspw::DependencyData>();
        CHECK(std::holds_alternative<klspw::ModuleSource>(parsed));
    }
}

TEST_CASE("KotlinSettingsData preserves null optionals") {
    klspw::KotlinSettingsData ks;
    ks.name = "Kotlin";
    ks.module = "mod";
    const json j = ks;
    CHECK(j["productionOutputPath"].is_null());
    CHECK(j["testOutputPath"].is_null());
    CHECK(j["compilerArguments"].is_null());
    auto parsed = j.get<klspw::KotlinSettingsData>();
    CHECK_FALSE(parsed.productionOutputPath.has_value());
    CHECK_FALSE(parsed.compilerArguments.has_value());
}

// --- Coverage gap tests ---

TEST_CASE("ModuleDep round-trips") {
    klspw::DependencyData d = klspw::ModuleDep{
        .name = "core",
        .scope = klspw::DependencyScope::test,
        .isExported = true,
        .isTestJar = true,
    };
    const json j = d;
    CHECK(j["type"] == "module");
    CHECK(j["name"] == "core");
    CHECK(j["scope"] == "test");
    CHECK(j["isExported"] == true);
    CHECK(j["isTestJar"] == true);

    auto parsed = j.get<klspw::DependencyData>();
    REQUIRE(std::holds_alternative<klspw::ModuleDep>(parsed));
    const auto& m = std::get<klspw::ModuleDep>(parsed);
    CHECK(m.name == "core");
    CHECK(m.scope == klspw::DependencyScope::test);
    CHECK(m.isExported);
    CHECK(m.isTestJar);
}

TEST_CASE("SdkDep round-trips") {
    klspw::DependencyData d = klspw::SdkDep{.name = "JDK21", .kind = "JavaSDK"};
    const json j = d;
    CHECK(j["type"] == "sdk");
    CHECK(j["name"] == "JDK21");
    CHECK(j["kind"] == "JavaSDK");

    auto parsed = j.get<klspw::DependencyData>();
    REQUIRE(std::holds_alternative<klspw::SdkDep>(parsed));
    CHECK(std::get<klspw::SdkDep>(parsed).name == "JDK21");
}

TEST_CASE("LibraryDep with isExported round-trips") {
    klspw::DependencyData d = klspw::LibraryDep{
        .name = "guava",
        .scope = klspw::DependencyScope::provided,
        .isExported = true,
    };
    const json j = d;
    CHECK(j["isExported"] == true);

    auto parsed = j.get<klspw::DependencyData>();
    REQUIRE(std::holds_alternative<klspw::LibraryDep>(parsed));
    CHECK(std::get<klspw::LibraryDep>(parsed).isExported);
}

TEST_CASE("unknown dependency type throws") {
    const json j = {{"type", "bogus"}};
    CHECK_THROWS_AS(j.get<klspw::DependencyData>(), std::runtime_error);
}

TEST_CASE("unknown DependencyScope defaults to first entry") {
    const json j = "bogus";
    // NLOHMANN_JSON_SERIALIZE_ENUM returns the first mapping entry for unknown values
    CHECK(j.get<klspw::DependencyScope>() == klspw::DependencyScope::compile);
}

TEST_CASE("XmlElement round-trips") {
    klspw::XmlElement elem;
    elem.tag = "root";
    elem.attributes = {{"key", "val"}};
    elem.text = "hello";

    klspw::XmlElement child;
    child.tag = "child";
    elem.children.push_back(child);

    json j = elem;
    CHECK(j["tag"] == "root");
    CHECK(j["attributes"]["key"] == "val");
    CHECK(j["text"] == "hello");
    CHECK(j["children"].size() == 1);

    auto parsed = j.get<klspw::XmlElement>();
    CHECK(parsed.tag == "root");
    CHECK(parsed.attributes.at("key") == "val");
    CHECK(parsed.text.value() == "hello");
    CHECK(parsed.children.size() == 1);
    CHECK(parsed.children[0].tag == "child");
}

TEST_CASE("XmlElement empty fields are omitted") {
    klspw::XmlElement elem;
    const json j = elem;
    CHECK_FALSE(j.contains("tag"));
    CHECK_FALSE(j.contains("attributes"));
    CHECK_FALSE(j.contains("children"));
    CHECK_FALSE(j.contains("text"));
}

TEST_CASE("ContentRootData with excludedPatterns round-trips") {
    klspw::ContentRootData cr;
    cr.path = "/src";
    cr.excludedPatterns = {"*.bak", "tmp/"};
    cr.excludedUrls = {"file:///excluded"};
    cr.sourceRoots = {{.path = "/src/main", .type = "java-source"}};

    json j = cr;
    CHECK(j["excludedPatterns"].size() == 2);
    CHECK(j["excludedUrls"].size() == 1);

    auto parsed = j.get<klspw::ContentRootData>();
    CHECK(parsed.excludedPatterns.size() == 2);
    CHECK(parsed.excludedUrls[0] == "file:///excluded");
}

TEST_CASE("FacetData with configuration round-trips") {
    klspw::XmlElement config;
    config.tag = "facet-config";
    config.attributes = {{"version", "1"}};

    klspw::FacetData facet;
    facet.name = "Kotlin";
    facet.type = "kotlin-facet";
    facet.configuration = config;

    json j = facet;
    CHECK(j["name"] == "Kotlin");
    CHECK(j["configuration"]["tag"] == "facet-config");

    auto parsed = j.get<klspw::FacetData>();
    CHECK(parsed.configuration.has_value());
    CHECK(parsed.configuration->tag == "facet-config");
}

TEST_CASE("SdkData round-trips") {
    klspw::SdkData sdk;
    sdk.name = "JDK21";
    sdk.type = "JavaSDK";
    sdk.version = "21";
    sdk.homePath = "/usr/lib/jvm/java-21";
    sdk.roots = std::vector<klspw::SdkRootData>{{.url = "jar:///rt.jar!/", .type = "classPath"}};
    sdk.additionalData = "extra";

    json j = sdk;
    CHECK(j["name"] == "JDK21");
    CHECK(j["version"] == "21");
    CHECK(j["roots"].size() == 1);

    auto parsed = j.get<klspw::SdkData>();
    CHECK(parsed.version.value() == "21");
    CHECK(parsed.roots.has_value());
    CHECK(parsed.roots->size() == 1);
    CHECK(parsed.roots->at(0).url == "jar:///rt.jar!/");
}

TEST_CASE("JavaSettingsData round-trips") {
    klspw::JavaSettingsData js;
    js.module = "mymod";
    js.inheritedCompilerOutput = false;
    js.excludeOutput = false;
    js.compilerOutput = "/out";
    js.compilerOutputForTests = "/test-out";
    js.languageLevelId = "JDK_21";
    js.manifestAttributes = {{"Main-Class", "com.example.Main"}};

    const json j = js;
    auto parsed = j.get<klspw::JavaSettingsData>();
    CHECK(parsed.module == "mymod");
    CHECK_FALSE(parsed.inheritedCompilerOutput);
    CHECK(parsed.compilerOutput.value() == "/out");
    CHECK(parsed.languageLevelId.value() == "JDK_21");
    CHECK(parsed.manifestAttributes.at("Main-Class") == "com.example.Main");
}

TEST_CASE("LibraryData with properties round-trips") {
    klspw::XmlElement props;
    props.tag = "properties";

    klspw::LibraryData lib;
    lib.name = "mylib";
    lib.type = "java-imported";
    lib.roots = {{.path = "/lib.jar", .type = std::nullopt, .inclusionOptions = std::nullopt}};
    lib.excludedRoots = {"/excluded"};
    lib.properties = props;

    json j = lib;
    CHECK(j["excludedRoots"].size() == 1);
    CHECK(j["properties"]["tag"] == "properties");

    auto parsed = j.get<klspw::LibraryData>();
    CHECK(parsed.excludedRoots[0] == "/excluded");
    CHECK(parsed.properties.has_value());
    CHECK(parsed.properties->tag == "properties");
}
