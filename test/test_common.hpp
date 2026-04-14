#pragma once

/// Shared RAII test fixtures for temp files, directories, and log capture.

#include <atomic>
#include <filesystem>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "files.hpp"

namespace fs = std::filesystem;

struct TempConfig {
  fs::path path;

  explicit TempConfig(const std::string& content) {
    const auto dir = fs::temp_directory_path() / "klspw_test";
    fs::create_directories(dir);
    static std::atomic<int> counter{0};
    path = dir / ("config_" + std::to_string(counter++) + ".yaml");
    klspw::write_file(path, content);
  }

  ~TempConfig() {
    std::error_code ec;
    fs::remove(path, ec);
  }

  TempConfig(const TempConfig&) = delete;
  TempConfig& operator=(const TempConfig&) = delete;
  TempConfig(TempConfig&&) = delete;
  TempConfig& operator=(TempConfig&&) = delete;
};

struct TempDir {
  fs::path path;

  TempDir() {
    static std::atomic<int> counter{0};
    path = fs::temp_directory_path() / ("klspw_test_dir_" + std::to_string(counter++));
    fs::create_directories(path);
  }

  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;
};

/// RAII guard that removes a single file on destruction.
struct TempFile {
  fs::path path;

  explicit TempFile(fs::path p) : path{std::move(p)} {}

  ~TempFile() {
    std::error_code ec;
    fs::remove(path, ec);
  }

  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;
};

/// RAII log capture: installs an ostream sink, restores original on destruction.
/// stream is mutable so LogCapture can be const (clang-tidy) while the sink writes through it.
struct LogCapture {
  mutable std::ostringstream stream;
  std::shared_ptr<spdlog::logger> original;

  LogCapture() : original(spdlog::default_logger()->clone("backup")) {
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_st>(stream);
    sink->set_pattern("%v");
    spdlog::default_logger()->sinks() = {sink};
    spdlog::set_level(spdlog::level::trace);
  }

  ~LogCapture() {
    spdlog::default_logger()->sinks() = original->sinks();
    spdlog::set_level(spdlog::level::info);
  }

  std::string output() const { return stream.str(); }

  LogCapture(const LogCapture&) = delete;
  LogCapture& operator=(const LogCapture&) = delete;
  LogCapture(LogCapture&&) = delete;
  LogCapture& operator=(LogCapture&&) = delete;
};
