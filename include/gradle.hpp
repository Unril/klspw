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
#include "describe.hpp"
#include "files.hpp"
#include "ranges.hpp"
#include "sources.hpp"
#include "strings.hpp"
#include "workspace.hpp"

namespace klspw {

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
    string_map<string> source_classpath; ///< Classes jar -> source jar (resolved by Gradle).
    string compile_classpath_configuration_name; ///< Gradle configuration name for compile classpath.
    string runtime_classpath_configuration_name; ///< Gradle configuration name for runtime classpath.

    /// Heuristic: any source set whose name contains "test" or "Test".
    bool is_test() const { return name.contains("test") || name.contains("Test"); }

    /// Source roots not in java_source_roots and not in resources_roots.
    /// Used to populate KotlinSettingsData.pure_kotlin_source_folders.
    strings pure_kotlin_roots() const {
        return source_roots | not_in(java_source_roots) | not_in(resources_roots) | to_vector();
    }

    LibraryData library_from_jar_with_sources(string_view jar) const {
        vector<LibraryRootData> roots{{.path = string{jar}}};
        // Prefer Gradle-resolved source jar, fall back to filesystem discovery.
        if (auto it = source_classpath.find(string{jar}); it != source_classpath.end()) {
            roots.push_back({.path = it->second, .type = "SOURCES"});
        } else if (auto src = SourceResolver{jar}.find()) {
            roots.push_back({.path = std::move(*src), .type = "SOURCES"});
        }
        return {.name = file_stem(jar), .type = "java-imported", .roots = std::move(roots)};
    }

    static LibraryData library_from_jar(string_view jar) {
        return {.name = file_stem(jar), .type = "java-imported", .roots = {{.path = string{jar}}}};
    }

    // --- Workspace model conversion ---

    vector<SourceRootData> to_source_roots() const {
        const auto src_type = is_test() ? "java-test"s : "java-source"s;
        const auto res_type = is_test() ? "java-test-resource"s : "java-resource"s;
        auto to_src = [&](const auto& p) -> SourceRootData { return {.path = p, .type = src_type}; };
        auto to_res = [&](const auto& p) -> SourceRootData { return {.path = p, .type = res_type}; };
        // Exclude resource dirs from source pass -- they're added below as resource-type roots.
        auto roots = source_roots | not_in(resources_roots) | v::transform(to_src) | to_vector();
        roots.append_range(resources_roots | v::transform(to_res));
        return roots;
    }

    vector<LibraryData> collect_libraries_with_sources() const {
        auto to_lib = [&](const auto& jar) { return library_from_jar_with_sources(jar); };
        return compile_classpath | v::transform(to_lib) | to_vector();
    }

    vector<LibraryData> collect_libraries() const {
        return compile_classpath | v::transform(library_from_jar) | to_vector();
    }

    vector<LibraryDep> collect_library_deps() const {
        const auto scope = is_test() ? DependencyScope::test : DependencyScope::compile;
        auto to_dep = [&](const auto& jar) -> LibraryDep { return {.name = file_stem(jar), .scope = scope}; };
        return compile_classpath | v::transform(to_dep) | to_vector();
    }

    void describe(DescribeContext& ctx) const {
        ctx.add(format("    source set '{}': {} source root(s), {} compile classpath jar(s)",
            name,
            source_roots.size(),
            compile_classpath.size()));
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

    void describe(DescribeContext& ctx) const {
        if (is_skipped()) {
            ctx.add(format("  {} [SKIPPED: {}]", project_path, *skip_reason));
            return;
        }
        ctx.add(format("  {} ({} source set(s), {} plugin(s))", project_path, source_sets.size(), plugins.size()));
        ctx.describe_each(source_sets);
    }

    vector<SourceSet> active_sets(const GenerationOptions& opts) const {
        if (opts.include_tests) {
            return source_sets;
        }
        return source_sets | v::filter(std::not_fn(&SourceSet::is_test)) | to_vector();
    }

    ModuleData to_module(const vector<SourceSet>& sets) const {
        auto source_roots = sets | v::transform(&SourceSet::to_source_roots) | v::join | to_vector();
        vector<ContentRootData> content_roots;
        if (!source_roots.empty()) {
            content_roots.push_back({.path = project_dir, .source_roots = std::move(source_roots)});
        }

        auto lib_deps = sets | v::transform(&SourceSet::collect_library_deps) | v::join | unique_by(&LibraryDep::name);
        vector<DependencyData> deps(std::from_range, std::move(lib_deps));
        deps.emplace_back(InheritedSdk{});
        deps.emplace_back(ModuleSource{});

        return {
            .name = module_name(),
            .dependencies = std::move(deps),
            .content_roots = std::move(content_roots),
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

    /// Parse a JSON string into a GradleBuildOutput. Ignores unknown keys.
    static GradleBuildOutput from_json(string_view json_str) {
        GradleBuildOutput output;
        const auto ec = glz::read<ws_read_opts>(output, json_str);
        require(!ec, "Failed to parse Gradle JSON: {}", [&] { return glz::format_error(ec, json_str); });
        return output;
    }

    /// Extract JSON from raw Gradle stdout (between KLSPW_BEGIN/KLSPW_END) and parse it.
    static GradleBuildOutput from_raw_output(string_view raw_output) {
        auto json_str = extract_between(raw_output, {.open = "KLSPW_BEGIN", .close = "KLSPW_END"});
        require(json_str.has_value(), "KLSPW_BEGIN/KLSPW_END delimiters not found in Gradle output");
        return from_json(*json_str);
    }

    size_t active_project_count() const {
        return static_cast<size_t>(r::count_if(projects, std::not_fn(&GradleProject::is_skipped)));
    }

    strings describe() const {
        DescribeContext ctx{false};
        ctx.add(format("{} project(s), {} active", projects.size(), active_project_count()));
        ctx.describe_each(projects);
        return ctx.take_lines();
    }

    WorkspaceData to_workspace(const string& compiler_args_json, const GenerationOptions& options) const {
        auto active_projects = projects | v::filter(std::not_fn(&GradleProject::is_skipped)) | to_vector();

        auto to_active_sets = [&](const auto& p) { return p.active_sets(options); };
        auto sets_per_project = active_projects | v::transform(to_active_sets) | to_vector();

        auto to_libs = [&](const auto& ss) {
            return options.attach_sources ? ss.collect_libraries_with_sources() : ss.collect_libraries();
        };

        WorkspaceData ws;
        for (const auto& [proj, sets] : v::zip(active_projects, sets_per_project)) {
            ws.modules.push_back(proj.to_module(sets));
            ws.kotlin_settings.push_back(proj.to_kotlin_settings(compiler_args_json, sets));
            ws.libraries.append_range(sets | v::transform(to_libs) | v::join | to_vector());
        }

        // Deduplicate libraries by name, keeping first occurrence.
        ws.libraries = std::move(ws.libraries) | unique_by(&LibraryData::name);

        if (options.attach_sources) {
            // Libraries with >1 root have at least one SOURCES entry (CLASSES is always first).
            const auto with_sources =
                static_cast<size_t>(r::count_if(ws.libraries, [](const auto& lib) { return lib.roots.size() > 1; }));
            spdlog::info("  sources attached to {}/{} libraries", with_sources, ws.libraries.size());
        }

        return ws;
    }
};

} // namespace klspw

// camel_case auto-converts snake_case C++ fields to camelCase JSON keys.
template <> struct glz::meta<klspw::SourceSet> : glz::camel_case {};
template <> struct glz::meta<klspw::GradleProject> : glz::camel_case {};
template <> struct glz::meta<klspw::GradleBuildOutput> : glz::camel_case {};
