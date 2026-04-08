#pragma once

/// Workspace model matching the kotlin-lsp workspace.json schema.
/// Reference: github.com/Kotlin/kotlin-lsp/workspace-import/src/com/jetbrains/ls/imports/json/model.kt
///
/// kotlin-lsp's JsonWorkspaceImporter reads workspace.json and converts it to
/// IntelliJ EntityStorage (modules, libraries, SDKs, Kotlin/Java settings).
/// The JSON importer is tried first in the import chain, so generating a valid
/// workspace.json is the fastest path to a working kotlin-lsp workspace.
///
/// Import order in kotlin-lsp: JSON → Maven → Gradle → JPS → Light.
/// importWorkspaceData() processes: SDKs → Libraries → Modules → KotlinSettings → JavaSettings.
/// Module dependencies reference libraries/SDKs by exact name match.
///
/// Each type owns its serialization via to_json()/from_json() methods.
/// ADL free functions delegate to these methods for nlohmann/json integration.

#include <map>
#include <set>

#include "common.hpp"
#include "json.hpp"

namespace klspw {

// --- DependencyScope ---

/// Maps to DependencyDataScope enum in model.kt.
/// Determines when a dependency is available: compile-time, test-only, runtime-only, or provided.
/// JSON "type" discriminator values: "compile", "test", "runtime", "provided".
enum class DependencyScope : uint8_t { compile, test, runtime, provided };

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
NLOHMANN_JSON_SERIALIZE_ENUM(DependencyScope, {
    {DependencyScope::compile, "compile"},
    {DependencyScope::test, "test"},
    {DependencyScope::runtime, "runtime"},
    {DependencyScope::provided, "provided"},
})

// --- SourceRootData ---

/// A source or resource folder within a ContentRootData.
/// Maps to SourceRootData in model.kt.
/// type values: "java-source", "java-test", "java-resource", "java-test-resource".
/// Despite the "java-" prefix, Kotlin sources also use "java-source"/"java-test".

struct SourceRootData {
    string path;
    string type;

    json to_json() const { return {{"path", path}, {"type", type}}; }

    static SourceRootData from_json(const json& j) {
        return {.path = read<string>(j, "path"), .type = read<string>(j, "type")};
    }
};

// --- ContentRootData ---

/// Root directory containing module source code and resources.
/// Maps to ContentRootData in model.kt.
/// path is the project/module root directory (absolute).
/// sourceRoots list the actual source folders within this root.

struct ContentRootData {
    string path;
    strings excludedPatterns;
    strings excludedUrls;
    vector<SourceRootData> sourceRoots;

    json to_json() const {
        json j = json::object();
        write_field(j, "path", path);
        write_field(j, "sourceRoots", sourceRoots);
        write_opt(j, "excludedPatterns", excludedPatterns);
        write_opt(j, "excludedUrls", excludedUrls);
        return j;
    }

    static ContentRootData from_json(const json& j) {
        return {
            .path = read<string>(j, "path"),
            .excludedPatterns = read_or<strings>(j, "excludedPatterns", {}),
            .excludedUrls = read_or<strings>(j, "excludedUrls", {}),
            .sourceRoots = read_or<vector<SourceRootData>>(j, "sourceRoots", {}),
        };
    }
};

// --- Dependency variant types ---

/// Maps to DependencyData sealed class in model.kt.
/// Five variants discriminated by "type" field:
///   "module"       → ModuleDep (inter-module dependency)
///   "library"      → LibraryDep (external jar, name must match a LibraryData.name)
///   "sdk"          → SdkDep (explicit SDK reference)
///   "inheritedSdk" → InheritedSdk (use project default SDK)
///   "moduleSource" → ModuleSource (dependency on module's own compiled output)

struct ModuleDep {
    string name;
    DependencyScope scope = DependencyScope::compile;
    bool isExported = false;
    bool isTestJar = false;

    json to_json() const {
        json j = {{"type", "module"}, {"name", name}, {"scope", scope}};
        write_true(j, "isExported", isExported);
        write_true(j, "isTestJar", isTestJar);
        return j;
    }

    static ModuleDep from_json(const json& j) {
        return {
            .name = read<string>(j, "name"),
            .scope = read<DependencyScope>(j, "scope"),
            .isExported = read_or(j, "isExported", false),
            .isTestJar = read_or(j, "isTestJar", false),
        };
    }
};

struct LibraryDep {
    string name;
    DependencyScope scope = DependencyScope::compile;
    bool isExported = false;

    json to_json() const {
        json j = {{"type", "library"}, {"name", name}, {"scope", scope}};
        write_true(j, "isExported", isExported);
        return j;
    }

    static LibraryDep from_json(const json& j) {
        return {
            .name = read<string>(j, "name"),
            .scope = read<DependencyScope>(j, "scope"),
            .isExported = read_or(j, "isExported", false),
        };
    }
};

struct SdkDep {
    string name;
    string kind;

    json to_json() const { return {{"type", "sdk"}, {"name", name}, {"kind", kind}}; }

    static SdkDep from_json(const json& j) {
        return {.name = read<string>(j, "name"), .kind = read<string>(j, "kind")};
    }
};

struct InheritedSdk {
    static json to_json() { return {{"type", "inheritedSdk"}}; }
    static InheritedSdk from_json(const json& /*j*/) { return {}; }
};

struct ModuleSource {
    static json to_json() { return {{"type", "moduleSource"}}; }
    static ModuleSource from_json(const json& /*j*/) { return {}; }
};

/// Sealed dependency sum type, discriminated by "type" field in JSON.
/// Mirrors kotlin-lsp's DependencyData sealed class with @SerialName annotations.
/// ModuleSource and InheritedSdk should be present in every module's dependencies.
using DependencyData = variant<ModuleDep, LibraryDep, SdkDep, InheritedSdk, ModuleSource>;

inline void to_json(json& j, const DependencyData& d) {
    visit([&j](const auto& dep) { j = dep.to_json(); }, d);
}

inline void from_json(const json& j, DependencyData& d) {
    const auto& t = read<string>(j, "type");
    if (t == "module") {
        d = ModuleDep::from_json(j);
    } else if (t == "library") {
        d = LibraryDep::from_json(j);
    } else if (t == "sdk") {
        d = SdkDep::from_json(j);
    } else if (t == "inheritedSdk") {
        d = InheritedSdk::from_json(j);
    } else if (t == "moduleSource") {
        d = ModuleSource::from_json(j);
    } else {
        throw runtime_error(format("Unknown dependency type: {}", t));
    }
}

// --- XmlElement ---

/// Recursive XML structure used by FacetData.configuration and LibraryData.properties.
/// Rarely populated in practice; preserved for lossless round-trip.
struct XmlElement {
    string tag;
    map<string, string> attributes;
    vector<XmlElement> children;
    opt_string text;

    json to_json() const {
        json j = json::object();
        if (!tag.empty()) {
            j["tag"] = tag;
        }
        if (!attributes.empty()) {
            j["attributes"] = attributes;
        }
        if (!children.empty()) {
            auto arr = json::array();
            for (const auto& c : children) {
                arr.push_back(c.to_json());
            }
            j["children"] = std::move(arr);
        }
        if (text) {
            j["text"] = *text;
        }
        return j;
    }

    static XmlElement from_json(const json& j) {
        XmlElement e;
        e.tag = read_or<string>(j, "tag", "");
        e.attributes = read_or<map<string, string>>(j, "attributes", {});
        e.children = read_or<vector<XmlElement>>(j, "children", {});
        e.text = read_opt<string>(j, "text");
        return e;
    }
};

// --- FacetData ---

struct FacetData {
    string name;
    string type;
    optional<XmlElement> configuration;

    json to_json() const {
        json j = {{"name", name}, {"type", type}};
        if (configuration) {
            j["configuration"] = configuration->to_json();
        }
        return j;
    }

    static FacetData from_json(const json& j) {
        FacetData d;
        d.name = read<string>(j, "name");
        d.type = read<string>(j, "type");
        d.configuration = read_opt<XmlElement>(j, "configuration");
        return d;
    }
};

// --- ModuleData ---

/// A project module (Gradle subproject, Maven module).
/// Maps to ModuleData in model.kt.
/// name must be unique across the workspace.
/// type is "JAVA_MODULE" for Kotlin/Java modules (null in root-workspace files).
/// dependencies must include InheritedSdk + ModuleSource for kotlin-lsp to work.

struct ModuleData {
    string name;
    opt_string type; ///< null in root-workspace files, "JAVA_MODULE" in proj-workspace files.
    vector<DependencyData> dependencies;
    vector<ContentRootData> contentRoots;
    vector<FacetData> facets; ///< Always empty in observed workspaces; preserved for round-trip.

    json to_json() const {
        json j = json::object();
        write_field(j, "name", name);
        write_opt(j, "type", type);
        write_opt(j, "dependencies", dependencies);
        write_opt(j, "contentRoots", contentRoots);
        write_opt(j, "facets", facets);
        return j;
    }

    static ModuleData from_json(const json& j) {
        ModuleData d;
        d.name = read<string>(j, "name");
        d.type = read_opt<string>(j, "type");
        d.dependencies = read_or<vector<DependencyData>>(j, "dependencies", {});
        d.contentRoots = read_or<vector<ContentRootData>>(j, "contentRoots", {});
        d.facets = read_or<vector<FacetData>>(j, "facets", {});
        return d;
    }
};

// --- LibraryRootData ---

/// A single JAR or directory within a library's classpath.
/// Maps to LibraryRootData in model.kt.
/// type: "CLASSES" (compiled code, default), "SOURCES" (source jars), "JAVADOC".
/// inclusionOptions: "root_itself" (single jar), "archives_under_root" (dir of jars).

struct LibraryRootData {
    string path;
    opt_string type;             ///< null = CLASSES (implicit default), "SOURCES", "JAVADOC".
    opt_string inclusionOptions; ///< "root_itself", "archives_under_root", "archives_under_root_recursively".

    json to_json() const {
        json j = {{"path", path}};
        write_opt(j, "type", type);
        write_opt(j, "inclusionOptions", inclusionOptions);
        return j;
    }

    static LibraryRootData from_json(const json& j) {
        LibraryRootData d;
        d.path = read<string>(j, "path");
        d.type = read_opt<string>(j, "type");
        d.inclusionOptions = read_opt<string>(j, "inclusionOptions");
        return d;
    }
};

// --- LibraryData ---

/// An external library (jar dependency).
/// Maps to LibraryData in model.kt.
/// name must match exactly in DependencyData.Library references.
/// level: "project" (shared across modules) or "module" (scoped to one module).
/// type: null is written as explicit JSON null (kotlin-lsp expects the key present).

struct LibraryData {
    string name;
    opt_string level; ///< "project" in proj-workspace files; absent in root-workspace files.
    opt_string module;
    opt_string type; ///< Always written as explicit null when absent (kotlin-lsp expects the key).
    vector<LibraryRootData> roots;
    strings excludedRoots;
    optional<XmlElement> properties;

    json to_json() const {
        json j = json::object();
        write_field(j, "name", name);
        write_opt(j, "level", level);
        write_opt(j, "module", module);
        write_nullable(j, "type", type);
        write_field(j, "roots", roots);
        write_opt(j, "excludedRoots", excludedRoots);
        if (properties) {
            j["properties"] = properties->to_json();
        }
        return j;
    }

    static LibraryData from_json(const json& j) {
        LibraryData d;
        d.name = read<string>(j, "name");
        d.level = read_opt<string>(j, "level");
        d.module = read_opt<string>(j, "module");
        d.type = read_opt<string>(j, "type");
        d.roots = read<vector<LibraryRootData>>(j, "roots");
        d.excludedRoots = read_or<strings>(j, "excludedRoots", {});
        d.properties = read_opt<XmlElement>(j, "properties");
        return d;
    }
};

// --- SdkRootData ---

struct SdkRootData {
    string url;
    string type;

    json to_json() const { return {{"url", url}, {"type", type}}; }

    static SdkRootData from_json(const json& j) {
        return {.url = read<string>(j, "url"), .type = read<string>(j, "type")};
    }
};

// --- SdkData ---

/// SDK definition (JDK, Android SDK, etc.).
/// Maps to SdkData in model.kt.
/// Either roots or homePath must be provided.
/// For Java SDKs with homePath, kotlin-lsp can auto-calculate class roots.

struct SdkData {
    string name;
    string type;
    opt_string version;
    opt_string homePath;
    optional<vector<SdkRootData>> roots; ///< Omitted when kotlin-lsp can calculate from homePath.
    string additionalData;

    json to_json() const {
        json j = json::object();
        write_field(j, "name", name);
        write_field(j, "type", type);
        write_nullable(j, "version", version);
        write_nullable(j, "homePath", homePath);
        write_opt(j, "roots", roots);
        write_field(j, "additionalData", additionalData);
        return j;
    }

    static SdkData from_json(const json& j) {
        SdkData d;
        d.name = read<string>(j, "name");
        d.type = read<string>(j, "type");
        d.version = read_opt<string>(j, "version");
        d.homePath = read_opt<string>(j, "homePath");
        d.roots = read_opt<vector<SdkRootData>>(j, "roots");
        d.additionalData = read_or<string>(j, "additionalData", "");
        return d;
    }
};

// --- ConfigFileItemData ---

struct ConfigFileItemData {
    string id;
    string url;

    json to_json() const { return {{"id", id}, {"url", url}}; }

    static ConfigFileItemData from_json(const json& j) {
        return {.id = read<string>(j, "id"), .url = read<string>(j, "url")};
    }
};

// --- KotlinSettingsData ---

/// Kotlin compiler and module settings.
/// Maps to KotlinSettingsData in model.kt (~25 fields, inherent to upstream schema).
/// One entry per Kotlin module. module field must match a ModuleData.name.
/// compilerArguments format: J{"jvmTarget":"21"} (JSON-in-string, prefixed with 'J').
/// pureKotlinSourceFolders: source dirs containing only .kt files (no .java).
/// kind: "default" for regular modules, "source_set_holder" for multiplatform.
struct KotlinSettingsData {
    string name;
    strings sourceRoots;
    vector<ConfigFileItemData> configFileItems;
    string module;

    bool useProjectSettings = false;
    strings implementedModuleNames;
    strings dependsOnModuleNames;
    set<string> additionalVisibleModuleNames;
    opt_string productionOutputPath;
    opt_string testOutputPath;
    strings sourceSetNames;
    bool isTestModule = false;
    string externalProjectId;
    bool isHmppEnabled = true;

    strings pureKotlinSourceFolders;
    string kind = "default"; ///< "default", "source_set_holder", "compilation_and_source_set_holder".

    opt_string compilerArguments; ///< Format: J{"jvmTarget":"21"} (JSON-in-string, prefixed with 'J').
    opt_string additionalArguments;
    opt_string scriptTemplates;
    opt_string scriptTemplatesClasspath;
    bool copyJsLibraryFiles = false;
    opt_string outputDirectoryForJsLibraryFiles;

    opt_string targetPlatform;
    strings externalSystemRunTasks;
    int version = 5;
    bool flushNeeded = false;

    json to_json() const {
        json j = json::object();
        write_field(j, "name", name);
        write_field(j, "sourceRoots", sourceRoots);
        write_field(j, "configFileItems", configFileItems);
        write_field(j, "module", module);
        write_field(j, "useProjectSettings", useProjectSettings);
        write_field(j, "implementedModuleNames", implementedModuleNames);
        write_field(j, "dependsOnModuleNames", dependsOnModuleNames);
        write_field(j, "additionalVisibleModuleNames", additionalVisibleModuleNames);
        write_nullable(j, "productionOutputPath", productionOutputPath);
        write_nullable(j, "testOutputPath", testOutputPath);
        write_field(j, "sourceSetNames", sourceSetNames);
        write_field(j, "isTestModule", isTestModule);
        write_field(j, "externalProjectId", externalProjectId);
        write_field(j, "isHmppEnabled", isHmppEnabled);
        write_field(j, "pureKotlinSourceFolders", pureKotlinSourceFolders);
        write_field(j, "kind", kind);
        write_nullable(j, "compilerArguments", compilerArguments);
        write_field(j, "version", version);
        write_nullable(j, "additionalArguments", additionalArguments);
        write_nullable(j, "scriptTemplates", scriptTemplates);
        write_nullable(j, "scriptTemplatesClasspath", scriptTemplatesClasspath);
        write_true(j, "copyJsLibraryFiles", copyJsLibraryFiles);
        write_nullable(j, "outputDirectoryForJsLibraryFiles", outputDirectoryForJsLibraryFiles);
        write_nullable(j, "targetPlatform", targetPlatform);
        write_field(j, "externalSystemRunTasks", externalSystemRunTasks);
        write_field(j, "flushNeeded", flushNeeded);
        return j;
    }

    static KotlinSettingsData from_json(const json& j) {
        KotlinSettingsData d;
        d.name = read<string>(j, "name");
        d.sourceRoots = read<strings>(j, "sourceRoots");
        d.configFileItems = read_or<vector<ConfigFileItemData>>(j, "configFileItems", {});
        d.module = read<string>(j, "module");
        d.useProjectSettings = read_or(j, "useProjectSettings", false);
        d.implementedModuleNames = read_or<strings>(j, "implementedModuleNames", {});
        d.dependsOnModuleNames = read_or<strings>(j, "dependsOnModuleNames", {});
        d.additionalVisibleModuleNames = read_or<set<string>>(j, "additionalVisibleModuleNames", {});
        d.productionOutputPath = read_opt<string>(j, "productionOutputPath");
        d.testOutputPath = read_opt<string>(j, "testOutputPath");
        d.sourceSetNames = read_or<strings>(j, "sourceSetNames", {});
        d.isTestModule = read_or(j, "isTestModule", false);
        d.externalProjectId = read_or<string>(j, "externalProjectId", "");
        d.isHmppEnabled = read_or(j, "isHmppEnabled", true);
        d.pureKotlinSourceFolders = read_or<strings>(j, "pureKotlinSourceFolders", {});
        d.kind = read_or<string>(j, "kind", "default");
        d.compilerArguments = read_opt<string>(j, "compilerArguments");
        d.version = read_or(j, "version", 5);
        d.additionalArguments = read_opt<string>(j, "additionalArguments");
        d.scriptTemplates = read_opt<string>(j, "scriptTemplates");
        d.scriptTemplatesClasspath = read_opt<string>(j, "scriptTemplatesClasspath");
        d.copyJsLibraryFiles = read_or(j, "copyJsLibraryFiles", false);
        d.outputDirectoryForJsLibraryFiles = read_opt<string>(j, "outputDirectoryForJsLibraryFiles");
        d.targetPlatform = read_opt<string>(j, "targetPlatform");
        d.externalSystemRunTasks = read_or<strings>(j, "externalSystemRunTasks", {});
        d.flushNeeded = read_or(j, "flushNeeded", false);
        return d;
    }
};

// --- JavaSettingsData ---

/// Java compiler and output settings per module.
/// Maps to JavaSettingsData in model.kt.
/// module field must match a ModuleData.name.

struct JavaSettingsData {
    string module;
    bool inheritedCompilerOutput = true;
    bool excludeOutput = true;
    opt_string compilerOutput;
    opt_string compilerOutputForTests;
    opt_string languageLevelId;
    map<string, string> manifestAttributes;

    json to_json() const {
        json j = json::object();
        write_field(j, "module", module);
        write_field(j, "inheritedCompilerOutput", inheritedCompilerOutput);
        write_field(j, "excludeOutput", excludeOutput);
        write_nullable(j, "compilerOutput", compilerOutput);
        write_nullable(j, "compilerOutputForTests", compilerOutputForTests);
        write_nullable(j, "languageLevelId", languageLevelId);
        write_field(j, "manifestAttributes", manifestAttributes);
        return j;
    }

    static JavaSettingsData from_json(const json& j) {
        JavaSettingsData d;
        d.module = read<string>(j, "module");
        d.inheritedCompilerOutput = read_or(j, "inheritedCompilerOutput", true);
        d.excludeOutput = read_or(j, "excludeOutput", true);
        d.compilerOutput = read_opt<string>(j, "compilerOutput");
        d.compilerOutputForTests = read_opt<string>(j, "compilerOutputForTests");
        d.languageLevelId = read_opt<string>(j, "languageLevelId");
        d.manifestAttributes = read_or<map<string, string>>(j, "manifestAttributes", {});
        return d;
    }
};

// --- WorkspaceData ---

/// Root container for the entire workspace.json.
/// Maps to WorkspaceData in model.kt.
/// kotlin-lsp's importWorkspaceData() processes in order: sdks → libraries → modules → kotlinSettings → javaSettings.
/// Library names in module dependencies must match entries in the libraries list.

struct WorkspaceData {
    vector<ModuleData> modules;
    vector<LibraryData> libraries;
    vector<SdkData> sdks;
    vector<KotlinSettingsData> kotlinSettings;
    vector<JavaSettingsData> javaSettings;

    json to_json() const {
        json j = json::object();
        write_field(j, "modules", modules);
        write_field(j, "libraries", libraries);
        write_opt(j, "sdks", sdks);
        write_opt(j, "kotlinSettings", kotlinSettings);
        write_opt(j, "javaSettings", javaSettings);
        return j;
    }

    static WorkspaceData from_json(const json& j) {
        return {
            .modules = read_or<vector<ModuleData>>(j, "modules", {}),
            .libraries = read_or<vector<LibraryData>>(j, "libraries", {}),
            .sdks = read_or<vector<SdkData>>(j, "sdks", {}),
            .kotlinSettings = read_or<vector<KotlinSettingsData>>(j, "kotlinSettings", {}),
            .javaSettings = read_or<vector<JavaSettingsData>>(j, "javaSettings", {}),
        };
    }
};

} // namespace klspw
