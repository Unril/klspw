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
  explicit ProcessRunner(strings args, opt_path cwd = {}) : args_{std::move(args)}, cwd_{std::move(cwd)} {}

  /// Execute the process. Returns captured stdout on success.
  string run() const {
    reproc::process proc;
    reproc::options opts;
    opts.redirect.err.type = reproc::redirect::parent;

    string cwd_str;
    if (cwd_) {
      cwd_str = cwd_->string();
      opts.working_directory = cwd_str.c_str();
    }

    const auto cmd = args_ | join_to_string();
    const auto display_cwd = cwd_ ? cwd_str : "."s;
    d_debug("exec: {} (cwd: {})", cmd, display_cwd);

    auto fail = [&](string_view action, string_view detail) {
      return format("{}: {}\n  Command: {}\n  Working directory: {}", action, detail, cmd, display_cwd);
    };

    const auto start_ec = proc.start(args_, opts);
    require(!start_ec, "{}", [&] { return fail("Failed to start process", start_ec.message()); });

    string stdout_output;
    reproc::sink::discard discard_sink;
    const auto drain_ec = reproc::drain(proc, reproc::sink::string(stdout_output), discard_sink);
    require(!drain_ec, "{}", [&] { return fail("Failed to read process output", drain_ec.message()); });

    const auto [status, wait_ec] = proc.wait(reproc::infinite);
    require(!wait_ec, "{}", [&] { return fail("Failed waiting for process", wait_ec.message()); });

    d_debug("exec done: exit={}, stdout={} bytes", status, stdout_output.size());
    require(status == 0, "{}",
            [&] { return fail(format("Process exited with code {}", status), format("{:.500}", stdout_output)); });

    return stdout_output;
  }

  const strings& args() const { return args_; }

  const opt_path& cwd() const { return cwd_; }

 private:
  strings args_;
  opt_path cwd_;
};

}  // namespace klspw
