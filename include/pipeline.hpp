#pragma once

/// Workspace generation pipeline.
///
/// Pipeline owns a Config (by value) and a build function (injected),
/// orchestrating workspace building, writing, and inspection.

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "describe.hpp"
#include "files.hpp"
#include "gradle.hpp"
#include "gradle_runner.hpp"
#include "strings.hpp"
#include "workspace.hpp"

namespace klspw {

/// Path markers that identify cache directories for compact library path display.
inline constexpr std::array<string_view, 2> cache_path_markers = {"/caches/", "/packages/"};

/// Orchestrates Gradle execution across config roots and produces WorkspaceData.
class Pipeline {
  public:
    Pipeline(Config cfg, GradleBuildFn run_gradle) : cfg_{std::move(cfg)}, run_gradle_{std::move(run_gradle)} {
        require(run_gradle_ != nullptr, "Pipeline: run_gradle must not be null");
    }

    /// Set where to save raw Gradle output for debugging.
    void set_gradle_output_path(fs::path path) { gradle_output_path_ = std::move(path); }

    /// Run Gradle on each root and merge results into a single WorkspaceData.
    [[nodiscard]] WorkspaceData build_workspace() const {
        WorkspaceData ws;
        for (const auto& root : cfg_.data().roots) {
            ws.merge(build_root_workspace(root));
        }
        spdlog::info("Pipeline complete: {} module(s), {} library(ies), {} kotlin setting(s)",
            ws.modules.size(),
            ws.libraries.size(),
            ws.kotlin_settings.size());
        return ws;
    }

    /// Build workspace and write it as workspace.json.
    void write_workspace() const {
        const auto ws_path = cfg_.workspace_file();
        require(!ws_path.empty(), "workspace_file not configured in config");

        build_workspace().save_json_file(ws_path);
        spdlog::info("Wrote {}", ws_path.string());
    }

    /// Build workspace and log a full summary at info level (for inspect subcommand).
    void log_workspace() const {
        DescribeContext ctx{true, cache_path_markers};
        build_workspace().describe(ctx);
        ctx.log(spdlog::level::info);
    }

  private:
    WorkspaceData build_root_workspace(const RootEntry& root) const {
        const auto build = cfg_.data().build_for(root);
        const auto root_path = cfg_.root_path(root);
        spdlog::info("Processing root: {}", root_path.string());
        spdlog::info("  build command: {}", join(build.command));
        spdlog::info("  gradle args: {}", build.has_args() ? join(build.gradle_args) : "(none)");

        const auto raw_output = run_gradle_(build, root_path);
        spdlog::info("  Gradle output: {} bytes", raw_output.size());

        if (!gradle_output_path_.empty()) {
            save_gradle_output(raw_output);
        }

        const auto build_output = GradleBuildOutput::from_raw_output(raw_output);
        DescribeContext build_ctx;
        build_output.describe(build_ctx);
        build_ctx.log(spdlog::level::info);

        WorkspaceData ws = build_output.to_workspace(cfg_.data().compiler_arguments_json(), cfg_.data().options);
        spdlog::info("  workspace: {} module(s), {} library(ies), {} kotlin setting(s)",
            ws.modules.size(),
            ws.libraries.size(),
            ws.kotlin_settings.size());
        return ws;
    }

    /// Save Gradle output to the configured path.
    void save_gradle_output(string_view raw_output) const {
        if (const auto parent = gradle_output_path_.parent_path(); !parent.empty()) {
            fs::create_directories(parent);
        }
        write_file(gradle_output_path_, raw_output);
        spdlog::info("  Saved gradle output to {}", gradle_output_path_.string());
    }

    Config cfg_;
    GradleBuildFn run_gradle_;
    fs::path gradle_output_path_;
};

} // namespace klspw
