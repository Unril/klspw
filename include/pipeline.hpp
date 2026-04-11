#pragma once

/// Workspace generation pipeline.
///
/// Pipeline owns a Config (by value) and a build function (injected),
/// orchestrating workspace building, writing, and inspection.

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "files.hpp"
#include "gradle.hpp"
#include "gradle_runner.hpp"
#include "strings.hpp"
#include "workspace.hpp"

namespace klspw {

/// Orchestrates Gradle execution across config roots and produces WorkspaceData.
class Pipeline {
  public:
    Pipeline(Config cfg, GradleBuildFn run_gradle) : cfg_{std::move(cfg)}, run_gradle_{std::move(run_gradle)} {
        require(run_gradle_ != nullptr, "Pipeline: run_gradle must not be null");
    }

    /// Set where to save raw Gradle output for debugging.
    void set_gradle_output_path(fs::path path) { gradle_output_path_ = std::move(path); }

    /// Run Gradle on each root and merge results into a single WorkspaceData.
    WorkspaceData build_workspace() const {
        WorkspaceData ws;
        for (const auto& root : cfg_.data().roots) {
            ws.merge(build_root_workspace(root));
        }
        const auto promoted = ws.promote_module_deps();
        if (promoted > 0) {
            d_info("  promoted {} library deps to module deps ({} libraries remaining)", promoted, ws.libraries.size());
        }
        ws.describe();
        return ws;
    }

    /// Build workspace and write it as workspace.json.
    void write_workspace() const {
        const auto ws_path = cfg_.workspace_file();
        require(!ws_path.empty(), "workspace_file not configured in config");

        build_workspace().save_json_file(ws_path);
        d_info("Wrote {}", ws_path);
    }

    /// Build workspace and log a full summary (for inspect subcommand).
    void log_workspace() const { build_workspace(); }

  private:
    WorkspaceData build_root_workspace(const RootEntry& root) const {
        const auto build = cfg_.data().build_for(root);
        const auto root_path = cfg_.root_path(root);
        d_info("Processing root: {}", root_path.string());
        d_info("  build command: {}", join(build.command));
        d_info("  gradle args: {}", build.has_args() ? join(build.gradle_args) : "(none)");

        const auto raw_output = run_gradle_(build, root_path);
        d_info("  Gradle output: {} bytes", raw_output.size());

        if (!gradle_output_path_.empty()) {
            save_gradle_output(raw_output);
        }

        const auto build_output = GradleBuildOutput::from_raw_output(raw_output);
        build_output.describe();

        return build_output.to_workspace(cfg_.data().compiler_arguments_json(), cfg_.data().options);
    }

    /// Save Gradle output to the configured path.
    void save_gradle_output(string_view raw_output) const {
        if (const auto parent = gradle_output_path_.parent_path(); !parent.empty()) {
            fs::create_directories(parent);
        }
        write_file(gradle_output_path_, raw_output);
        d_info("  Saved gradle output to {}", gradle_output_path_.string());
    }

    Config cfg_;
    GradleBuildFn run_gradle_;
    fs::path gradle_output_path_;
};

} // namespace klspw
