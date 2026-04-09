#pragma once

/// Workspace generation pipeline.
///
/// Pipeline owns a Config (by value) and a GradleRunner reference (injected),
/// orchestrating workspace building, writing, and inspection.

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "gradle.hpp"
#include "gradle_output.hpp"

namespace klspw {

/// Orchestrates Gradle execution across config roots and produces WorkspaceData.
class Pipeline {
  public:
    /// runner must outlive this Pipeline instance.
    Pipeline(Config cfg, const GradleRunner* runner) : cfg_{std::move(cfg)}, runner_{runner} {
        require(runner_ != nullptr, "Pipeline: runner must not be null");
    }

    /// Run Gradle on each root and merge results into a single WorkspaceData.
    [[nodiscard]] WorkspaceData build_workspace() const {
        WorkspaceData workspace;
        for (const auto& root : cfg_.roots()) {
            workspace.merge(build_root_workspace(root));
        }
        spdlog::info("Pipeline complete: {} module(s), {} library(ies), {} kotlin setting(s)", workspace.modules.size(),
                     workspace.libraries.size(), workspace.kotlin_settings.size());
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
    void log_workspace() const { build_workspace().log_summary(); }

  private:
    WorkspaceData build_root_workspace(const RootEntry& root) const {
        const auto build = cfg_.build_for(root);
        const auto root_path = cfg_.root_path(root);
        spdlog::info("Processing root: {}", root_path.string());

        const auto raw_output = runner_->run(build, root_path);
        spdlog::debug("Gradle output: {} bytes", raw_output.size());

        const auto json_str = GradleOutputParser::extract_json(raw_output);
        const auto build_output = GradleOutputParser::parse(json_str);
        spdlog::info("  {} project(s), {} active", build_output.projects.size(), build_output.active_project_count());

        return build_output.to_workspace(cfg_.compiler_arguments_json(), cfg_.options());
    }

    Config cfg_;
    const GradleRunner* runner_;
};

} // namespace klspw
