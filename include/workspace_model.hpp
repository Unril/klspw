#pragma once

/// Workspace model matching the kotlin-lsp workspace.json schema.
/// Source: github.com/Kotlin/kotlin-lsp workspace-import/src/com/jetbrains/ls/imports/json/model.kt
///
/// kotlin-lsp's JsonWorkspaceImporter reads workspace.json and converts it to
/// IntelliJ EntityStorage. The JSON importer is tried first in the import chain
/// (JSON → Maven → Gradle → JPS → Light), so a pre-generated workspace.json
/// enables faster startup by skipping build-system invocation.
///
/// Import order in conversion.kt: SDKs → Libraries → Modules → KotlinSettings → JavaSettings.
/// This order is dependency-driven: SDKs and libraries must exist before modules reference them.
///
/// Types use glaze auto-reflection for JSON serialization.
/// DependencyData uses glaze tagged variant with "type" discriminator.
/// DependencyScope uses glaze enum serialization.

#include <spdlog/spdlog.h>

#include "common.hpp"

namespace klspw {

// --- DependencyScope ---

/// When a dependency is available: compile-time, test-only, runtime-only, or provided.
/// Maps to DependencyDataScope enum in model.kt.
///   compile:  available at compile, runtime, and test.
///   test:     available only during test compilation and execution.
///   runtime:  available at runtime but not compile-time.
///   provided: available at compile-time but not bundled at runtime.
enum class DependencyScope : uint8_t { compile, test, runtime, provided };

// --- SourceRootData ---

/// A source or resource folder within a ContentRootData.
/// type values: "java-source", "java-test", "java-resource", "java-test-resource".
/// Despite the "java-" prefix, Kotlin sources also use "java-source"/"java-test".
struct SourceRootData {
    string path; ///< Absolute path or <WORKSPACE>/-prefixed relative path.
    string type; ///< "java-source", "java-test", "java-resource", "java-test-resource".
};

// --- ContentRootData ---

/// Root directory containing module source code and resources.
/// Paths may use special prefixes for portability: <WORKSPACE>/, <MAVEN_REPO>/, <HOME>/.
/// kotlin-lsp's toAbsolutePath() resolves these prefixes during import.
struct ContentRootData {
    string path; ///< Project/module root directory.
    vector<SourceRootData> sourceRoots; ///< Source and resource folders within this root.
    strings excludedPatterns; ///< Ant-style patterns to exclude (e.g., "**/target/**").
    strings excludedUrls; ///< URL-based exclusions (rarely used in practice).
};

// --- Dependency variant types ---

/// Maps to DependencyData sealed class in model.kt.
/// Five variants discriminated by "type" field in JSON:
///   "module"       → ModuleDep (inter-module dependency)
///   "library"      → LibraryDep (external jar, name must match a LibraryData.name)
///   "sdk"          → SdkDep (explicit SDK reference by name and kind)
///   "inheritedSdk" → InheritedSdk (use project default SDK)
///   "moduleSource" → ModuleSource (dependency on module's own compiled output)

/// Inter-module dependency. name must match another ModuleData.name in the workspace.
struct ModuleDep {
    string name; ///< Target module name.
    DependencyScope scope = DependencyScope::compile; ///< When this dependency is available.
    bool isExported = false; ///< If true, transitive dependents see this dep.
    bool isTestJar = false; ///< Test artifact indicator.
    auto operator<=>(const ModuleDep&) const = default;
};

/// External library dependency. name must match a LibraryData.name exactly.
struct LibraryDep {
    string name; ///< Library name (must match LibraryData.name).
    DependencyScope scope = DependencyScope::compile; ///< When this dependency is available.
    bool isExported = false; ///< If true, transitive dependents see this dep.
    auto operator<=>(const LibraryDep&) const = default;
};

/// Explicit SDK dependency. Alternative to InheritedSdk for a specific SDK reference.
struct SdkDep {
    string name; ///< SDK name (e.g., "17").
    string kind; ///< SDK type (e.g., "JavaSDK").
    auto operator<=>(const SdkDep&) const = default;
};

/// Module uses the project's default/inherited SDK. No parameters needed.
struct InheritedSdk {
    auto operator<=>(const InheritedSdk&) const = default;
};

/// Dependency on module's own compiled output. Always present in module dependencies.
struct ModuleSource {
    auto operator<=>(const ModuleSource&) const = default;
};

/// Sealed dependency sum type. Glaze uses tagged variant with "type" discriminator.
/// Every module's dependencies should include at least InheritedSdk + ModuleSource.
using DependencyData = variant<ModuleDep, LibraryDep, SdkDep, InheritedSdk, ModuleSource>;

} // namespace klspw

// Glaze metadata for enum and tagged variant.
// Placed before types that contain DependencyData to guarantee
// visibility at template instantiation time.

template <> struct glz::meta<klspw::DependencyScope> {
    using enum klspw::DependencyScope;
    static constexpr auto value = enumerate("compile", compile, "test", test, "runtime", runtime, "provided", provided);
};

template <> struct glz::meta<klspw::DependencyData> {
    static constexpr std::string_view tag = "type";
    static constexpr auto ids = std::array{"module", "library", "sdk", "inheritedSdk", "moduleSource"};
};

namespace klspw {

// --- XmlElement ---

/// Recursive XML structure used by FacetData.configuration and LibraryData.properties.
/// Rarely populated in practice; preserved for lossless round-trip.
struct XmlElement {
    string tag; ///< XML element name (e.g., "configuration", "properties").
    map<string, string> attributes; ///< Element attributes (e.g., {"groupId": "org.jetbrains"}).
    vector<XmlElement> children; ///< Nested child elements.
    opt_string text; ///< Element text content, if any.
};

// --- FacetData ---

/// Framework-specific configuration (Spring, JPA, etc.).
/// configuration holds an XML tree; underlyingFacet (not modeled here) allows nesting.
struct FacetData {
    string name; ///< Facet identifier (e.g., "Spring", "Kotlin").
    string type; ///< Framework type ID.
    optional<XmlElement> configuration; ///< XML configuration tree, or nullopt.
};

// --- ModuleData ---

/// A project module (Gradle subproject, Maven module).
/// name must be unique across the workspace.
/// type defaults to "JAVA_MODULE" in kotlin-lsp when null.
/// dependencies must include InheritedSdk + ModuleSource for kotlin-lsp to work correctly.
struct ModuleData {
    string name; ///< Unique module identifier.
    opt_string type; ///< "JAVA_MODULE" for Kotlin/Java; null defaults to same.
    vector<DependencyData> dependencies; ///< Libraries, modules, SDKs this module depends on.
    vector<ContentRootData> contentRoots; ///< Root directories containing source code.
    vector<FacetData> facets; ///< Framework facets (Spring, JPA). Usually empty.
};

// --- LibraryRootData ---

/// A single JAR or directory within a library's classpath.
struct LibraryRootData {
    string path; ///< JAR file or directory path.
    opt_string type; ///< "CLASSES" (default), "SOURCES", or "JAVADOC".
    opt_string inclusionOptions; ///< "root_itself", "archives_under_root", "archives_under_root_recursively".
};

// --- LibraryData ---

/// An external library (jar dependency).
/// name must match exactly in LibraryDep references.
/// The importer reports unresolved dependencies (library refs with no matching entry)
/// and missing roots (paths that don't exist on disk) but does not block import.
struct LibraryData {
    string name; ///< Unique library identifier (e.g., "kotlin-stdlib-2.0.0").
    opt_string level; ///< "project" (shared) or "module" (scoped). Absent in root-workspace files.
    opt_string module; ///< Module name if level is "module".
    opt_string type; ///< Library classification (e.g., "repository"). Nullable.
    vector<LibraryRootData> roots; ///< Classpath entries (CLASSES, SOURCES, JAVADOC jars).
    strings excludedRoots; ///< Paths to exclude from classpath.
    optional<XmlElement> properties; ///< Maven coordinates or other metadata as XML.
};

// --- SdkRootData ---

/// A single classpath entry within an SDK.
/// url uses JRT format for JDK 9+ (jrt:///path/modules/java.base) or jar:// for older JDKs.
struct SdkRootData {
    string url; ///< SDK classpath entry (JRT URL, JAR path, or directory).
    string type; ///< "CLASSES", "SOURCES", "JAVADOC", or "classPath".
};

// --- SdkData ---

/// SDK definition (JDK, Android SDK, etc.).
/// Either roots or homePath should be provided.
/// For Java SDKs with homePath, kotlin-lsp auto-calculates class roots via JavaSdkImpl.findClasses().
struct SdkData {
    string name; ///< SDK identifier (e.g., "17", "corretto-17").
    string type; ///< SDK classification (e.g., "JavaSDK").
    opt_string version; ///< SDK version string (e.g., "17.0.5"). Nullable.
    opt_string homePath; ///< SDK installation directory. Nullable.
    optional<vector<SdkRootData>> roots; ///< Explicit classpath entries. Null = auto-calculate from homePath.
    string additionalData; ///< Extra SDK metadata (JSON string or empty).
};

// --- ConfigFileItemData ---

/// References configuration files (e.g., kotlinc-extension.xml).
struct ConfigFileItemData {
    string id; ///< Configuration identifier (e.g., "kotlinc-extension").
    string url; ///< File location (e.g., "<WORKSPACE>/.idea/kotlinc.xml").
};

// --- KotlinSettingsData ---

/// Kotlin compiler and module settings. One entry per Kotlin module.
/// module field must match a ModuleData.name.
/// compilerArguments format: J{"jvmTarget":"21"} (JSON-in-string, prefixed with 'J').
/// pureKotlinSourceFolders: source dirs containing only .kt files (no .java).
/// kind: "default" for regular modules, "source_set_holder" for multiplatform.
struct KotlinSettingsData {
    string name; ///< Settings identifier (typically "Kotlin").
    strings sourceRoots; ///< Kotlin source directories.
    vector<ConfigFileItemData> configFileItems; ///< Referenced config files.
    string module; ///< Associated module name (must match ModuleData.name).
    bool useProjectSettings = false; ///< Use project-level Kotlin settings.
    strings implementedModuleNames; ///< Platform implementations for multiplatform.
    strings dependsOnModuleNames; ///< Source set dependencies for multiplatform.
    set<string> additionalVisibleModuleNames; ///< Cross-source-set visibility.
    opt_string productionOutputPath; ///< Compiled output directory. Nullable.
    opt_string testOutputPath; ///< Test output directory. Nullable.
    strings sourceSetNames; ///< Gradle source set names.
    bool isTestModule = false; ///< Whether this is a test module.
    string externalProjectId; ///< Maven/Gradle project ID (e.g., ":mymod:unspecified").
    bool isHmppEnabled = true; ///< Hierarchical multiplatform enabled.
    strings pureKotlinSourceFolders; ///< Dirs with only .kt files (no .java).
    string kind = "default"; ///< "default", "source_set_holder", "compilation_and_source_set_holder".
    opt_string compilerArguments; ///< J-prefixed JSON: J{"jvmTarget":"21"}. Nullable.
    opt_string additionalArguments; ///< Extra compiler flags (space-separated). Nullable.
    opt_string scriptTemplates; ///< Script template class names. Nullable.
    opt_string scriptTemplatesClasspath; ///< Classpath for script templates. Nullable.
    bool copyJsLibraryFiles = false; ///< Copy JS library files to output.
    opt_string outputDirectoryForJsLibraryFiles; ///< JS library output directory. Nullable.
    opt_string targetPlatform; ///< "JVM", "JS", "Native", or null (inherits JVM).
    strings externalSystemRunTasks; ///< Build system tasks to run.
    int version = 5; ///< Settings schema version.
    bool flushNeeded = false; ///< Whether settings refresh is needed.
};

// --- JavaSettingsData ---

/// Java compiler and output settings per module.
/// module field must match a ModuleData.name.
struct JavaSettingsData {
    string module; ///< Associated module name (must match ModuleData.name).
    bool inheritedCompilerOutput = true; ///< Use project-level output directory.
    bool excludeOutput = true; ///< Exclude output folder from IDE indexing.
    opt_string compilerOutput; ///< Compiled .class output directory. Nullable.
    opt_string compilerOutputForTests; ///< Test .class output directory. Nullable.
    opt_string languageLevelId; ///< Java version: "JDK_17", "JDK_21", etc. Nullable.
    map<string, string> manifestAttributes; ///< MANIFEST.MF attributes (e.g., {"Main-Class": "..."}).
};

// --- WorkspaceData ---

/// Root container for the entire workspace.json.
/// All five lists default to empty in kotlin-lsp, so a minimal valid file is just {}.
/// The importer processes in order: SDKs → Libraries → Modules → KotlinSettings → JavaSettings.
/// Library names in module dependencies must match entries in the libraries list.
struct WorkspaceData {
    vector<ModuleData> modules; ///< All project modules.
    vector<LibraryData> libraries; ///< External dependencies (jars).
    vector<SdkData> sdks; ///< SDK definitions (JDK, etc.).
    vector<KotlinSettingsData> kotlinSettings; ///< Kotlin compiler settings per module.
    vector<JavaSettingsData> javaSettings; ///< Java compiler settings per module.

    /// Merge another WorkspaceData into this one.
    /// Modules, kotlinSettings, javaSettings, and sdks are appended.
    /// Libraries are deduplicated by name (first occurrence wins).
    void merge(WorkspaceData other) {
        modules.append_range(std::move(other.modules));
        kotlinSettings.append_range(std::move(other.kotlinSettings));
        javaSettings.append_range(std::move(other.javaSettings));
        sdks.append_range(std::move(other.sdks));

        string_set existing_names;
        existing_names.reserve(libraries.size());
        for (const auto& lib : libraries) {
            existing_names.insert(lib.name);
        }
        for (auto& lib : other.libraries) {
            if (existing_names.insert(lib.name).second) {
                libraries.push_back(std::move(lib));
            }
        }
    }

    /// Log a human-readable summary of this workspace via spdlog.
    void log_summary() const {
        spdlog::info("Modules ({}):", modules.size());
        for (const auto& mod : modules) {
            const auto lib_deps =
                r::count_if(mod.dependencies, [](const auto& d) { return std::holds_alternative<LibraryDep>(d); });
            spdlog::info("  {} ({} deps, {} content root(s))", mod.name, lib_deps, mod.contentRoots.size());
            for (const auto& cr : mod.contentRoots) {
                spdlog::info("    root: {} ({} source root(s))", cr.path, cr.sourceRoots.size());
            }
        }

        spdlog::info("Libraries ({}):", libraries.size());
        for (const auto& lib : libraries) {
            spdlog::info("  {} ({} root(s))", lib.name, lib.roots.size());
        }

        spdlog::info("Kotlin settings ({}):", kotlinSettings.size());
        for (const auto& ks : kotlinSettings) {
            spdlog::info("  module={}, {} source root(s), {} pure-kotlin folder(s)", ks.module, ks.sourceRoots.size(),
                         ks.pureKotlinSourceFolders.size());
        }
    }
};

} // namespace klspw
