#pragma once

/// Workspace model matching the kotlin-lsp workspace.json schema.
/// Source: github.com/Kotlin/kotlin-lsp workspace-import/src/com/jetbrains/ls/imports/json/model.kt
///
/// JSON field names are camelCase (matching kotlin-lsp). C++ field names are
/// snake_case. glz::camel_case auto-converts at compile time.
///
/// DependencyData uses glaze tagged variant with "type" discriminator.
/// See work/reports/glaze_tagged_variant_limitation.md for why variant
/// alternatives keep camelCase C++ field names.

#include "common.hpp"
#include "describe.hpp"
#include "files.hpp"
#include "ranges.hpp"

namespace klspw {

enum class DependencyScope : uint8_t { compile, test, runtime, provided };

/// A source or resource folder within a ContentRootData.
/// type values: "java-source", "java-test", "java-resource", "java-test-resource".
/// Despite the "java-" prefix, Kotlin sources also use "java-source"/"java-test".
struct SourceRootData {
    string path; ///< Absolute path or workspace-relative path.
    string type; ///< "java-source", "java-test", "java-resource", "java-test-resource".

    void describe(DescribeContext& ctx) const { ctx.add(format("      {} [{}]", path, type)); }
};

/// Root directory containing module source code and resources.
/// Paths may use special prefixes for portability: <WORKSPACE>/, <MAVEN_REPO>/, <HOME>/.
/// kotlin-lsp's toAbsolutePath() resolves these prefixes during import.
struct ContentRootData {
    string path; ///< Project/module root directory.
    vector<SourceRootData> source_roots; ///< Source and resource folders within this root.
    strings excluded_patterns; ///< Ant-style patterns to exclude (e.g., "**/target/**").
    strings excluded_urls; ///< URL-based exclusions (rarely used in practice).

    void describe(DescribeContext& ctx) const {
        ctx.add(format("    root: {} ({} source root(s))", path, source_roots.size()));
        ctx.describe(source_roots);
    }
};

/// Variant alternatives keep camelCase field names because glaze's tagged variant
/// dispatch doesn't apply rename_key/camel_case/modify during reading, and explicit
/// glz::object metadata breaks the tag dispatch mechanism entirely.
/// See work/reports/glaze_tagged_variant_limitation.md for details.

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
    string kind; ///< SDK kind (e.g., "jdk").
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

// --- Glaze metadata (between namespace blocks for visibility) ---

// Variant meta: tagged dispatch by "type" field.
template <> struct glz::meta<klspw::DependencyScope> {
    using enum klspw::DependencyScope;
    static constexpr auto value = enumerate("compile", compile, "test", test, "runtime", runtime, "provided", provided);
};

template <> struct glz::meta<klspw::DependencyData> {
    static constexpr std::string_view tag = "type";
    static constexpr auto ids = std::array{"module", "library", "sdk", "inheritedSdk", "moduleSource"};
};

// Non-variant types: camel_case auto-converts snake_case fields to camelCase JSON keys.
template <> struct glz::meta<klspw::ContentRootData> : glz::camel_case {};
template <> struct glz::meta<klspw::SourceRootData> : glz::camel_case {};

namespace klspw {

/// Recursive XML structure used by FacetData.configuration and LibraryData.properties.
/// Rarely populated in practice; preserved for lossless round-trip.
struct XmlElement {
    string tag; ///< XML element name (e.g., "configuration").
    string_map<string> attributes; ///< Element attributes.
    vector<XmlElement> children; ///< Nested child elements.
    opt_string text; ///< Element text content, if any.
};

/// Framework-specific configuration (Spring, JPA, etc.).
struct FacetData {
    string name; ///< Facet identifier (e.g., "Spring", "Kotlin").
    string type; ///< Framework type ID.
    optional<XmlElement> configuration; ///< XML configuration tree, or nullopt.
};

/// A project module (Gradle subproject, Maven module).
/// name must be unique across the workspace.
/// type defaults to "JAVA_MODULE" in kotlin-lsp when null.
/// dependencies must include InheritedSdk + ModuleSource for kotlin-lsp to work correctly.
struct ModuleData {
    string name; ///< Unique module identifier.
    string type = "JAVA_MODULE"; ///< Module type. Default matches kotlin-lsp's default.
    vector<DependencyData> dependencies; ///< Libraries, modules, SDKs this module depends on.
    vector<ContentRootData> content_roots; ///< Root directories containing source code.
    vector<FacetData> facets; ///< Framework facets (Spring, JPA). Usually empty.

    void describe(DescribeContext& ctx) const {
        const auto is_dep = [](const auto& d) { return std::holds_alternative<LibraryDep>(d); };
        const auto deps_count = r::count_if(dependencies, is_dep);
        ctx.add(format("  {} ({} deps, {} content root(s))", name, deps_count, content_roots.size()));
        if (ctx.verbose()) {
            ctx.describe(content_roots);
        }
    }
};

/// A single JAR or directory within a library's classpath.
struct LibraryRootData {
    string path; ///< JAR file or directory path.
    string type = "CLASSES"; ///< "CLASSES" (default), "SOURCES", or "JAVADOC".
    string inclusion_options =
        "root_itself"; ///< "root_itself", "archives_under_root", "archives_under_root_recursively".

    void describe(DescribeContext& ctx) const { ctx.add(format("    {}  [{}]", ctx.format_path(path), type)); }
};

/// An external library (jar dependency).
/// name must match exactly in LibraryDep references.
/// level defaults to "project" (matching kotlin-lsp's default).
/// module is nullable -- written as null when not set.
struct LibraryData {
    string name; ///< Unique library identifier (e.g., "kotlin-stdlib-2.0.0").
    string level = "project"; ///< "project" (shared), "module" (scoped), or "global". Default: "project".
    opt_string module; ///< Module name if level is "module". Null if not applicable.
    opt_string type; ///< Library classification (e.g., "java-imported"). Required by kotlin-lsp.
    vector<LibraryRootData> roots; ///< Classpath entries (CLASSES, SOURCES, JAVADOC jars).
    strings excluded_roots; ///< Paths to exclude from classpath.
    optional<XmlElement> properties; ///< Maven coordinates or other metadata as XML.

    /// Libraries with >1 root have at least one SOURCES entry (CLASSES is always first).
    bool with_sources() const { return roots.size() > 1; }

    void describe(DescribeContext& ctx) const {
        ctx.add(format("  {} ({} root(s))", name, roots.size()));
        if (ctx.verbose()) {
            ctx.describe(roots);
        }
    }
};

/// A single classpath entry within an SDK.
struct SdkRootData {
    string url; ///< SDK classpath entry (JRT URL, JAR path, or directory).
    string type; ///< "CLASSES", "SOURCES", "JAVADOC", or "classPath".
};

/// SDK definition (JDK, Android SDK, etc.).
/// Either roots or home_path should be provided.
struct SdkData {
    string name; ///< SDK identifier (e.g., "17", "corretto-17").
    string type; ///< SDK classification (e.g., "JavaSDK").
    opt_string version; ///< SDK version string (e.g., "17.0.5").
    opt_string home_path; ///< SDK installation directory.
    optional<vector<SdkRootData>> roots; ///< Explicit classpath entries. Null = auto-calculate from home_path.
    string additional_data; ///< Extra SDK metadata (JSON string or empty).
};

/// References configuration files (e.g., kotlinc-extension.xml).
struct ConfigFileItemData {
    string id; ///< Configuration identifier (e.g., "kotlinc-extension").
    string url; ///< File location.
};

/// Kotlin compiler and module settings. One entry per Kotlin module.
/// module field must match a ModuleData.name.
/// compiler_arguments format: J{"jvmTarget":"21"} (JSON-in-string, prefixed with 'J').
/// pure_kotlin_source_folders: source dirs containing only .kt files (no .java).
struct KotlinSettingsData {
    string name; ///< Settings identifier (typically "Kotlin").
    strings source_roots; ///< Kotlin source directories.
    vector<ConfigFileItemData> config_file_items; ///< Referenced config files.
    string module; ///< Associated module name (must match ModuleData.name).
    bool use_project_settings = false; ///< Use project-level Kotlin settings.
    strings implemented_module_names; ///< Platform implementations for multiplatform.
    strings depends_on_module_names; ///< Source set dependencies for multiplatform.
    strings additional_visible_module_names; ///< Cross-source-set visibility (sorted for deterministic output).
    opt_string production_output_path; ///< Compiled output directory.
    opt_string test_output_path; ///< Test output directory.
    strings source_set_names; ///< Gradle source set names.
    bool is_test_module = false; ///< Whether this is a test module.
    string external_project_id; ///< Maven/Gradle project ID (e.g., ":mymod:unspecified").
    bool is_hmpp_enabled = true; ///< Hierarchical multiplatform enabled.
    strings pure_kotlin_source_folders; ///< Dirs with only .kt files (no .java).
    string kind = "default"; ///< "default", "source_set_holder", etc.
    opt_string compiler_arguments; ///< J-prefixed JSON: J{"jvmTarget":"21"}.
    opt_string additional_arguments; ///< Extra compiler flags (space-separated).
    opt_string script_templates; ///< Script template class names.
    opt_string script_templates_classpath; ///< Classpath for script templates.
    bool copy_js_library_files = false; ///< Copy JS library files to output.
    opt_string output_directory_for_js_library_files; ///< JS library output directory.
    opt_string target_platform; ///< "JVM", "JS", "Native", or null (inherits JVM).
    strings external_system_run_tasks; ///< Build system tasks to run.
    int version = 5; ///< Settings schema version.
    bool flush_needed = false; ///< Whether settings refresh is needed.

    void describe(DescribeContext& ctx) const {
        ctx.add(format("  module={}, {} source root(s), {} pure-kotlin folder(s)",
            module,
            source_roots.size(),
            pure_kotlin_source_folders.size()));
    }
};

/// Java compiler and output settings per module.
/// module field must match a ModuleData.name.
struct JavaSettingsData {
    string module; ///< Associated module name (must match ModuleData.name).
    bool inherited_compiler_output = true; ///< Use project-level output directory.
    bool exclude_output = true; ///< Exclude output folder from IDE indexing.
    opt_string compiler_output; ///< Compiled .class output directory.
    opt_string compiler_output_for_tests; ///< Test .class output directory.
    opt_string language_level_id; ///< Java version: "JDK_17", "JDK_21", etc.
    string_map<string> manifest_attributes; ///< MANIFEST.MF attributes.
};

/// Root container for the entire workspace.json.
/// All five lists default to empty, so a minimal valid file is just {}.
struct WorkspaceData {
    vector<ModuleData> modules; ///< All project modules.
    vector<LibraryData> libraries; ///< External dependencies (jars).
    vector<SdkData> sdks; ///< SDK definitions (JDK, etc.).
    vector<KotlinSettingsData> kotlin_settings; ///< Kotlin compiler settings per module.
    vector<JavaSettingsData> java_settings; ///< Java compiler settings per module.

    /// Merge another WorkspaceData into this one.
    /// Modules, kotlin_settings, java_settings, and sdks are appended.
    /// Libraries are deduplicated by name (first occurrence wins).
    void merge(WorkspaceData other) {
        modules.append_range(std::move(other.modules));
        kotlin_settings.append_range(std::move(other.kotlin_settings));
        java_settings.append_range(std::move(other.java_settings));
        sdks.append_range(std::move(other.sdks));
        libraries.append_range(std::move(other.libraries));
        libraries = std::move(libraries) | unique_by(&LibraryData::name);
    }

    /// Serialize to pretty-printed JSON matching the kotlin-lsp workspace.json schema.
    string to_json() const {
        auto json = glz::write<ws_write_opts>(*this);
        require(json.has_value(), "Failed to serialize workspace JSON");
        return std::move(json).value();
    }

    /// Deserialize from a JSON string. Ignores unknown keys for forward compatibility.
    static WorkspaceData from_json(string_view json_str) {
        WorkspaceData ws;
        const auto ec = glz::read<ws_read_opts>(ws, json_str);
        require(!ec, "Failed to parse workspace JSON: {}", [&] { return glz::format_error(ec, json_str); });
        return ws;
    }

    /// Write workspace.json to a file.
    void save_json_file(const fs::path& path) const { write_file(path, to_json()); }

    /// Read workspace.json from a file.
    static WorkspaceData load_json_file(const fs::path& path) { return from_json(read_file(path)); }

    void describe(DescribeContext& ctx) const {
        ctx.describe_section(format("Modules ({}):", modules.size()), modules);
        ctx.describe_section(format("Libraries ({}):", libraries.size()), libraries);
        ctx.flush_stripped_prefixes();
        ctx.describe_section(format("Kotlin settings ({}):", kotlin_settings.size()), kotlin_settings);
    }
};

} // namespace klspw

// camel_case for all remaining types with snake_case fields.
template <> struct glz::meta<klspw::ModuleData> : glz::camel_case {};
template <> struct glz::meta<klspw::LibraryRootData> : glz::camel_case {};
template <> struct glz::meta<klspw::LibraryData> : glz::camel_case {};
template <> struct glz::meta<klspw::SdkData> : glz::camel_case {};
template <> struct glz::meta<klspw::KotlinSettingsData> : glz::camel_case {};
template <> struct glz::meta<klspw::JavaSettingsData> : glz::camel_case {};
template <> struct glz::meta<klspw::WorkspaceData> : glz::camel_case {};
