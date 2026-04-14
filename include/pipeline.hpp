#pragma once

/// Workspace generation pipeline.
///
/// Pipeline owns a Config (by value) and a build function (injected),
/// orchestrating workspace building, writing, and inspection.

#include <fstream>

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "files.hpp"
#include "gradle.hpp"
#include "gradle_runner.hpp"
#include "native_stubs_content.hpp"
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
      inject_native_stubs(ws);
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
  struct RootResult {
    WorkspaceData workspace;
    string_map<string_set> project_deps;
    bool has_kmp = false;
  };

  RootResult build_root_workspace(const RootEntry& root) const {
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

    const auto has_kmp_project = r::any_of(build_output.projects, [](const auto& p) {
      return !p.is_skipped() && (p.kind == "kmp" || p.kind == "kmp-android");
    });

    return {
        .workspace = build_output.to_workspace(cfg_.data().jvm_target, cfg_.data().options),
        .project_deps = build_output.collect_project_deps(),
        .has_kmp = has_kmp_project,
    };
  }

  static constexpr auto native_stubs_lib_name = "kotlin-native-stubs"sv;

  /// Write the embedded kotlin-native-stubs.jar next to workspace.json and add it as a library.
  /// KMP projects use kotlin.native annotations (HiddenFromObjC, etc.) that only exist in
  /// Kotlin/Native metadata (.knm/.klib), not in JVM bytecode. kotlin-lsp can't resolve them
  /// without JVM .class stubs.
  void inject_native_stubs(WorkspaceData& ws) const {
    const auto ws_dir = cfg_.workspace_file().parent_path();
    const auto stubs_dir = ws_dir / ".klspw";
    fs::create_directories(stubs_dir);
    const auto stubs_path = stubs_dir / "kotlin-native-stubs.jar";

    // Write the embedded jar to disk.
    std::ofstream out(stubs_path, std::ios::binary);
    require(out.good(), "Failed to open {} for writing", stubs_path);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) -- writing embedded binary data
    out.write(reinterpret_cast<const char*>(native_stubs_jar.data()),
              static_cast<std::streamsize>(native_stubs_jar.size()));
    out.close();
    require(out.good(), "Failed to write {}", stubs_path);

    // Add as a library and make all modules depend on it.
    const auto stubs_path_str = stubs_path.string();
    ws.libraries.push_back({
        .name = string(native_stubs_lib_name),
        .type = string(library_type_imported),
        .roots = {{.path = stubs_path_str}},
    });
    for (auto& mod : ws.modules) {
      mod.dependencies.emplace_back(LibraryDep{.name = string(native_stubs_lib_name)});
    }
    d_info("  injected kotlin-native-stubs.jar for KMP annotation resolution");
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
  path gradle_output_path_;
};

}  // namespace klspw
