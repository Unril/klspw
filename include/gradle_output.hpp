#pragma once

/// Gradle build output model and parser.
///
/// The init script (resources/init.gradle.kts) registers a dumpKotlinLspModel
/// task that emits structured JSON between KLSPW_BEGIN/KLSPW_END delimiters.
/// This header defines the types that mirror that JSON structure and the parser
/// that extracts it from Gradle's noisy stdout.
///
/// Each model type owns its conversion to workspace model types (Tell Don't Ask):
///   SourceSet       -> SourceRootData, ContentRootData, LibraryData, LibraryDep
///   GradleProject   -> ModuleData, KotlinSettingsData
///   GradleBuildOutput -> WorkspaceData (the full pipeline entry point)
///
/// JSON field names are camelCase (matching the Kotlin init script output).
/// C++ field names are snake_case. The mapping is defined via inline struct glaze
/// metadata on each type.

#include <ranges>

#include "common.hpp"
#include "config.hpp"
#include "workspace_model.hpp"

namespace klspw {

// --- Free utilities ---

/// Derive a library name from a jar path.
/// E.g., "/cache/kotlin-stdlib-2.0.0.jar" -> "kotlin-stdlib-2.0.0".
inline string library_name_for_jar(const string& jar) {
    return fs::path{jar}.stem().string();
}

// --- SourceSet ---

/// A Gradle source set (main, test, integrationTest, etc.).
///
/// Represents one entry in the "sourceSets" array of the init script JSON.
/// source_roots includes all dirs (kotlin + java + resources); java_source_roots
/// is the Java-only subset. The difference identifies pure-Kotlin folders for
/// KotlinSettingsData.pureKotlinSourceFolders.
///
/// All path fields are plain strings -- fs::path operations happen at usage sites.
struct SourceSet {
    string name;
    strings source_roots;
    string_set java_source_roots;
    string_set resources_roots;
    strings classes_dirs;
    opt_string resources_dir;
    strings compile_classpath;
    strings runtime_classpath;
    string compile_classpath_config_name;
    string runtime_classpath_config_name;

    struct glaze {
        using T = SourceSet;
        static constexpr auto value =
            glz::object("name", &T::name, "sourceRoots", &T::source_roots, "javaSourceRoots", &T::java_source_roots,
                        "resourcesRoots", &T::resources_roots, "classesDirs", &T::classes_dirs, "resourcesDir",
                        &T::resources_dir, "compileClasspath", &T::compile_classpath, "runtimeClasspath",
                        &T::runtime_classpath, "compileClasspathConfigurationName", &T::compile_classpath_config_name,
                        "runtimeClasspathConfigurationName", &T::runtime_classpath_config_name);
    };

    /// Heuristic: any source set whose name contains "test" or "Test".
    bool is_test() const { return name.contains("test") || name.contains("Test"); }
    bool is_source() const { return !is_test(); }
    DependencyScope dep_scope() const { return is_test() ? DependencyScope::test : DependencyScope::compile; }

    /// Source roots not in java_source_roots -- directories containing only .kt files.
    auto pure_kotlin_roots() const { return source_roots | not_in(java_source_roots); }

    // --- Factory methods for workspace model types ---

    static LibraryData library_from_jar(const string& jar) {
        return {.name = library_name_for_jar(jar), .roots = {{.path = jar}}};
    }

    LibraryDep library_dep_from_jar(const string& jar) const {
        return {.name = library_name_for_jar(jar), .scope = dep_scope()};
    }

    // --- Workspace model conversion ---

    vector<SourceRootData> to_source_roots() const {
        const auto* const src_type = is_test() ? "java-test" : "java-source";
        const auto* const res_type = is_test() ? "java-test-resource" : "java-resource";

        auto sources = source_roots | not_in(resources_roots) |
                       v::transform([&](const auto& sr) { return SourceRootData{sr, src_type}; });
        auto resources = resources_roots | v::transform([&](const auto& rr) { return SourceRootData{rr, res_type}; });

        auto roots = sources | to_vector();
        roots.append_range(resources);
        return roots;
    }

    vector<LibraryData> collect_libraries() const {
        return compile_classpath | v::transform(library_from_jar) | to_vector();
    }

    vector<LibraryDep> collect_library_deps() const {
        return compile_classpath | v::transform([this](const auto& jar) { return library_dep_from_jar(jar); }) |
               to_vector();
    }
};

// --- GradleProject ---

/// Module name is derived from the last path component of project_dir.
/// Skipped projects carry a skip_reason and are excluded from workspace generation.
struct GradleProject {
    string project_path;
    string project_dir;
    string kind;
    strings plugins;
    vector<SourceSet> source_sets;
    opt_string skip_reason;

    struct glaze {
        using T = GradleProject;
        static constexpr auto value =
            glz::object("projectPath", &T::project_path, "projectDir", &T::project_dir, "kind", &T::kind, "plugins",
                        &T::plugins, "sourceSets", &T::source_sets, "skipReason", &T::skip_reason);
    };

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
        return {{.path = project_dir, .sourceRoots = std::move(source_roots)}};
    }

    vector<ContentRootData> module_content_roots(const vector<SourceSet>& sets) const {
        return content_roots_from(sets | v::transform(&SourceSet::to_source_roots) | v::join | to_vector());
    }

    static vector<DependencyData> module_dependencies(const vector<SourceSet>& sets) {
        auto lib_deps = sets | v::transform(&SourceSet::collect_library_deps) | v::join | unique_by(&LibraryDep::name);
        vector<DependencyData> deps(std::make_move_iterator(lib_deps.begin()), std::make_move_iterator(lib_deps.end()));
        deps.emplace_back(InheritedSdk{});
        deps.emplace_back(ModuleSource{});
        return deps;
    }

    ModuleData to_module(const GenerationOptions& options) const {
        const auto sets = active_sets(options);
        return {
            .name = module_name(),
            .type = "JAVA_MODULE",
            .dependencies = module_dependencies(sets),
            .contentRoots = module_content_roots(sets),
        };
    }

    vector<LibraryData> collect_libraries(const GenerationOptions& options) const {
        return active_sets(options) | v::transform(&SourceSet::collect_libraries) | v::join |
               unique_by(&LibraryData::name);
    }

    KotlinSettingsData to_kotlin_settings(const string& compiler_args_json, const GenerationOptions& options) const {
        const auto mod_name = module_name();
        const auto sets = active_sets(options);
        return {
            .name = "Kotlin",
            .sourceRoots = sets | v::transform(&SourceSet::source_roots) | v::join | to_vector(),
            .module = mod_name,
            .externalProjectId = format(":{}:unspecified", mod_name),
            .pureKotlinSourceFolders = sets | v::transform(&SourceSet::pure_kotlin_roots) | v::join | to_vector(),
            .compilerArguments = compiler_args_json,
        };
    }
};

// --- GradleBuildOutput ---

/// Top-level Gradle build output: root project path + all discovered (sub)projects.
/// to_workspace() is the main entry point for the Gradle -> workspace.json pipeline.
struct GradleBuildOutput {
    string root_project;
    vector<GradleProject> projects;

    struct glaze {
        using T = GradleBuildOutput;
        static constexpr auto value = glz::object("rootProject", &T::root_project, "projects", &T::projects);
    };

    size_t active_project_count() const {
        return static_cast<size_t>(r::count_if(projects, std::not_fn(&GradleProject::is_skipped)));
    }

    auto active_projects() const { return projects | v::filter(std::not_fn(&GradleProject::is_skipped)); }

    WorkspaceData to_workspace(const string& compiler_args_json, const GenerationOptions& options) const {
        auto active = active_projects();

        auto kotlin_settings =
            active | v::transform([&](const auto& p) { return p.to_kotlin_settings(compiler_args_json, options); }) |
            to_vector();
        auto all_libs = active | v::transform([&](const auto& p) { return p.collect_libraries(options); }) | v::join;
        return {
            .modules = active | v::transform([&](const auto& p) { return p.to_module(options); }) | to_vector(),
            .libraries = all_libs | unique_by(&LibraryData::name),
            .kotlinSettings = std::move(kotlin_settings),
        };
    }
};

// --- GradleOutputParser ---

class GradleOutputParser {
  public:
    static constexpr string_view begin_delimiter = "KLSPW_BEGIN";
    static constexpr string_view end_delimiter = "KLSPW_END";

    static string extract_json(string_view raw_output) {
        auto result = extract_between(raw_output, {.open = begin_delimiter, .close = end_delimiter});
        require(result.has_value(), "KLSPW_BEGIN/KLSPW_END delimiters not found in Gradle output");
        return std::move(*result);
    }

    static GradleBuildOutput parse(string_view json_str) {
        GradleBuildOutput output;
        const auto ec = glz::read<ws_read_opts>(output, json_str);
        require(!ec, "Failed to parse Gradle JSON: {}", [&] { return glz::format_error(ec, json_str); });
        return output;
    }
};

} // namespace klspw
