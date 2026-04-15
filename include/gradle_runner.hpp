// SPDX-FileCopyrightText: 2026 Nikolai Fedorov
//
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <thread>

#include "config.hpp"
#include "files.hpp"
#include "init_script_content.hpp"
#include "process_runner.hpp"

namespace klspw {

/// Signature for running a Gradle build: (BuildConfig, root_path) -> stdout.
/// std::move_only_function would be a better fit (move-only captures), but Apple libc++ lacks it.
using GradleBuildFn = std::function<string(const BuildConfig&, const path&)>;

/// Manages the Gradle init script lifecycle and process execution.
///
/// Writes the embedded init script to a temp directory on construction.
/// Each run() call takes a BuildConfig + root path, assembles the command,
/// and delegates to ProcessRunner. The script is removed on close() or destruction.
///
/// Movable but not copyable (unique ownership of the temp init script file).
class GradleRunner {
 public:
  /// Uses system temp dir for the init script.
  GradleRunner() : GradleRunner(fs::temp_directory_path() / "klspw") {}

  /// Explicit temp directory (for testing).
  explicit GradleRunner(const path& temp_dir) : init_script_path_{write_init_script(temp_dir)} {
    d_debug("GradleRunner: init script at {}", init_script_path_->string());
  }

  ~GradleRunner() noexcept { close(); }

  GradleRunner(GradleRunner&& other) noexcept : init_script_path_{std::exchange(other.init_script_path_, nullopt)} {}

  GradleRunner& operator=(GradleRunner&& other) noexcept {
    if (this != &other) {
      close();
      init_script_path_ = std::exchange(other.init_script_path_, nullopt);
    }
    return *this;
  }

  GradleRunner(const GradleRunner&) = delete;
  GradleRunner& operator=(const GradleRunner&) = delete;

  /// Remove the init script from disk.
  /// Safe to call multiple times -- subsequent calls are no-ops.
  void close() noexcept {
    if (!init_script_path_) {
      return;
    }
    const auto path_copy = *init_script_path_;
    init_script_path_ = nullopt;
    std::error_code ec;
    fs::remove(path_copy, ec);
    if (ec) {
      d_warn("Failed to remove init script {}: {}", path_copy, ec);
    }
  }

  /// Run Gradle against a root directory with the given build config. Returns captured stdout.
  string operator()(const BuildConfig& build, const path& root) const {
    require(init_script_path_.has_value(), "GradleRunner: init script not available (already closed?)");
    return ProcessRunner(build.args_for(*init_script_path_), root).run();
  }

  const opt_path& init_script_path() const { return init_script_path_; }

 private:
  opt_path init_script_path_;

  static path write_init_script(const path& dir) {
    const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const auto path = dir / format("init.{}.gradle.kts", tid);
    write_file(path, init_script_content);
    return path;
  }
};

}  // namespace klspw
