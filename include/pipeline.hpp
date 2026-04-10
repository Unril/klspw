#pragma once

/// Workspace generation pipeline.
///
/// Pipeline owns a Config (by value) and a build function (injected),
/// orchestrating workspace building, writing, and inspection.

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "gradle.hpp"
#include "gradle_output.hpp"
#include "workspace_model.hpp"

namespace klspw {

/// Path markers that identify cache directories for compact library path display.
inline constexpr std::array<string_view, 2> cache_path_markers = {"/caches/", "/packages/"};

/// Log each line from a describe() result at the given level.
inline void log_lines(spdlog::level::level_enum level, const strings& lines) {
    for (const auto& line : lines) {
        spdlog::log(level, "{}", line);
    }
}

/// Orchestrates Gradle execution across config roots and produces WorkspaceData.
class Pipeline {
  public:
    Pipeline(Config cfg, GradleBuildFn run_gradle) : cfg_{std::move(cfg)}, run_gradle_{std::move(run_gradle)} {
        require(run_gradle_ != nullptr, "Pipeline: run_gradle must not be null");
    }

    /// Set where to save raw Gradle output for debugging.
    void set_gradle_output_path(string path) { gradle_output_path_ = std::move(path); }

    /// Run Gradle on each root and merge results into a single WorkspaceData.
    [[nodiscard]] WorkspaceData build_workspace() const {
        WorkspaceData ws;
        for (const auto& root : cfg_.roots()) {
            ws.merge(build_root_workspace(root));
        }
        spdlog::info("Pipeline complete: {} module(s), {} library(ies), {} kotlin setting(s)",
            ws.modules.size(),
            ws.libraries.size(),
            ws.kotlin_settings.size());
        log_lines(spdlog::level::debug, ws.describe(true, cache_path_markers));
        return ws;
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
        log_lines(spdlog::level::info, ws.describe(true, cache_path_markers));
    }

  private:
    WorkspaceData build_root_workspace(const RootEntry& root) const {
        const auto build = cfg_.build_for(root);
        const auto root_path = cfg_.root_path(root);
        spdlog::info("Processing root: {}", root_path.string());
        spdlog::info("  build command: {}", join(build.command));
        spdlog::info("  gradle args: {}", build.gradle_args.empty() ? "(none)" : join(build.gradle_args));

        const auto raw_output = run_gradle_(build, root_path);
        spdlog::info("  Gradle output: {} bytes", raw_output.size());

        if (!gradle_output_path_.empty()) {
            save_gradle_output(raw_output);
        }

        const auto build_output = GradleBuildOutput::from_raw_output(raw_output);
        log_lines(spdlog::level::info, build_output.describe());

        WorkspaceData ws = build_output.to_workspace(cfg_.compiler_arguments_json(), cfg_.options());
        spdlog::info("  workspace: {} module(s), {} library(ies), {} kotlin setting(s)",
            ws.modules.size(),
            ws.libraries.size(),
            ws.kotlin_settings.size());
        return ws;
    }

    /// Save Gradle output to the configured path.
    void save_gradle_output(string_view raw_output) const {
        const fs::path p{gradle_output_path_};
        if (const auto parent = p.parent_path(); !parent.empty()) {
            fs::create_directories(parent);
        }
        write_file(p, raw_output);
        spdlog::info("  Saved gradle output to {}", p.string());
    }

    Config cfg_;
    GradleBuildFn run_gradle_;
    string gradle_output_path_;
};

} // namespace klspw
