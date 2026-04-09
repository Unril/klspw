#pragma once

/// Gradle build output model and parser.
///
/// The init script (resources/init.gradle.kts) registers a dumpKotlinLspModel
/// task that emits structured JSON between KLSPW_BEGIN/KLSPW_END delimiters.
/// This header defines the types that mirror that JSON structure and the parser
/// that extracts it from Gradle's noisy stdout.
///
/// Each model type owns its conversion to workspace model types (Tell Don't Ask):
///   SourceSet       -> SourceRootData, LibraryData, LibraryDep
///   GradleProject   -> ModuleData (with ContentRootData), KotlinSettingsData
///   GradleBuildOutput -> WorkspaceData (the full pipeline entry point)
///
/// JSON field names are camelCase (matching the Kotlin init script output).
/// C++ field names are snake_case. glz::camel_case auto-converts at compile time.

#include <ranges>

#include "common.hpp"
#include "config.hpp"
#include "sources.hpp"
#include "workspace_model.hpp"

namespace klspw {

// --- Free utilities ---

/// Derive a library name from a jar path.
/// E.g., "/cache/kotlin-stdlib-2.0.0.jar" -> "kotlin-stdlib-2.0.0".
inline string library_name_for_jar(string_view jar) {
    return fs::path{jar}.stem().string();
}

// --- SourceSet ---

/// A Gradle source set (main, test, integrationTest, etc.).
///
/// Mirrors one entry in the "sourceSets" array emitted by SourceSet.toModel()
/// in resources/init.gradle.kts. Fields map 1:1 via glz::camel_case:
///   C++ source_roots -> JSON sourceRoots, etc.
///
/// source_roots includes all dirs (kotlin + java + resources); java_source_roots
/// is the Java-only subset. The difference identifies pure-Kotlin folders for
/// KotlinSettingsData.pure_kotlin_source_folders.
///
/// All path fields are plain strings -- fs::path operations happen at usage sites.
struct SourceSet {
    string name; ///< Source set name (e.g., "main", "test", "integrationTest").
    strings source_roots; ///< All source dirs (kotlin + java + resources).
    string_set java_source_roots; ///< Java-only source dirs (subset of source_roots).
    string_set resources_roots; ///< Resource directories.
    strings classes_dirs; ///< Compiled .class output directories.
    opt_string resources_dir; ///< Processed resources output directory.
    strings compile_classpath; ///< Jars available at compile time.
    strings runtime_classpath; ///< Jars available at runtime.
    map<string, string> source_classpath; ///< Classes jar path -> source jar path (resolved by Gradle).
    string compile_classpath_configuration_name; ///< Gradle configuration name for compile classpath.
    string runtime_classpath_configuration_name; ///< Gradle configuration name for runtime classpath.

    /// Heuristic: any source set whose name contains "test" or "Test".
    bool is_test() const { return name.contains("test") || name.contains("Test"); }
    bool is_source() const { return !is_test(); }
    DependencyScope dep_scope() const { return is_test() ? DependencyScope::test : DependencyScope::compile; }

    /// Source roots not in java_source_roots and not in resources_roots.
    /// Used to populate KotlinSettingsData.pure_kotlin_source_folders.
    strings pure_kotlin_roots() const {
        return source_roots | not_in(java_source_roots) | not_in(resources_roots) | to_vector();
    }

    // --- Factory methods for workspace model types ---

    SourceRootData source_root(const string& path) const {
        return {.path = path, .type = is_test() ? "java-test" : "java-source"};
    }

    SourceRootData resource_root(const string& path) const {
        return {.path = path, .type = is_test() ? "java-test-resource" : "java-resource"};
    }

    LibraryData library_from_jar(string_view jar, bool attach_sources) const {
        vector<LibraryRootData> roots{{.path = string{jar}}};
        if (attach_sources) {
            // Prefer Gradle-resolved source jar, fall back to filesystem discovery.
            if (auto it = source_classpath.find(string{jar}); it != source_classpath.end()) {
                roots.push_back({.path = it->second, .type = "SOURCES"});
            } else if (auto src = find_sources(jar)) {
                roots.push_back({.path = std::move(*src), .type = "SOURCES"});
            }
        }
        return {.name = library_name_for_jar(jar), .type = "java-imported", .roots = std::move(roots)};
    }

    LibraryDep library_dep_from_jar(string_view jar) const {
        return {.name = library_name_for_jar(jar), .scope = dep_scope()};
    }

    // --- Workspace model conversion ---

    vector<SourceRootData> to_source_roots() const {
        auto to_src = [&](const auto& p) { return source_root(p); };
        auto to_res = [&](const auto& p) { return resource_root(p); };
        // Exclude resource dirs from source pass -- they're added below as resource-type roots.
        auto roots = source_roots | not_in(resources_roots) | v::transform(to_src) | to_vector();
        roots.append_range(resources_roots | v::transform(to_res));
        return roots;
    }

    vector<LibraryData> collect_libraries(bool attach_sources) const {
        auto to_lib = [&](const auto& jar) { return library_from_jar(jar, attach_sources); };
        return compile_classpath | v::transform(to_lib) | to_vector();
    }

    vector<LibraryDep> collect_library_deps() const {
        return compile_classpath | v::transform([&](const auto& jar) { return library_dep_from_jar(jar); }) |
               to_vector();
    }
};

// --- GradleProject ---

/// A Gradle (sub)project discovered by the init script.
///
/// Mirrors one entry in the "projects" array emitted by Project.detectModel()
/// in resources/init.gradle.kts. Module name is derived from the last path
/// component of project_dir. Skipped projects carry a skip_reason and are
/// excluded from workspace generation.
struct GradleProject {
    string project_path; ///< Gradle project path (e.g., ":", ":subproject").
    string project_dir; ///< Absolute path to the project directory.
    string kind; ///< Project kind (e.g., "jvm", "non-jvm").
    strings plugins; ///< Applied Gradle plugin class names.
    vector<SourceSet> source_sets; ///< All source sets discovered by the init script.
    opt_string skip_reason; ///< If set, project is excluded from workspace generation.

    bool is_skipped() const { return skip_reason.has_value(); }
    string module_name() const { return fs::path{project_dir}.filename().string(); }

    vector<SourceSet> active_sets(const GenerationOptions& opts) const {
        if (opts.include_tests) {
            return source_sets;
        }
        return source_sets | v::filter(&SourceSet::is_source) | to_vector();
    }

    // --- Workspace model conversion ---

    /// Wrap source roots into a ContentRootData under project_dir, or empty if no roots.
    vector<ContentRootData> content_roots_from(vector<SourceRootData> source_roots) const {
        if (source_roots.empty()) {
            return {};
        }
        return {{.path = project_dir, .source_roots = std::move(source_roots)}};
    }

    vector<ContentRootData> module_content_roots(const vector<SourceSet>& sets) const {
        return content_roots_from(sets | v::transform(&SourceSet::to_source_roots) | v::join | to_vector());
    }

    static vector<DependencyData> module_dependencies(const vector<SourceSet>& sets) {
        auto lib_deps = sets | v::transform(&SourceSet::collect_library_deps) | v::join | unique_by(&LibraryDep::name);
        vector<DependencyData> deps(std::from_range, std::move(lib_deps));
        deps.emplace_back(InheritedSdk{});
        deps.emplace_back(ModuleSource{});
        return deps;
    }

    ModuleData to_module(const vector<SourceSet>& sets) const {
        return {
            .name = module_name(),
            .dependencies = module_dependencies(sets),
            .content_roots = module_content_roots(sets),
        };
    }

    KotlinSettingsData to_kotlin_settings(const string& compiler_args_json, const vector<SourceSet>& sets) const {
        const auto mod_name = module_name();
        return {
            .name = "Kotlin",
            .source_roots = sets | v::transform(&SourceSet::source_roots) | v::join | to_vector(),
            .module = mod_name,
            .external_project_id = format(":{}:unspecified", mod_name),
            .pure_kotlin_source_folders = sets | v::transform(&SourceSet::pure_kotlin_roots) | v::join | to_vector(),
            .compiler_arguments = compiler_args_json,
        };
    }
};

// --- GradleBuildOutput ---

/// Top-level Gradle build output emitted between KLSPW_BEGIN/KLSPW_END delimiters.
/// Mirrors the root JSON object from resources/init.gradle.kts: rootProject + projects.
/// to_workspace() is the main entry point for the Gradle -> workspace.json conversion.
struct GradleBuildOutput {
    string root_project; ///< Absolute path to the Gradle root project directory.
    vector<GradleProject> projects; ///< All discovered (sub)projects, including skipped ones.

    size_t active_project_count() const {
        return static_cast<size_t>(r::count_if(projects, std::not_fn(&GradleProject::is_skipped)));
    }

    auto active_projects() const { return projects | v::filter(std::not_fn(&GradleProject::is_skipped)); }

    WorkspaceData to_workspace(const string& compiler_args_json, const GenerationOptions& options) const {
        auto active = active_projects() | to_vector();

        // Compute active source sets once per project to avoid redundant filtering.
        auto to_active_sets = [&](const auto& p) { return p.active_sets(options); };
        auto sets_per_project = active | v::transform(to_active_sets) | to_vector();

        auto to_libs = [&](const auto& ss) { return ss.collect_libraries(options.attach_sources); };

        WorkspaceData ws;
        for (const auto& [proj, sets] : v::zip(active, sets_per_project)) {
            ws.modules.push_back(proj.to_module(sets));
            ws.kotlin_settings.push_back(proj.to_kotlin_settings(compiler_args_json, sets));
            ws.libraries.append_range(sets | v::transform(to_libs) | v::join | to_vector());
        }

        // Deduplicate libraries by name, keeping first occurrence.
        ws.libraries = std::move(ws.libraries) | unique_by(&LibraryData::name);
        return ws;
    }
};

// --- Gradle output parsing ---

inline constexpr string_view gradle_begin_delimiter = "KLSPW_BEGIN";
inline constexpr string_view gradle_end_delimiter = "KLSPW_END";

inline string extract_gradle_json(string_view raw_output) {
    auto result = extract_between(raw_output, {.open = gradle_begin_delimiter, .close = gradle_end_delimiter});
    require(result.has_value(), "{}/{} delimiters not found in Gradle output", gradle_begin_delimiter,
            gradle_end_delimiter);
    return std::move(*result);
}

/// Parse a JSON string into a GradleBuildOutput. Uses ws_read_opts (ignores unknown keys).
inline GradleBuildOutput parse_gradle_output(string_view json_str) {
    GradleBuildOutput output;
    const auto ec = glz::read<ws_read_opts>(output, json_str);
    require(!ec, "Failed to parse Gradle JSON: {}", [&] { return glz::format_error(ec, json_str); });
    return output;
}

} // namespace klspw

// camel_case auto-converts snake_case C++ fields to camelCase JSON keys.
template <> struct glz::meta<klspw::SourceSet> : glz::camel_case {};
template <> struct glz::meta<klspw::GradleProject> : glz::camel_case {};
template <> struct glz::meta<klspw::GradleBuildOutput> : glz::camel_case {};
