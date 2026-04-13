#pragma once

/// Generic subprocess execution with stdout capture.

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>
#include <spdlog/spdlog.h>

#include "common.hpp"
#include "describe.hpp"
#include "strings.hpp"

namespace klspw {

/// Runs a subprocess, captures stdout, passes stderr through to the parent.
/// Constructed with the command + args and optional working directory.
/// run() executes and returns stdout. Throws on any failure.
class ProcessRunner {
 public:
  explicit ProcessRunner(strings args, path cwd = {}) : args_{std::move(args)}, cwd_{std::move(cwd)} {}

  /// Execute the process. Returns captured stdout on success.
  string run() const {
    reproc::process proc;
    reproc::options opts;
    opts.redirect.err.type = reproc::redirect::parent;

    const auto cwd_str = cwd_.string();
    if (!cwd_.empty()) {
      opts.working_directory = cwd_str.c_str();
    }

    const auto cmd = join(args_);
    d_debug("exec: {} (cwd: {})", cmd, cwd_.empty() ? "." : cwd_str);

    const auto start_ec = proc.start(args_, opts);
    require(!start_ec, "Failed to start process: {}\n  Command: {}\n  Working directory: {}", start_ec, cmd,
            cwd_.empty() ? path{"."} : cwd_);

    string stdout_output;
    reproc::sink::discard discard_sink;
    const auto drain_ec = reproc::drain(proc, reproc::sink::string(stdout_output), discard_sink);
    require(!drain_ec, "Failed to read process output: {}\n  Command: {}\n  Working directory: {}", drain_ec, cmd,
            cwd_.empty() ? path{"."} : cwd_);

    const auto [status, wait_ec] = proc.wait(reproc::infinite);
    require(!wait_ec, "Failed waiting for process: {}\n  Command: {}\n  Working directory: {}", wait_ec, cmd,
            cwd_.empty() ? path{"."} : cwd_);

    d_debug("exec done: exit={}, stdout={} bytes", status, stdout_output.size());
    require(status == 0, "Process exited with code {}\n  Command: {}\n  Working directory: {}\n  Output: {:.500}",
            status, cmd, cwd_.empty() ? path{"."} : cwd_, stdout_output);

    return stdout_output;
  }

  const strings& args() const { return args_; }

  const path& cwd() const { return cwd_; }

 private:
  strings args_;
  path cwd_;
};

}  // namespace klspw
