#pragma once

/// Gradle build output model and parser.
/// SourceSet, GradleProject, GradleBuildOutput represent the JSON
/// emitted by the init script between KLSPW_BEGIN/KLSPW_END delimiters.
/// GradleOutputParser extracts and deserializes that JSON.
///
/// Each type also owns its conversion to workspace model types (Tell Don't Ask).
/// SourceSet → ContentRootData + LibraryData + LibraryDep
/// GradleProject → ModuleData + KotlinSettingsData
/// GradleBuildOutput → WorkspaceData

#include <algorithm>

#include "common.hpp"
#include "json.hpp"
#include "workspace_model.hpp"

namespace klspw {

// --- Gradle build output model ---

/// A Gradle source set (main, test, integrationTest, etc.).
/// Maps to the "sourceSets" array in the init script's JSON output.
/// source_roots includes all dirs (kotlin + java + resources); java_source_roots
/// is the Java-only subset. The difference identifies pure-Kotlin folders for
/// KotlinSettingsData.pureKotlinSourceFolders.
class SourceSet {
  public:
    const string& name() const { return name_; }
    const vector<fs::path>& source_roots() const { return source_roots_; }
    const vector<fs::path>& java_source_roots() const { return java_source_roots_; }
    const vector<fs::path>& resources_roots() const { return resources_roots_; }
    const vector<fs::path>& classes_dirs() const { return classes_dirs_; }
    const optional<fs::path>& resources_dir() const { return resources_dir_; }
    const vector<fs::path>& compile_classpath() const { return compile_classpath_; }
    const vector<fs::path>& runtime_classpath() const { return runtime_classpath_; }
    const string& compile_classpath_config_name() const { return compile_classpath_config_name_; }
    const string& runtime_classpath_config_name() const { return runtime_classpath_config_name_; }

    /// Heuristic: any source set whose name contains "test" or "Test".
    bool is_test() const { return name_.contains("test") || name_.contains("Test"); }

    // --- Workspace model conversion ---

    /// Derive a library name from a jar path. Uses the filename stem.
    /// E.g., "/path/to/kotlin-stdlib-2.0.0.jar" → "kotlin-stdlib-2.0.0".
    static string library_name_for_jar(const fs::path& jar) { return jar.stem().string(); }

    /// Classify source roots into SourceRootData with appropriate types.
    /// Kotlin and Java sources both use "java-source"/"java-test" (kotlin-lsp convention).
    /// Resources use "java-resource"/"java-test-resource".
    vector<SourceRootData> to_source_roots() const {
        const auto* const src_type = is_test() ? "java-test" : "java-source";
        const auto* const res_type = is_test() ? "java-test-resource" : "java-resource";

        vector<SourceRootData> roots;
        for (const auto& sr : source_roots_) {
            if (!std::ranges::contains(resources_roots_, sr)) {
                roots.push_back({.path = sr.string(), .type = src_type});
            }
        }
        for (const auto& rr : resources_roots_) {
            roots.push_back({.path = rr.string(), .type = res_type});
        }
        return roots;
    }

    /// Build a ContentRootData from this source set's roots under the given project dir.
    ContentRootData to_content_root(const fs::path& project_dir) const {
        return {
            .path = project_dir.string(),
            .sourceRoots = to_source_roots(),
        };
    }

    /// Build LibraryData entries from the compile classpath.
    /// Each jar becomes a library with a single CLASSES root.
    vector<LibraryData> collect_libraries() const {
        vector<LibraryData> libs;
        libs.reserve(compile_classpath_.size());
        for (const auto& jar : compile_classpath_) {
            libs.push_back({
                .name = library_name_for_jar(jar),
                .roots = {{.path = jar.string()}},
            });
        }
        return libs;
    }

    /// Build LibraryDep references for each compile classpath jar.
    vector<DependencyData> collect_library_deps() const {
        const auto scope = is_test() ? DependencyScope::test : DependencyScope::compile;
        vector<DependencyData> deps;
        deps.reserve(compile_classpath_.size());
        for (const auto& jar : compile_classpath_) {
            deps.emplace_back(LibraryDep{.name = library_name_for_jar(jar), .scope = scope});
        }
        return deps;
    }

    static SourceSet from_json(const json& j) {
        SourceSet ss;
        ss.name_ = read<string>(j, "name");
        ss.source_roots_ = read_all<fs::path>(j, "sourceRoots", to_path);
        ss.java_source_roots_ = read_all<fs::path>(j, "javaSourceRoots", to_path);
        ss.resources_roots_ = read_all<fs::path>(j, "resourcesRoots", to_path);
        ss.classes_dirs_ = read_all<fs::path>(j, "classesDirs", to_path);
        ss.resources_dir_ = read_opt<string>(j, "resourcesDir").transform([](const string& s) { return fs::path{s}; });
        ss.compile_classpath_ = read_all<fs::path>(j, "compileClasspath", to_path);
        ss.runtime_classpath_ = read_all<fs::path>(j, "runtimeClasspath", to_path);
        ss.compile_classpath_config_name_ = read_or<string>(j, "compileClasspathConfigurationName", "");
        ss.runtime_classpath_config_name_ = read_or<string>(j, "runtimeClasspathConfigurationName", "");
        return ss;
    }

  private:
    string name_;
    vector<fs::path> source_roots_;
    vector<fs::path> java_source_roots_;
    vector<fs::path> resources_roots_;
    vector<fs::path> classes_dirs_;
    optional<fs::path> resources_dir_;
    vector<fs::path> compile_classpath_;
    vector<fs::path> runtime_classpath_;
    string compile_classpath_config_name_;
    string runtime_classpath_config_name_;
};

/// A Gradle (sub)project from the init script output.
/// Module name is derived from the last path component of project_dir
/// (e.g., "/home/dev/my-service" → "my-service").
/// Skipped projects (non-JVM, no source sets) carry a skip_reason and are
/// excluded from workspace generation.
class GradleProject {
  public:
    const string& project_path() const { return project_path_; }
    const fs::path& project_dir() const { return project_dir_; }
    const string& kind() const { return kind_; }
    const strings& plugins() const { return plugins_; }
    const vector<SourceSet>& source_sets() const { return source_sets_; }
    const opt_string& skip_reason() const { return skip_reason_; }

    bool is_skipped() const { return skip_reason_.has_value(); }
    string module_name() const { return project_dir_.filename().string(); }

    // --- Workspace model conversion ---

    /// Build a ModuleData from this project's source sets.
    /// Aggregates content roots and library deps from all (non-test if !include_tests) source sets.
    ModuleData to_module(bool include_tests) const {
        vector<ContentRootData> content_roots;
        vector<DependencyData> deps;
        set<string> seen_libs;
        const auto dir_str = project_dir_.string();

        for (const auto& ss : source_sets_) {
            if (!include_tests && ss.is_test()) {
                continue;
            }

            for (auto& sr : ss.to_source_roots()) {
                bool found = false;
                for (auto& cr : content_roots) {
                    if (cr.path == dir_str) {
                        cr.sourceRoots.push_back(std::move(sr));
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    content_roots.push_back({
                        .path = dir_str,
                        .sourceRoots = {std::move(sr)},
                    });
                }
            }

            for (auto& dep : ss.collect_library_deps()) {
                const auto& lib_dep = std::get<LibraryDep>(dep);
                if (seen_libs.insert(lib_dep.name).second) {
                    deps.push_back(std::move(dep));
                }
            }
        }

        // kotlin-lsp expects these two sentinel deps on every module
        deps.push_back(InheritedSdk{});
        deps.push_back(ModuleSource{});

        return {
            .name = module_name(),
            .type = "JAVA_MODULE",
            .dependencies = std::move(deps),
            .contentRoots = std::move(content_roots),
        };
    }

    /// Collect all unique libraries from this project's source sets.
    vector<LibraryData> collect_libraries(bool include_tests) const {
        vector<LibraryData> libs;
        set<string> seen;
        for (const auto& ss : source_sets_) {
            if (!include_tests && ss.is_test()) {
                continue;
            }
            for (auto& lib : ss.collect_libraries()) {
                if (seen.insert(lib.name).second) {
                    libs.push_back(std::move(lib));
                }
            }
        }
        return libs;
    }

    /// Build KotlinSettingsData for this project.
    /// compiler_args_json: pre-formatted "J{...}" string from Config.
    KotlinSettingsData to_kotlin_settings(const string& compiler_args_json, bool include_tests) const {
        const auto mod_name = module_name();
        strings source_roots;
        strings pure_kotlin_folders;

        for (const auto& ss : source_sets_) {
            if (!include_tests && ss.is_test()) {
                continue;
            }
            for (const auto& sr : ss.source_roots()) {
                const auto sr_str = sr.string();
                source_roots.push_back(sr_str);

                // Pure Kotlin folder: in source_roots but not in java_source_roots
                if (!std::ranges::contains(ss.java_source_roots(), sr)) {
                    pure_kotlin_folders.push_back(sr_str);
                }
            }
        }

        return {
            .name = "Kotlin",
            .sourceRoots = std::move(source_roots),
            .module = mod_name,
            .externalProjectId = format(":{}:unspecified", mod_name),
            .pureKotlinSourceFolders = std::move(pure_kotlin_folders),
            .compilerArguments = compiler_args_json,
        };
    }

    static GradleProject from_json(const json& j) {
        GradleProject proj;
        proj.project_path_ = read<string>(j, "projectPath");
        proj.project_dir_ = fs::path{read<string>(j, "projectDir")};
        proj.kind_ = read<string>(j, "kind");
        proj.plugins_ = read_all<string>(j, "plugins");
        for (const auto& ss_json : j.at("sourceSets")) {
            proj.source_sets_.push_back(SourceSet::from_json(ss_json));
        }
        proj.skip_reason_ = read_opt<string>(j, "skipReason");
        return proj;
    }

  private:
    string project_path_;
    fs::path project_dir_;
    string kind_;
    strings plugins_;
    vector<SourceSet> source_sets_;
    opt_string skip_reason_;
};

/// Top-level Gradle build output: root project path + all discovered (sub)projects.
/// Produced by GradleOutputParser::parse() from the init script's JSON.
/// to_workspace() is the main entry point for the Gradle → workspace.json pipeline.
class GradleBuildOutput {
  public:
    const fs::path& root_project() const { return root_project_; }
    const vector<GradleProject>& projects() const { return projects_; }

    size_t active_project_count() const {
        return static_cast<size_t>(count_if(projects_, [](const auto& p) { return !p.is_skipped(); }));
    }

    // --- Workspace model conversion ---

    /// Build a complete WorkspaceData from all active projects.
    /// Libraries are deduplicated by name across all projects (first occurrence wins).
    WorkspaceData to_workspace(const string& compiler_args_json, bool include_tests) const {
        vector<ModuleData> modules;
        vector<LibraryData> libraries;
        vector<KotlinSettingsData> kotlin_settings;
        set<string> seen_libs;

        for (const auto& proj : projects_) {
            if (proj.is_skipped()) {
                continue;
            }

            modules.push_back(proj.to_module(include_tests));
            kotlin_settings.push_back(proj.to_kotlin_settings(compiler_args_json, include_tests));

            for (auto& lib : proj.collect_libraries(include_tests)) {
                if (seen_libs.insert(lib.name).second) {
                    libraries.push_back(std::move(lib));
                }
            }
        }

        return {
            .modules = std::move(modules),
            .libraries = std::move(libraries),
            .kotlinSettings = std::move(kotlin_settings),
        };
    }

    static GradleBuildOutput from_json(const json& j) {
        GradleBuildOutput output;
        output.root_project_ = fs::path{read<string>(j, "rootProject")};
        output.projects_ =
            read_all<GradleProject>(j, "projects", [](const json& pj) { return GradleProject::from_json(pj); });
        return output;
    }

  private:
    fs::path root_project_;
    vector<GradleProject> projects_;
};

// --- Parser ---

/// Extracts and parses the structured JSON payload from raw Gradle output.
/// The init script wraps its JSON between KLSPW_BEGIN/KLSPW_END delimiters
/// so it can be reliably separated from Gradle's noisy human-readable output.
class GradleOutputParser {
  public:
    static constexpr string_view begin_delimiter = "KLSPW_BEGIN";
    static constexpr string_view end_delimiter = "KLSPW_END";

    static string extract_json(string_view raw_output) {
        const auto begin_pos = raw_output.find(begin_delimiter);
        if (begin_pos == string_view::npos) {
            throw runtime_error("KLSPW_BEGIN delimiter not found in Gradle output");
        }
        const auto json_start = begin_pos + begin_delimiter.size();
        const auto end_pos = raw_output.find(end_delimiter, json_start);
        if (end_pos == string_view::npos) {
            throw runtime_error("KLSPW_END delimiter not found in Gradle output");
        }
        auto block = raw_output.substr(json_start, end_pos - json_start);
        block = trim(block);
        return string{block};
    }

    static GradleBuildOutput parse(string_view json_str) {
        json j;
        try {
            j = json::parse(json_str);
        } catch (const json::parse_error& e) {
            throw runtime_error(format("Failed to parse Gradle JSON: {}", e.what()));
        }
        return GradleBuildOutput::from_json(j);
    }
};

} // namespace klspw
