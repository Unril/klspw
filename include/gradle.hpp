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
#include "module_matcher.hpp"
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
/// All path fields are plain strings -- path operations happen at usage sites.
struct SourceSet {
  string name;  ///< Source set name (e.g., "main", "test", "integrationTest").
  strings source_roots;  ///< All source dirs (kotlin + java + resources).
  string_set java_source_roots;  ///< Java-only source dirs (subset of source_roots).
  string_set resources_roots;  ///< Resource directories.
  strings classes_dirs;  ///< Compiled .class output directories.
  opt_string resources_dir;  ///< Processed resources output directory.
  strings compile_classpath;  ///< Jars available at compile time.
  strings runtime_classpath;  ///< Jars available at runtime.
  string_map<string> source_classpath;  ///< Classes jar -> source jar (resolved by Gradle).
  string_map<string> classpath_coordinates;  ///< Jar path -> "group:module:version" (from Gradle component IDs).
  string compile_classpath_configuration_name;  ///< Gradle configuration name for compile classpath.
  string runtime_classpath_configuration_name;  ///< Gradle configuration name for runtime classpath.

  /// Cached Gradle module cache root directory (lazily discovered from classpath entries).
  mutable optional<path> gradle_cache_root_;

  void describe() const {
    d_info(
        "    source set '{}'{}: {} source root(s), {} compile classpath jar(s), {} source jar "
        "mapping(s), {} coordinate mapping(s)",
        name, is_test() ? " [test]" : "", source_roots.size(), compile_classpath.size(), source_classpath.size(),
        classpath_coordinates.size());
    d_debug("      java_source_roots: {}, resources_roots: {}, classes_dirs: {}, runtime_classpath: {}",
            java_source_roots.size(), resources_roots.size(), classes_dirs.size(), runtime_classpath.size());
    d_debug("      resources_dir: {}", resources_dir.value_or("(none)"));
    d_debug("      compile_config: {}, runtime_config: {}", compile_classpath_configuration_name,
            runtime_classpath_configuration_name);
    for (const auto& root : source_roots) {
      d_debug("      source: {}", root);
    }
    for (const auto& root : resources_roots) {
      d_debug("      resource: {}", root);
    }
    for (const auto& jar : compile_classpath) {
      d_trace("      classpath: {}", jar);
    }
  }

  /// Heuristic: any source set whose name contains "test" or "Test".
  bool is_test() const { return name.contains("test") || name.contains("Test"); }

  /// Remove entries from path collections that don't exist on disk.
  /// Classpath entries matching a sibling module name are preserved even if missing,
  /// because inter-project dependency jars may not exist until the project is built.
  void remove_missing_paths(const string_set& preserve_modules = {}) {
    const ModuleMatcher matcher(preserve_modules);
    const auto missing = [](const auto& path) {
      if (!fs::exists(path)) {
        d_debug("  missing: {}", path);
        return true;
      }
      return false;
    };
    const auto removed_sources = std::erase_if(source_roots, missing);
    const auto removed_classpath = std::erase_if(compile_classpath, [&](const auto& p) {
      if (!fs::exists(p)) {
        if (!matcher.empty() && matcher.classpath_matches(p)) {
          d_debug("  missing but preserved (module dep): {}", p);
          return false;
        }
        d_debug("  missing: {}", p);
        return true;
      }
      return false;
    });
    const auto removed_classes = std::erase_if(classes_dirs, missing);
    // java_source_roots and resources_roots are subsets of source_roots — already logged above.
    std::erase_if(java_source_roots, missing);
    std::erase_if(resources_roots, missing);

    const auto total = removed_sources + removed_classpath + removed_classes;
    if (total > 0) {
      d_warn("source set '{}': removed {} missing path(s) (sources: {}, classpath: {}, classes: {})", name, total,
             removed_sources, removed_classpath, removed_classes);
    }
  }

  /// Source roots not in java_source_roots and not in resources_roots.
  /// Used to populate KotlinSettingsData.pure_kotlin_source_folders.
  strings pure_kotlin_roots() const {
    return source_roots | not_in(java_source_roots) | not_in(resources_roots) | to_vector();
  }

  /// Derive a library name for a jar on this source set's classpath.
  /// Priority: classpath_coordinates (Gradle component ID) > JarPath (Gradle cache path) > file stem.
  string jar_library_name(string_view jar) const {
    if (auto it = classpath_coordinates.find(string{jar}); it != classpath_coordinates.end()) {
      return it->second;
    }
    return JarPath{jar}.library_name();
  }

  /// Find sources jar in the Gradle cache by Maven coordinates.
  /// Extracts the cache root from any Gradle cache jar on this source set's classpath.
  opt_string find_sources_by_coords(string_view coords) const {
    if (!gradle_cache_root_.has_value()) {
      // Lazily discover the Gradle cache root from the first Gradle cache jar on the classpath.
      for (const auto& jar : compile_classpath) {
        if (auto root = JarPath::gradle_cache_root(jar)) {
          gradle_cache_root_ = std::move(*root);
          break;
        }
      }
    }
    if (!gradle_cache_root_.has_value()) {
      return nullopt;
    }
    return JarPath::find_sources_by_coordinates(*gradle_cache_root_, coords);
  }

  /// Build a LibraryData with source jar attachment.
  /// Prefers Gradle-resolved source mapping, falls back to JarPath filesystem discovery,
  /// then coordinate-based Gradle cache search for AGP transforms.
  LibraryData library_from_jar_with_sources(string_view jar) const {
    vector<LibraryRootData> roots{{.path = string{jar}}};
    if (auto it = source_classpath.find(string{jar}); it != source_classpath.end()) {
      roots.push_back({.path = it->second, .type = string(root_type_sources)});
    } else if (auto src = JarPath{jar}.find_sources()) {
      roots.push_back({.path = std::move(*src), .type = string(root_type_sources)});
    } else if (auto coord_it = classpath_coordinates.find(string{jar}); coord_it != classpath_coordinates.end()) {
      // AGP transforms: use coordinates to find sources in the Gradle cache.
      if (auto src = find_sources_by_coords(coord_it->second)) {
        roots.push_back({.path = std::move(*src), .type = string(root_type_sources)});
      }
    }
    return {.name = jar_library_name(jar), .type = string(library_type_imported), .roots = std::move(roots)};
  }

  /// Build a LibraryData without source attachment. Type "java-imported" is required by kotlin-lsp.
  LibraryData library_from_jar(string_view jar) const {
    return {.name = jar_library_name(jar), .type = string(library_type_imported), .roots = {{.path = string{jar}}}};
  }

  /// Convert source_roots into typed SourceRootData entries.
  /// Resource dirs are excluded from the source pass and added separately as resource-type roots.
  vector<SourceRootData> to_source_roots() const {
    const auto src_type = string(is_test() ? source_type_test : source_type_java);
    const auto res_type = string(is_test() ? source_type_test_resource : source_type_resource);
    auto to_src = [&](const auto& p) -> SourceRootData { return {.path = p, .type = src_type}; };
    auto to_res = [&](const auto& p) -> SourceRootData { return {.path = p, .type = res_type}; };
    // Exclude resource dirs from source pass -- they're added below as resource-type roots.
    auto roots = source_roots | not_in(resources_roots) | v::transform(to_src) | to_vector();
    roots.append_range(resources_roots | v::transform(to_res));
    return roots;
  }

  /// Collect libraries from compile classpath with source jar attachment.
  vector<LibraryData> collect_libraries_with_sources() const {
    auto to_lib = [&](const auto& jar) { return library_from_jar_with_sources(jar); };
    return compile_classpath | v::transform(to_lib) | to_vector();
  }

  /// Collect libraries from compile classpath without source attachment.
  vector<LibraryData> collect_libraries() const {
    auto to_lib = [&](const auto& jar) { return library_from_jar(jar); };
    return compile_classpath | v::transform(to_lib) | to_vector();
  }

  /// Collect library dependency references from compile classpath.
  /// Scope is inferred from the source set name (test vs compile).
  vector<LibraryDep> collect_library_deps() const {
    const auto scope = is_test() ? DependencyScope::test : DependencyScope::compile;
    auto to_dep = [&](const auto& jar) -> LibraryDep { return {.name = jar_library_name(jar), .scope = scope}; };
    return compile_classpath | v::transform(to_dep) | to_vector();
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
  string project_path;  ///< Gradle project path (e.g., ":", ":subproject").
  string project_name;  ///< Gradle project name (from settings.gradle.kts).
  string project_dir;  ///< Absolute path to the project directory.
  string kind;  ///< Project kind (e.g., "jvm", "non-jvm").
  strings plugins;  ///< Applied Gradle plugin class names.
  vector<SourceSet> source_sets;  ///< All source sets discovered by the init script.
  strings project_dependencies;  ///< Explicit project dependency names (from project(":core") declarations).
  strings compiler_plugin_classpath;  ///< Kotlin compiler plugin jars (serialization, compose, etc.).
  opt_string skip_reason;  ///< If set, project is excluded from workspace generation.

  void describe() const {
    if (is_skipped()) {
      d_info("  {} [SKIPPED: {}]", project_path, *skip_reason);
      return;
    }
    d_info("  {} (name: {}, dir: {}, kind: {}, {} source set(s), {} plugin(s))", project_path, project_name,
           project_dir, kind, source_sets.size(), plugins.size());
    d_describe(source_sets);
  }

  bool is_skipped() const { return skip_reason.has_value(); }

  /// Gradle project name. Falls back to directory name if not provided (forward compat).
  string module_name() const { return project_name.empty() ? path{project_dir}.filename().string() : project_name; }

  bool is_android() const { return kind == "android" || kind == "kmp-android"; }

  /// Return source sets to include in the workspace.
  /// For Android projects, picks one main variant (first = debug) and its test variant
  /// to avoid CLASSIFIER_REDECLARATION errors from variant-specific source dirs.
  /// For JVM/KMP projects, returns all source sets (optionally filtering tests).
  vector<SourceSet> active_sets(const GenerationOptions& opts) const {
    if (is_android() && source_sets.size() > 1) {
      return pick_android_variant(opts);
    }
    if (opts.include_tests) {
      return source_sets;
    }
    return source_sets | v::filter(std::not_fn(&SourceSet::is_test)) | to_vector();
  }

  /// Pick one Android build variant (debug preferred) and its test variant.
  /// Android projects emit source sets per variant (debug, release, debugUnitTest, etc.).
  /// Including all variants causes CLASSIFIER_REDECLARATION errors because variant-specific
  /// source dirs (src/debug/kotlin, src/release/kotlin) contain different implementations
  /// of the same class. kotlin-lsp doesn't understand Android variants, so we pick one.
  vector<SourceSet> pick_android_variant(const GenerationOptions& opts) const {
    // Find the first non-test source set (typically "debug").
    const auto main_it = r::find_if(source_sets, std::not_fn(&SourceSet::is_test));
    if (main_it == source_sets.end()) {
      return {};
    }
    const auto& main_name = main_it->name;  // e.g., "debug"

    vector<SourceSet> result;
    result.push_back(*main_it);

    if (opts.include_tests) {
      // Find the matching test variant: "{main_name}UnitTest" (e.g., "debugUnitTest").
      const auto test_name = main_name + "UnitTest";
      const auto test_it = r::find(source_sets, test_name, &SourceSet::name);
      if (test_it != source_sets.end()) {
        result.push_back(*test_it);
      }
      // Also include android-prefixed test variant if present (KMP-android).
      auto upper_first = string{main_name};
      upper_first[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(upper_first[0])));
      const auto android_test_name = "android" + upper_first + "UnitTest";
      const auto android_test_it = r::find(source_sets, android_test_name, &SourceSet::name);
      if (android_test_it != source_sets.end()) {
        result.push_back(*android_test_it);
      }
    }

    d_debug("  android variant: picked '{}' ({} source set(s) of {})", main_name, result.size(), source_sets.size());
    return result;
  }

  /// Convert this project into a ModuleData with content roots and dependencies.
  /// Dependencies include library deps (deduped by name) plus InheritedSdk and ModuleSource.
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

  /// Build J-prefixed compiler arguments JSON with jvmTarget and plugin classpaths.
  string build_compiler_arguments(string_view jvm_target) const {
    if (compiler_plugin_classpath.empty()) {
      return format(R"(J{{"jvmTarget":"{}"}})", jvm_target);
    }
    // Escape for JSON string values. Only \ and " need escaping for filesystem paths.
    auto escape_json = [](const string& s) {
      string result;
      for (const auto c : s) {
        if (c == '\\' || c == '"') {
          result += '\\';
        }
        result += c;
      }
      return result;
    };
    auto to_json_str = [&](const string& s) { return format("\"{}\"", escape_json(s)); };
    auto paths = compiler_plugin_classpath | v::transform(to_json_str);
    return format(R"(J{{"jvmTarget":"{}","pluginClasspaths":[{}]}})", jvm_target, join(paths, ","));
  }

  /// Build KotlinSettingsData for this project.
  /// compiler_arguments format: J-prefixed JSON, e.g. J{"jvmTarget":"21","pluginClasspaths":[...]}.
  KotlinSettingsData to_kotlin_settings(const string& jvm_target, const vector<SourceSet>& sets) const {
    const auto mod_name = module_name();
    return {
        .name = "Kotlin",
        .source_roots = sets | v::transform(&SourceSet::source_roots) | v::join | to_vector(),
        .module = mod_name,
        .external_project_id = format(":{}:unspecified", mod_name),
        .pure_kotlin_source_folders = sets | v::transform(&SourceSet::pure_kotlin_roots) | v::join | to_vector(),
        .compiler_arguments = build_compiler_arguments(jvm_target),
    };
  }
};

// --- GradleBuildOutput ---

/// Top-level Gradle build output emitted between KLSPW_BEGIN/KLSPW_END delimiters.
/// Mirrors the root JSON object from resources/init.gradle.kts: rootProject + projects.
/// to_workspace() is the main entry point for the Gradle -> workspace.json conversion.
struct GradleBuildOutput {
  string root_project;  ///< Absolute path to the Gradle root project directory.
  vector<GradleProject> projects;  ///< All discovered (sub)projects, including skipped ones.

  void describe() const {
    d_info("{} project(s), {} active (root: {})", projects.size(), active_project_count(), root_project);
    d_describe(projects);
  }

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

  /// Collect explicit project dependency mappings from active projects.
  /// Returns module_name -> set of dependency module names.
  string_map<string_set> collect_project_deps() const {
    string_map<string_set> deps;
    for (const auto& proj : projects) {
      if (proj.is_skipped() || proj.project_dependencies.empty()) {
        continue;
      }
      deps[proj.module_name()] = proj.project_dependencies | to_set();
    }
    return deps;
  }

  /// Convert all active projects into a merged WorkspaceData.
  /// Libraries are deduplicated by name (first occurrence wins).
  WorkspaceData to_workspace(const string& jvm_target, const GenerationOptions& options) const {
    auto active_projects = projects | v::filter(std::not_fn(&GradleProject::is_skipped)) | to_vector();

    auto to_active_sets = [&](const auto& p) { return p.active_sets(options); };
    auto sets_per_project = active_projects | v::transform(to_active_sets) | to_vector();

    if (options.remove_missing_paths) {
      const auto mod_names = active_projects | v::transform(&GradleProject::module_name) | to_set();
      for (auto& ss : sets_per_project | v::join) {
        ss.remove_missing_paths(mod_names);
      }
    }

    auto to_libs = [&](const auto& ss) {
      return options.attach_sources ? ss.collect_libraries_with_sources() : ss.collect_libraries();
    };

    WorkspaceData ws;
    for (const auto& [proj, sets] : v::zip(active_projects, sets_per_project)) {
      ws.modules.push_back(proj.to_module(sets));
      ws.kotlin_settings.push_back(proj.to_kotlin_settings(jvm_target, sets));
      ws.libraries.append_range(sets | v::transform(to_libs) | v::join | to_vector());
    }

    // Deduplicate libraries by name, keeping first occurrence.
    ws.libraries = std::move(ws.libraries) | unique_by(&LibraryData::name);

    if (options.attach_sources) {
      const auto with_sources = r::count_if(ws.libraries, &LibraryData::has_sources);
      d_info("  sources attached to {}/{} libraries", with_sources, ws.libraries.size());
    }

    return ws;
  }
};

}  // namespace klspw

// camel_case auto-converts snake_case C++ fields to camelCase JSON keys.
template <>
struct glz::meta<klspw::SourceSet> : glz::camel_case {};

template <>
struct glz::meta<klspw::GradleProject> : glz::camel_case {};

template <>
struct glz::meta<klspw::GradleBuildOutput> : glz::camel_case {};
