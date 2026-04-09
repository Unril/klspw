#pragma once

/// Workspace generation pipeline.
///
/// Pipeline owns a Config (by value) and a build function (injected),
/// orchestrating workspace building, writing, and inspection.

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "gradle.hpp"
#include "gradle_output.hpp"

namespace klspw {

/// Orchestrates Gradle execution across config roots and produces WorkspaceData.
class Pipeline {
  public:
    Pipeline(Config cfg, GradleBuildFn run_gradle) : cfg_{std::move(cfg)}, run_gradle_{std::move(run_gradle)} {
        require(run_gradle_ != nullptr, "Pipeline: run_gradle must not be null");
    }

    /// Set where to save raw Gradle output for debugging.
    /// If path is a directory (or ends with /), saves as {path}/{root_name}_raw.txt and {path}/{root_name}.json.
    /// If path is a file, saves raw output there (only works well with a single root).
    void set_gradle_output_path(string path) { gradle_output_path_ = std::move(path); }

    /// Run Gradle on each root and merge results into a single WorkspaceData.
    [[nodiscard]] WorkspaceData build_workspace() const {
        WorkspaceData workspace;
        for (const auto& root : cfg_.roots()) {
            workspace.merge(build_root_workspace(root));
        }
        spdlog::info("Pipeline complete: {} module(s), {} library(ies), {} kotlin setting(s)",
            workspace.modules.size(),
            workspace.libraries.size(),
            workspace.kotlin_settings.size());
        return workspace;
    }

    /// Build workspace and write it as workspace.json.
    void write_workspace() const {
        const auto ws_path = cfg_.workspace_file();
        require(!ws_path.empty(), "workspace_file not configured in config");

        const auto workspace = build_workspace();

        const auto json = glz::write<ws_write_opts>(workspace);
        require(json.has_value(), "Failed to serialize workspace JSON");

        write_file(ws_path, json.value());
        spdlog::info("Wrote {} ({} bytes)", ws_path.string(), json->size());
    }

    /// Build workspace and log a summary of modules, libraries, and settings.
    void log_workspace() const { log_workspace_summary(build_workspace()); }

  private:
    /// Log a human-readable summary of workspace contents.
    static void log_workspace_summary(const WorkspaceData& ws) {
        spdlog::info("Modules ({}):", ws.modules.size());
        for (const auto& mod : ws.modules) {
            const auto lib_deps =
                r::count_if(mod.dependencies, [](const auto& d) { return std::holds_alternative<LibraryDep>(d); });
            spdlog::info("  {} ({} deps, {} content root(s))", mod.name, lib_deps, mod.content_roots.size());
            for (const auto& cr : mod.content_roots) {
                spdlog::info("    root: {} ({} source root(s))", cr.path, cr.source_roots.size());
            }
        }

        spdlog::info("Libraries ({}):", ws.libraries.size());
        for (const auto& lib : ws.libraries) {
            spdlog::info("  {} ({} root(s))", lib.name, lib.roots.size());
        }

        spdlog::info("Kotlin settings ({}):", ws.kotlin_settings.size());
        for (const auto& ks : ws.kotlin_settings) {
            spdlog::info("  module={}, {} source root(s), {} pure-kotlin folder(s)",
                ks.module,
                ks.source_roots.size(),
                ks.pure_kotlin_source_folders.size());
        }
    }

    WorkspaceData build_root_workspace(const RootEntry& root) const {
        const auto build = cfg_.build_for(root);
        const auto root_path = cfg_.root_path(root);
        const auto module_name = root_path.filename().string();
        spdlog::info("Processing root: {}", root_path.string());
        spdlog::debug("  build command: {}", join(build.command));
        spdlog::debug("  gradle args: {}", build.gradle_args.empty() ? "(none)" : join(build.gradle_args));

        const auto raw_output = run_gradle_(build, root_path);
        spdlog::debug("  Gradle raw output: {} bytes", raw_output.size());

        const auto json_str = extract_gradle_json(raw_output);
        spdlog::debug("  extracted JSON: {} bytes", json_str.size());

        if (!gradle_output_path_.empty()) {
            save_gradle_output(module_name, raw_output, json_str);
        }

        const auto build_output = parse_gradle_output(json_str);
        spdlog::info("  {} project(s), {} active", build_output.projects.size(), build_output.active_project_count());

        for (const auto& proj : build_output.projects) {
            if (proj.is_skipped()) {
                spdlog::debug("  project {} [SKIPPED: {}]", proj.project_path, *proj.skip_reason);
            } else {
                spdlog::debug("  project {} ({} source set(s), {} plugin(s))",
                    proj.project_path,
                    proj.source_sets.size(),
                    proj.plugins.size());
                for (const auto& ss : proj.source_sets) {
                    spdlog::debug("    source set '{}': {} source root(s), {} compile classpath jar(s)",
                        ss.name,
                        ss.source_roots.size(),
                        ss.compile_classpath.size());
                }
            }
        }

        const auto ws = build_output.to_workspace(cfg_.compiler_arguments_json(), cfg_.options());
        spdlog::debug("  workspace: {} module(s), {} library(ies), {} kotlin setting(s)",
            ws.modules.size(),
            ws.libraries.size(),
            ws.kotlin_settings.size());
        return ws;
    }

    void save_gradle_output(const string& root_name, string_view raw_output, string_view extracted_json) const {
        const auto p = fs::path{gradle_output_path_};
        if (fs::is_directory(p) || p.string().ends_with("/")) {
            // Directory mode: save both raw and extracted JSON with default names.
            fs::create_directories(p);
            write_file(p / format("{}_raw.txt", root_name), raw_output);
            write_file(p / format("{}.json", root_name), extracted_json);
            spdlog::info("  Saved gradle output to {}/{}[_raw.txt|.json]", p.string(), root_name);
        } else {
            // File mode: save raw output to the specified path.
            if (const auto parent = p.parent_path(); !parent.empty()) {
                fs::create_directories(parent);
            }
            write_file(p, raw_output);
            spdlog::info("  Saved gradle output to {}", p.string());
        }
    }

    Config cfg_;
    GradleBuildFn run_gradle_;
    string gradle_output_path_;
};

} // namespace klspw
