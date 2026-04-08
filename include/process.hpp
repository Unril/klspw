#pragma once

/// Generic subprocess execution with stdout capture.

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>

#include "common.hpp"

namespace klspw {

/// Runs a subprocess, captures stdout, passes stderr through to the parent.
/// Constructed with the command + args; run() executes and returns stdout.
/// Throws on any failure (start, I/O, wait, or non-zero exit).
class ProcessRunner {
  public:
    explicit ProcessRunner(strings args) : args_{std::move(args)} {}

    /// Execute the process. Returns captured stdout on success.
    string run() const {
        reproc::process proc;
        reproc::options opts;
        opts.redirect.err.type = reproc::redirect::parent;

        const auto cmd = join(args_);

        const auto start_ec = proc.start(args_, opts);
        if (start_ec) {
            throw runtime_error(format("Failed to start process: {}\n  Command: {}", start_ec.message(), cmd));
        }

        string stdout_output;
        reproc::sink::discard discard_sink;
        const auto drain_ec = reproc::drain(proc, reproc::sink::string(stdout_output), discard_sink);
        if (drain_ec) {
            throw runtime_error(format("Failed to read process output: {}\n  Command: {}", drain_ec.message(), cmd));
        }

        const auto [status, wait_ec] = proc.wait(reproc::infinite);
        if (wait_ec) {
            throw runtime_error(format("Failed waiting for process: {}\n  Command: {}", wait_ec.message(), cmd));
        }

        if (status != 0) {
            throw runtime_error(
                format("Process exited with code {}\n  Command: {}\n  Output: {:.500}", status, cmd, stdout_output));
        }

        return stdout_output;
    }

    const strings& args() const { return args_; }

  private:
    strings args_;
};

} // namespace klspw
