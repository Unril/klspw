#pragma once

/// Workspace model matching the kotlin-lsp workspace.json schema.
/// Source: github.com/Kotlin/kotlin-lsp workspace-import/src/com/jetbrains/ls/imports/json/model.kt
///
/// JSON field names are camelCase (matching kotlin-lsp). C++ field names are
/// snake_case. glz::camel_case auto-converts at compile time.
///
/// DependencyData uses glaze tagged variant with "type" discriminator.
/// Variant alternatives use explicit glz::meta<T>::value (required for tagged dispatch).

#include <spdlog/spdlog.h>

#include "common.hpp"

namespace klspw {

enum class DependencyScope : uint8_t { compile, test, runtime, provided };

struct SourceRootData {
    string path;
    string type;
};

struct ContentRootData {
    string path;
    vector<SourceRootData> source_roots;
    strings excluded_patterns;
    strings excluded_urls;
};

/// Variant alternatives keep camelCase field names because glaze's tagged variant
/// dispatch doesn't apply rename_key/camel_case/modify during reading.

struct ModuleDep {
    string name;
    DependencyScope scope = DependencyScope::compile;
    bool isExported = false; // camelCase: variant tagged dispatch requires matching JSON field names
    bool isTestJar = false;
    auto operator<=>(const ModuleDep&) const = default;
};

struct LibraryDep {
    string name;
    DependencyScope scope = DependencyScope::compile;
    bool isExported = false; // camelCase: variant tagged dispatch requires matching JSON field names
    auto operator<=>(const LibraryDep&) const = default;
};

struct SdkDep {
    string name;
    string kind;
    auto operator<=>(const SdkDep&) const = default;
};

struct InheritedSdk {
    auto operator<=>(const InheritedSdk&) const = default;
};

struct ModuleSource {
    auto operator<=>(const ModuleSource&) const = default;
};

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
template <> struct glz::meta<klspw::SourceRootData> {};

namespace klspw {

struct XmlElement {
    string tag;
    map<string, string> attributes;
    vector<XmlElement> children;
    opt_string text;
};

struct FacetData {
    string name;
    string type;
    optional<XmlElement> configuration;
};

struct ModuleData {
    string name;
    opt_string type;
    vector<DependencyData> dependencies;
    vector<ContentRootData> content_roots;
    vector<FacetData> facets;
};

struct LibraryRootData {
    string path;
    opt_string type;
    opt_string inclusion_options;
};

struct LibraryData {
    string name;
    opt_string level;
    opt_string module;
    opt_string type;
    vector<LibraryRootData> roots;
    strings excluded_roots;
    optional<XmlElement> properties;
};

struct SdkRootData {
    string url;
    string type;
};

struct SdkData {
    string name;
    string type;
    opt_string version;
    opt_string home_path;
    optional<vector<SdkRootData>> roots;
    string additional_data;
};

struct ConfigFileItemData {
    string id;
    string url;
};

struct KotlinSettingsData {
    string name;
    strings source_roots;
    vector<ConfigFileItemData> config_file_items;
    string module;
    bool use_project_settings = false;
    strings implemented_module_names;
    strings depends_on_module_names;
    string_set additional_visible_module_names;
    opt_string production_output_path;
    opt_string test_output_path;
    strings source_set_names;
    bool is_test_module = false;
    string external_project_id;
    bool is_hmpp_enabled = true;
    strings pure_kotlin_source_folders;
    string kind = "default";
    opt_string compiler_arguments;
    opt_string additional_arguments;
    opt_string script_templates;
    opt_string script_templates_classpath;
    bool copy_js_library_files = false;
    opt_string output_directory_for_js_library_files;
    opt_string target_platform;
    strings external_system_run_tasks;
    int version = 5;
    bool flush_needed = false;
};

struct JavaSettingsData {
    string module;
    bool inherited_compiler_output = true;
    bool exclude_output = true;
    opt_string compiler_output;
    opt_string compiler_output_for_tests;           
    opt_string language_level_id;
    map<string, string> manifest_attributes;
};

struct WorkspaceData {
    vector<ModuleData> modules;
    vector<LibraryData> libraries;
    vector<SdkData> sdks;
    vector<KotlinSettingsData> kotlin_settings;
    vector<JavaSettingsData> java_settings;

    void merge(WorkspaceData other) {
        modules.append_range(std::move(other.modules));
        kotlin_settings.append_range(std::move(other.kotlin_settings));
        java_settings.append_range(std::move(other.java_settings));
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

    void log_summary() const {
        spdlog::info("Modules ({}):", modules.size());
        for (const auto& mod : modules) {
            const auto lib_deps =
                r::count_if(mod.dependencies, [](const auto& d) { return std::holds_alternative<LibraryDep>(d); });
            spdlog::info("  {} ({} deps, {} content root(s))", mod.name, lib_deps, mod.content_roots.size());
            for (const auto& cr : mod.content_roots) {
                spdlog::info("    root: {} ({} source root(s))", cr.path, cr.source_roots.size());
            }
        }

        spdlog::info("Libraries ({}):", libraries.size());
        for (const auto& lib : libraries) {
            spdlog::info("  {} ({} root(s))", lib.name, lib.roots.size());
        }

        spdlog::info("Kotlin settings ({}):", kotlin_settings.size());
        for (const auto& ks : kotlin_settings) {
            spdlog::info("  module={}, {} source root(s), {} pure-kotlin folder(s)", ks.module, ks.source_roots.size(),
                         ks.pure_kotlin_source_folders.size());
        }
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
