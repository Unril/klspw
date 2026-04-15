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
#include "native_stubs_content.hpp"
#include "workspace.hpp"

namespace klspw {

/// Orchestrates Gradle execution across config roots and produces WorkspaceData.
class Pipeline {
 public:
  Pipeline(Config cfg, GradleBuildFn run_gradle) : cfg_{std::move(cfg)}, run_gradle_{std::move(run_gradle)} {
    require(run_gradle_ != nullptr, "Pipeline: run_gradle must not be null");
  }

  /// Set where to save raw Gradle output for debugging.
  void set_gradle_output_path(path path) { gradle_output_path_ = std::move(path); }

  /// Run Gradle on each root and merge results into a single WorkspaceData.
  WorkspaceData build_workspace() const {
    WorkspaceData ws;
    string_map<string_set> all_project_deps;
    bool has_kmp = false;
    for (const auto& root : cfg_.data().roots) {
      auto [root_ws, root_deps, root_has_kmp] = build_root_workspace(root);
      has_kmp = has_kmp || root_has_kmp;
      ws.merge(std::move(root_ws));
      all_project_deps.insert(root_deps.begin(), root_deps.end());
    }
    const auto promoted = ws.promote_module_deps(all_project_deps);
    if (promoted > 0) {
      d_info("  promoted {} library deps to module deps ({} libraries remaining)", promoted, ws.libraries.size());
    }
    if (has_kmp) {
      const auto ws_path = cfg_.workspace_file();
      require(ws_path.has_value(), "workspace_file required for native stubs injection");
      inject_native_stubs(ws, *ws_path);
    }
    ws.describe();
    return ws;
  }

  /// Build workspace and write it as workspace.json.
  void write_workspace() const {
    const auto ws_path = cfg_.workspace_file();
    require(ws_path.has_value(), "workspace_file not configured in config");

    build_workspace().save_json_file(*ws_path);
    d_info("Wrote {}", ws_path->string());
  }

  /// Build workspace and log a full summary (for inspect subcommand).
  void log_workspace() const { build_workspace(); }

 private:
  struct RootResult {
    WorkspaceData workspace;
    string_map<string_set> project_deps;
    bool has_kmp = false;
  };

  RootResult build_root_workspace(const RootEntry& root) const {
    const auto build = cfg_.data().build_for(root);
    const auto root_path = cfg_.root_path(root);
    d_info("Processing root: {}", root_path.string());
    d_info("  build command: {}", build.command | join_to_string());
    d_info("  gradle args: {}", build.has_args() ? (build.gradle_args | join_to_string()) : "(none)"s);

    const auto raw_output = run_gradle_(build, root_path);
    d_info("  Gradle output: {} bytes", raw_output.size());

    if (gradle_output_path_) {
      save_gradle_output(raw_output);
    }

    const auto build_output = GradleBuildOutput::from_raw_output(raw_output);
    build_output.describe();

    return {
        .workspace = build_output.to_workspace(cfg_.data().jvm_target, cfg_.data().options),
        .project_deps = build_output.collect_project_deps(),
        .has_kmp = build_output.has_kmp_project(),
    };
  }

  /// Write the embedded kotlin-native-stubs.jar next to workspace.json and add it as a library.
  static void inject_native_stubs(WorkspaceData& ws, const path& ws_path) {
    const auto lib_name = "kotlin-native-stubs"s;
    const auto stubs_dir = ws_path.parent_path() / ".klspw";
    const auto stubs_path = stubs_dir / (lib_name + ".jar");

    write_binary_file(stubs_path, native_stubs_jar);

    ws.add_lib({
        .name = lib_name,
        .type = string(library_type_imported),
        .roots = {{.path = stubs_path.string()}},
    });
    for (auto& mod : ws.modules) {
      mod.add_dep(LibraryDep{.name = lib_name});
    }
    d_info("  injected kotlin-native-stubs.jar for KMP annotation resolution");
  }

  /// Save Gradle output to the configured path.
  void save_gradle_output(string_view raw_output) const {
    write_file(*gradle_output_path_, raw_output);
    d_info("  Saved gradle output to {}", gradle_output_path_->string());
  }

  Config cfg_;
  GradleBuildFn run_gradle_;
  opt_path gradle_output_path_;
};

}  // namespace klspw
