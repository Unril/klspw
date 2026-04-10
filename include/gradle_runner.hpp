#pragma once

#include <functional>
#include <thread>

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "files.hpp"
#include "init_script_content.hpp"
#include "process_runner.hpp"

namespace klspw {

/// Signature for running a Gradle build: (BuildConfig, root_path) -> stdout.
/// std::move_only_function would be a better fit (move-only captures), but Apple libc++ lacks it.
using GradleBuildFn = std::function<string(const BuildConfig&, const fs::path&)>;

/// Manages the Gradle init script lifecycle and process execution.
///
/// Writes the embedded init script to a temp directory on construction.
/// Each run() call takes a BuildConfig + root path, assembles the command,
/// and delegates to ProcessRunner. The script is removed on close() or destruction.
///
/// Movable but not copyable (unique ownership of the temp init script file).
class GradleRunner {
  public:
    GradleRunner(const GradleRunner&) = delete;
    GradleRunner& operator=(const GradleRunner&) = delete;

    GradleRunner(GradleRunner&& other) noexcept : init_script_path_{std::move(other.init_script_path_)} {
        other.init_script_path_.clear();
    }

    GradleRunner& operator=(GradleRunner&& other) noexcept {
        if (this != &other) {
            close();
            init_script_path_ = std::move(other.init_script_path_);
            other.init_script_path_.clear();
        }
        return *this;
    }

    /// Uses system temp dir for the init script.
    GradleRunner() : GradleRunner(fs::temp_directory_path() / "klspw") {}

    /// Explicit temp directory (for testing).
    explicit GradleRunner(const fs::path& temp_dir) : init_script_path_{write_init_script(temp_dir)} {
        spdlog::debug("GradleRunner: init script at {}", init_script_path_.string());
    }

    ~GradleRunner() noexcept { close(); }

    /// Remove the init script from disk.
    /// Safe to call multiple times -- subsequent calls are no-ops.
    void close() noexcept {
        if (init_script_path_.empty()) {
            return;
        }
        const auto path_copy = init_script_path_;
        init_script_path_.clear();
        std::error_code ec;
        fs::remove(path_copy, ec);
        if (ec) {
            spdlog::warn("Failed to remove init script {}: {}", path_copy.string(), ec.message());
        }
    }

    /// Run Gradle against a root directory with the given build config. Returns captured stdout.
    string operator()(const BuildConfig& build, const fs::path& root) const {
        return ProcessRunner(build.args_for(init_script_path_), root).run();
    }

    const fs::path& init_script_path() const { return init_script_path_; }

  private:
    fs::path init_script_path_;

    static fs::path write_init_script(const fs::path& dir) {
        fs::create_directories(dir);
        const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
        const auto path = dir / format("init.{}.gradle.kts", tid);
        write_file(path, init_script_content);
        return path;
    }
};

} // namespace klspw
