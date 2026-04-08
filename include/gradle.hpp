#pragma once

#include <fstream>

#include <unistd.h>

#include "config.hpp"
#include "init_script_content.hpp"
#include "process.hpp"

namespace klspw {

/// Manages the Gradle init script lifecycle and process execution.
///
/// Writes the embedded init script to a temp directory, then runs Gradle
/// against each kotlin_gradle root. Cleans up the script on destruction.
class GradleRunner {
  public:
    /// Construct with a BuildConfig. Uses system temp dir for the init script.
    explicit GradleRunner(BuildConfig build) : GradleRunner(std::move(build), fs::temp_directory_path() / "klspw") {}

    /// Construct with a BuildConfig and explicit temp directory (for testing).
    GradleRunner(BuildConfig build, const fs::path& temp_dir)
        : build_{std::move(build)}, init_script_path_{write_init_script(temp_dir)} {}

    ~GradleRunner() noexcept {
        std::error_code ec;
        fs::remove(init_script_path_, ec);
    }

    GradleRunner(const GradleRunner&) = delete;
    GradleRunner& operator=(const GradleRunner&) = delete;
    GradleRunner(GradleRunner&&) = delete;
    GradleRunner& operator=(GradleRunner&&) = delete;

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
        std::ofstream out(path);
        if (!out) {
            throw runtime_error(format("Failed to write init script to: {}", path.string()));
        }
        out << init_script_content;
        out.close();
        if (!out) {
            throw runtime_error(format("Failed to flush init script to: {}", path.string()));
        }
        return path;
    }
};

} // namespace klspw
