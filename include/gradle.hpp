#pragma once

#include <spdlog/spdlog.h>
#include <unistd.h>

#include "config.hpp"
#include "init_script_content.hpp"
#include "process.hpp"

namespace klspw {

/// Manages the Gradle init script lifecycle and process execution.
///
/// Writes the embedded init script to a temp directory on construction.
/// Each run() call takes a BuildConfig + root path, assembles the command,
/// and delegates to ProcessRunner. The script is removed on close() or destruction.
class GradleRunner {
  public:
    GradleRunner(const GradleRunner&) = delete;
    GradleRunner& operator=(const GradleRunner&) = delete;
    GradleRunner(GradleRunner&&) = delete;
    GradleRunner& operator=(GradleRunner&&) = delete;

    /// Uses system temp dir for the init script.
    GradleRunner() : GradleRunner(fs::temp_directory_path() / "klspw") {}

    /// Explicit temp directory (for testing).
    explicit GradleRunner(const fs::path& temp_dir) : init_script_path_{write_init_script(temp_dir)} {}

    ~GradleRunner() noexcept {
        try {
            close();
        } catch (const std::exception& e) {
            spdlog::warn("Failed to clean up init script: {}", e.what());
        }
    }

    /// Remove the init script from disk.
    /// Safe to call multiple times -- subsequent calls are no-ops.
    void close() {
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
    string run(const BuildConfig& build, const fs::path& root) const {
        return ProcessRunner(build.args_for(root, init_script_path_)).run();
    }

    const fs::path& init_script_path() const { return init_script_path_; }

  private:
    fs::path init_script_path_;

    static fs::path write_init_script(const fs::path& dir) {
        fs::create_directories(dir);
        const auto path = dir / format("init.{}.gradle.kts", ::getpid());
        write_file(path, init_script_content);
        return path;
    }
};

} // namespace klspw
