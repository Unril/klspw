#pragma once

/// Gradle build output model and parser.
///
/// The init script (resources/init.gradle.kts) registers a dumpKotlinLspModel
/// task that emits structured JSON between KLSPW_BEGIN/KLSPW_END delimiters.
/// This header defines the types that mirror that JSON structure and the parser
/// that extracts it from Gradle's noisy stdout.
///
/// Each model type owns its conversion to workspace model types (Tell Don't Ask):
///   SourceSet       → SourceRootData, ContentRootData, LibraryData, LibraryDep
///   GradleProject   → ModuleData, KotlinSettingsData
///   GradleBuildOutput → WorkspaceData (the full pipeline entry point)
///
/// JSON field names are camelCase (matching the Kotlin init script output).
/// C++ field names are snake_case. The mapping is defined via inline struct glaze
/// metadata on each type.

#include "common.hpp"
#include "workspace_model.hpp"

namespace klspw {

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
    string name; ///< Source set name (e.g., "main", "test", "integrationTest").
    strings source_roots; ///< All source dirs (kotlin + java + resources).
    strings java_source_roots; ///< Java-only source dirs (subset of source_roots).
    strings resources_roots; ///< Resource directories.
    strings classes_dirs; ///< Compiled class output directories.
    opt_string resources_dir; ///< Processed resources output directory. Null if not configured.
    strings compile_classpath; ///< Jars on the compile classpath.
    strings runtime_classpath; ///< Jars on the runtime classpath (superset of compile).
    string compile_classpath_config_name; ///< Gradle configuration name (e.g., "compileClasspath").
    string runtime_classpath_config_name; ///< Gradle configuration name (e.g., "runtimeClasspath").

    /// Inline glaze metadata -- maps camelCase JSON keys to snake_case C++ fields.
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

    // --- Workspace model conversion ---

    /// Derive a library name from a jar path.
    /// E.g., "/cache/kotlin-stdlib-2.0.0.jar" → "kotlin-stdlib-2.0.0".
    static string library_name_for_jar(const string& jar) { return fs::path{jar}.stem().string(); }

    /// Classify source roots into SourceRootData with appropriate types.
    /// Roots in resources_roots become resources; the rest become sources.
    vector<SourceRootData> to_source_roots() const {
        const auto* const src_type = is_test() ? "java-test" : "java-source";
        const auto* const res_type = is_test() ? "java-test-resource" : "java-resource";

        auto to_root = [](const auto& path, const auto* type) { return SourceRootData{.path = path, .type = type}; };

        auto sources = source_roots | v::filter([&](const auto& sr) { return !r::contains(resources_roots, sr); }) |
                       v::transform([&](const auto& sr) { return to_root(sr, src_type); });

        auto resources = resources_roots | v::transform([&](const auto& rr) { return to_root(rr, res_type); });

        vector<SourceRootData> roots(std::from_range, sources);
        roots.append_range(resources);
        return roots;
    }

    /// Build a ContentRootData from this source set's roots under the given project dir.
    ContentRootData to_content_root(const string& project_dir) const {
        return {.path = project_dir, .sourceRoots = to_source_roots()};
    }

    /// Build LibraryData entries from the compile classpath.
    /// Each jar becomes a library with a single CLASSES root.
    vector<LibraryData> collect_libraries() const {
        auto libs = compile_classpath | v::transform([](const auto& jar) {
                        return LibraryData{.name = library_name_for_jar(jar), .roots = {{.path = jar}}};
                    });
        return {std::from_range, libs};
    }

    /// Build LibraryDep references for each compile classpath jar.
    /// Test source sets get DependencyScope::test; others get compile.
    vector<DependencyData> collect_library_deps() const {
        const auto scope = is_test() ? DependencyScope::test : DependencyScope::compile;
        auto deps = compile_classpath | v::transform([scope](const auto& jar) -> DependencyData {
                        return LibraryDep{.name = library_name_for_jar(jar), .scope = scope};
                    });
        return {std::from_range, deps};
    }
};

// --- GradleProject ---

/// A Gradle (sub)project from the init script output.
///
/// Module name is derived from the last path component of project_dir
/// (e.g., "/home/dev/my-service" → "my-service").
/// Skipped projects (non-JVM, no source sets) carry a skip_reason and are
/// excluded from workspace generation.
struct GradleProject {
    string project_path; ///< Gradle project path (e.g., ":", ":subproject").
    string project_dir; ///< Absolute filesystem path to the project directory.
    string kind; ///< Project kind (e.g., "jvm", "non-jvm").
    strings plugins; ///< Applied Gradle plugins.
    vector<SourceSet> source_sets; ///< Source sets discovered by the init script.
    opt_string skip_reason; ///< Why this project was skipped, or nullopt if active.

    /// Inline glaze metadata -- maps camelCase JSON keys to snake_case C++ fields.
    struct glaze {
        using T = GradleProject;
        static constexpr auto value =
            glz::object("projectPath", &T::project_path, "projectDir", &T::project_dir, "kind", &T::kind, "plugins",
                        &T::plugins, "sourceSets", &T::source_sets, "skipReason", &T::skip_reason);
    };

    bool is_skipped() const { return skip_reason.has_value(); }
    string module_name() const { return fs::path{project_dir}.filename().string(); }

    /// Filter source sets: exclude test sets when include_tests is false.
    auto active_sets(bool include_tests) const {
        return source_sets | v::filter([include_tests](const auto& ss) { return include_tests || !ss.is_test(); });
    }

    // --- Workspace model conversion ---

    /// Build a ModuleData from this project's source sets.
    /// Source roots from the same project_dir are merged into a single ContentRootData.
    /// Library deps are deduplicated by name (first occurrence wins).
    /// InheritedSdk and ModuleSource sentinels are appended -- kotlin-lsp expects both.
    ModuleData to_module(bool include_tests) const {
        vector<ContentRootData> content_roots;

        for (const auto& ss : active_sets(include_tests)) {
            // Merge source roots into a single ContentRootData per project_dir.
            auto it = r::find(content_roots, project_dir, &ContentRootData::path);
            if (it != content_roots.end()) {
                it->sourceRoots.append_range(ss.to_source_roots());
            } else {
                content_roots.push_back({.path = project_dir, .sourceRoots = ss.to_source_roots()});
            }
        }

        // Flatten all library deps across active source sets, dedup by name.
        auto all_deps = active_sets(include_tests)
            | v::transform([](const auto& ss) { return ss.collect_library_deps(); })
            | v::join;
        auto deps = unique_by(all_deps, [](const auto& dep) -> const string& {
            return std::get<LibraryDep>(dep).name;
        });

        deps.emplace_back(InheritedSdk{});
        deps.emplace_back(ModuleSource{});

        return {
            .name = module_name(),
            .type = "JAVA_MODULE",
            .dependencies = std::move(deps),
            .contentRoots = std::move(content_roots),
        };
    }

    /// Collect all unique libraries from this project's source sets.
    /// Deduplicated by name (first occurrence wins).
    vector<LibraryData> collect_libraries(bool include_tests) const {
        auto all_libs = active_sets(include_tests)
            | v::transform([](const auto& ss) { return ss.collect_libraries(); })
            | v::join;
        return unique_by(all_libs, &LibraryData::name);
    }

    /// Build KotlinSettingsData for this project.
    /// compiler_args_json: pre-formatted "J{...}" string from Config.
    /// pureKotlinSourceFolders: source roots in source_roots but NOT in java_source_roots.
    KotlinSettingsData to_kotlin_settings(const string& compiler_args_json, bool include_tests) const {
        const auto mod_name = module_name();
        strings src_roots;
        strings pure_kotlin_folders;

        for (const auto& ss : active_sets(include_tests)) {
            src_roots.append_range(ss.source_roots);

            auto pure =
                ss.source_roots | v::filter([&](const auto& sr) { return !r::contains(ss.java_source_roots, sr); });
            pure_kotlin_folders.append_range(pure);
        }

        return {
            .name = "Kotlin",
            .sourceRoots = std::move(src_roots),
            .module = mod_name,
            .externalProjectId = format(":{}:unspecified", mod_name),
            .pureKotlinSourceFolders = std::move(pure_kotlin_folders),
            .compilerArguments = compiler_args_json,
        };
    }
};

// --- GradleBuildOutput ---

/// Top-level Gradle build output: root project path + all discovered (sub)projects.
/// Produced by GradleOutputParser::parse() from the init script's JSON.
/// to_workspace() is the main entry point for the Gradle → workspace.json pipeline.
struct GradleBuildOutput {
    string root_project; ///< Absolute path to the Gradle root project directory.
    vector<GradleProject> projects; ///< All discovered (sub)projects, including skipped ones.

    /// Inline glaze metadata -- maps camelCase JSON keys to snake_case C++ fields.
    struct glaze {
        using T = GradleBuildOutput;
        static constexpr auto value = glz::object("rootProject", &T::root_project, "projects", &T::projects);
    };

    /// Count of non-skipped projects.
    size_t active_project_count() const {
        return static_cast<size_t>(r::count_if(projects, [](const auto& p) { return !p.is_skipped(); }));
    }

    /// Filter to non-skipped projects.
    auto active_projects() const {
        return projects | v::filter([](const auto& p) { return !p.is_skipped(); });
    }

    /// Build a complete WorkspaceData from all active projects.
    /// Libraries are deduplicated by name across all projects (first occurrence wins).
    WorkspaceData to_workspace(const string& compiler_args_json, bool include_tests) const {
        auto active = active_projects();

        auto modules = active | v::transform([&](const auto& p) { return p.to_module(include_tests); });
        auto kotlin_settings = active | v::transform([&](const auto& p) {
                                   return p.to_kotlin_settings(compiler_args_json, include_tests);
                               });

        // Libraries need cross-project dedup by name (first occurrence wins).
        auto all_libs = active
            | v::transform([&](const auto& p) { return p.collect_libraries(include_tests); })
            | v::join;
        auto libraries = unique_by(all_libs, &LibraryData::name);

        return {
            .modules = {std::from_range, modules},
            .libraries = std::move(libraries),
            .kotlinSettings = {std::from_range, kotlin_settings},
        };
    }
};

// --- GradleOutputParser ---

/// Extracts and parses the structured JSON payload from raw Gradle output.
///
/// The init script wraps its JSON between KLSPW_BEGIN/KLSPW_END delimiters
/// so it can be reliably separated from Gradle's noisy human-readable output.
///
/// Usage:
///   auto json_str = GradleOutputParser::extract_json(raw_gradle_stdout);
///   auto build_output = GradleOutputParser::parse(json_str);
class GradleOutputParser {
  public:
    static constexpr string_view begin_delimiter = "KLSPW_BEGIN";
    static constexpr string_view end_delimiter = "KLSPW_END";

    /// Extract the JSON block between KLSPW_BEGIN and KLSPW_END delimiters.
    /// Throws if either delimiter is missing.
    static string extract_json(string_view raw_output) {
        auto result = extract_between(raw_output, begin_delimiter, end_delimiter);
        require(result.has_value(), "KLSPW_BEGIN/KLSPW_END delimiters not found in Gradle output");
        return std::move(*result);
    }

    /// Parse a JSON string into a GradleBuildOutput.
    /// Throws on malformed JSON with a formatted error message.
    static GradleBuildOutput parse(string_view json_str) {
        GradleBuildOutput output;
        const auto ec = glz::read<ws_read_opts>(output, json_str);
        require(!ec, "Failed to parse Gradle JSON: {}", [&] { return glz::format_error(ec, json_str); });
        return output;
    }
};

} // namespace klspw
