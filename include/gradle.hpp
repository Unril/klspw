#pragma once

#include <spdlog/spdlog.h>
#include <unistd.h>

#include "config.hpp"
#include "init_script_content.hpp"
#include "process.hpp"

namespace klspw {

/// Manages the Gradle init script lifecycle and process execution.
///
/// Writes the embedded init script to a temp directory on construction,
/// then runs Gradle against each kotlin_gradle root via run().
/// The script is removed on close() or destruction.
class GradleRunner {
  public:
    GradleRunner(const GradleRunner&) = delete;
    GradleRunner& operator=(const GradleRunner&) = delete;
    GradleRunner(GradleRunner&&) = delete;
    GradleRunner& operator=(GradleRunner&&) = delete;

    /// Construct with a BuildConfig. Uses system temp dir for the init script.
    explicit GradleRunner(BuildConfig build) : GradleRunner(std::move(build), fs::temp_directory_path() / "klspw") {}

    /// Construct with a BuildConfig and explicit temp directory (for testing).
    GradleRunner(BuildConfig build, const fs::path& temp_dir)
        : build_{std::move(build)}, init_script_path_{write_init_script(temp_dir)} {}

    ~GradleRunner() noexcept {
        try {
            close();
        } catch (const std::exception& e) {
            spdlog::warn("Failed to clean up init script: {}", e.what());
        }
    }

    /// Remove the init script from disk. Throws on failure.
    /// Safe to call multiple times -- subsequent calls are no-ops.
    void close() {
        if (init_script_path_.empty()) {
            return;
        }
        const auto path_copy = init_script_path_;
        init_script_path_.clear();
        std::error_code ec;
        const auto removed = fs::remove(path_copy, ec);
        require(removed && !ec, "Failed to remove init script {}: {}", path_copy, ec);
    }

    /// Run Gradle against a root directory. Returns captured stdout.
    string run(const fs::path& root) const {
        return ProcessRunner(build_.gradle_args_for(root, init_script_path_)).run();
    }

    const fs::path& init_script_path() const { return init_script_path_; }

  private:
    BuildConfig build_;
    fs::path init_script_path_;

    static fs::path write_init_script(const fs::path& dir) {
        fs::create_directories(dir);
        const auto path = dir / format("init.{}.gradle.kts", ::getpid());
        write_file(path, init_script_content);
        return path;
    }
};

} // namespace klspw
