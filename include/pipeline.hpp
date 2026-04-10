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
        log_workspace_details(workspace);
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

    /// Build workspace and log a full summary at info level (for inspect subcommand).
    void log_workspace() const {
        const auto ws = build_workspace();
        log_workspace_summary(ws, spdlog::level::info);
    }

  private:
    /// Log workspace contents at the given level (used by inspect at info, by build at debug).
    static void log_workspace_summary(const WorkspaceData& ws, spdlog::level::level_enum level) {
        spdlog::log(level, "Modules ({}):", ws.modules.size());
        for (const auto& mod : ws.modules) {
            const auto lib_deps =
                r::count_if(mod.dependencies, [](const auto& d) { return std::holds_alternative<LibraryDep>(d); });
            spdlog::log(level, "  {} ({} deps, {} content root(s))", mod.name, lib_deps, mod.content_roots.size());
            for (const auto& cr : mod.content_roots) {
                spdlog::log(level, "    root: {} ({} source root(s))", cr.path, cr.source_roots.size());
                for (const auto& sr : cr.source_roots) {
                    spdlog::log(level, "      {} [{}]", sr.path, sr.type);
                }
            }
        }

        /// Path markers that identify cache directories for compact library path display.
        static constexpr std::array cache_path_markers = {"/caches/", "/packages/"};

        spdlog::log(level, "Libraries ({}):", ws.libraries.size());
        set<string> cache_prefixes;
        for (const auto& lib : ws.libraries) {
            spdlog::log(level, "  {} ({} root(s))", lib.name, lib.roots.size());
            for (const auto& root : lib.roots) {
                auto [display, stripped] = strip_prefixes(root.path, cache_path_markers);
                if (!stripped.empty()) {
                    cache_prefixes.emplace(stripped);
                    spdlog::log(level, "    .../{}  [{}]", display, root.type);
                } else {
                    spdlog::log(level, "    {}  [{}]", display, root.type);
                }
            }
        }
        for (const auto& prefix : cache_prefixes) {
            spdlog::log(level, "  (cache: {})", prefix);
        }

        spdlog::log(level, "Kotlin settings ({}):", ws.kotlin_settings.size());
        for (const auto& ks : ws.kotlin_settings) {
            spdlog::log(level,
                "  module={}, {} source root(s), {} pure-kotlin folder(s)",
                ks.module,
                ks.source_roots.size(),
                ks.pure_kotlin_source_folders.size());
        }
    }

    /// Log workspace details at debug level (called after build_workspace).
    static void log_workspace_details(const WorkspaceData& ws) { log_workspace_summary(ws, spdlog::level::debug); }

    WorkspaceData build_root_workspace(const RootEntry& root) const {
        const auto build = cfg_.build_for(root);
        const auto root_path = cfg_.root_path(root);
        spdlog::info("Processing root: {}", root_path.string());
        spdlog::info("  build command: {}", join(build.command));
        spdlog::info("  gradle args: {}", build.gradle_args.empty() ? "(none)" : join(build.gradle_args));

        const auto raw_output = run_gradle_(build, root_path);
        spdlog::info("  Gradle raw output: {} bytes", raw_output.size());

        const auto json_str = extract_gradle_json(raw_output);
        spdlog::info("  extracted JSON: {} bytes", json_str.size());

        if (!gradle_output_path_.empty()) {
            save_gradle_output(raw_output, json_str);
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
        spdlog::info("  workspace: {} module(s), {} library(ies), {} kotlin setting(s)",
            ws.modules.size(),
            ws.libraries.size(),
            ws.kotlin_settings.size());
        return ws;
    }

    /// Save Gradle output to the configured path.
    /// .json extension saves extracted JSON; .txt (or anything else) saves raw output.
    void save_gradle_output(string_view raw_output, string_view extracted_json) const {
        const fs::path p{gradle_output_path_};
        if (const auto parent = p.parent_path(); !parent.empty()) {
            fs::create_directories(parent);
        }
        const auto content = p.extension() == ".json" ? extracted_json : raw_output;
        write_file(p, content);
        spdlog::info("  Saved gradle output to {}", p.string());
    }

    Config cfg_;
    GradleBuildFn run_gradle_;
    string gradle_output_path_;
};

} // namespace klspw
