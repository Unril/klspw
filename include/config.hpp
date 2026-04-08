#pragma once

/// Config model for workspace-kotlin-lsp-config.yaml (version 1).
/// Owns YAML loading, path normalization (relative to config file dir),
/// and Gradle argument assembly.

#include "common.hpp" // IWYU pragma: keep
#include "yaml.hpp"

namespace klspw {

/// kotlin_gradle: Gradle-based Kotlin/JVM project, runs init script.
/// java_binary: pre-built Java project, scans lib_dir for jars directly.
enum class RootKind : std::uint8_t {
    kotlin_gradle,
    java_binary,
};

inline RootKind root_kind_from_string(string_view s) {
    if (s == "kotlin_gradle") {
        return RootKind::kotlin_gradle;
    }
    if (s == "java_binary") {
        return RootKind::java_binary;
    }
    throw runtime_error(format("Unknown root kind: {}", s));
}

/// Gradle build command and extra arguments.
/// Owns gradle_args_for() which assembles the full command line for a root.
class BuildConfig {
  public:
    const strings& command() const { return command_; }
    const strings& gradle_args() const { return gradle_args_; }

    /// Assemble full Gradle command: command... --init-script <path> gradle_args... -p <root> dumpKotlinLspModel
    strings gradle_args_for(const fs::path& root, const fs::path& init_script) const {
        strings args;
        args.reserve(command_.size() + gradle_args_.size() + 4);
        for (const auto& part : command_) {
            args.push_back(part);
        }
        args.emplace_back("--init-script");
        args.push_back(init_script.string());
        for (const auto& arg : gradle_args_) {
            args.push_back(arg);
        }
        args.emplace_back("-p");
        args.push_back(root.string());
        args.emplace_back("dumpKotlinLspModel");
        return args;
    }

  private:
    friend class Config;
    strings command_;
    strings gradle_args_;
};

/// Boolean flags controlling workspace generation behavior.
struct Options {
    bool include_tests = false;   ///< Include test source sets in modules.
    bool attach_sources = true;   ///< Discover and attach source jars to libraries.
    bool follow_symlinks = true;  ///< Follow symlinks during source discovery.
};

/// A project root to process. Paths are absolute (normalized against config dir).
/// lib_dir is relative to path; only used for java_binary roots.
class RootEntry {
  public:
    explicit RootEntry(RootKind kind, fs::path path, fs::path lib_dir = "build/lib")
        : kind_{kind}, path_{std::move(path)}, lib_dir_{std::move(lib_dir)} {}

    RootKind kind() const { return kind_; }
    const fs::path& path() const { return path_; }
    const fs::path& lib_dir() const { return lib_dir_; }
    fs::path resolved_lib_dir() const { return path_ / lib_dir_; }

  private:
    RootKind kind_;
    fs::path path_;
    fs::path lib_dir_;
};

/// Top-level config loaded from workspace-kotlin-lsp-config.yaml.
/// All paths are normalized to absolute at load time (relative to config file dir).
class Config {
  public:
    static Config from_yaml(const fs::path& config_path) {
        if (!fs::exists(config_path)) {
            throw runtime_error(format("Config file not found: {}", config_path.string()));
        }
        const auto node = YAML::LoadFile(config_path.string());
        const auto config_dir = fs::absolute(config_path).parent_path();
        return Config{node, config_dir};
    }

    int version() const { return version_; }
    const fs::path& workspace_file() const { return workspace_file_; }
    const string& jvm_target() const { return jvm_target_; }
    const BuildConfig& build() const { return build_; }
    const vector<RootEntry>& roots() const { return roots_; }
    const Options& options() const { return options_; }

    /// Format compilerArguments for kotlin-lsp KotlinSettingsData.
    /// Returns J{"jvmTarget":"<target>"} -- JSON-in-string prefixed with 'J'.
    string compiler_arguments_json() const {
        return format(R"(J{{"jvmTarget":"{}"}})", jvm_target_);
    }

  private:
    explicit Config(const YAML::Node& node, const fs::path& config_dir) {
        version_ = read<int>(node, "version");
        if (version_ != 1) {
            throw runtime_error(format("Unsupported config version: {}", version_));
        }

        if (node["workspace_file"]) {
            workspace_file_ = (config_dir / read<string>(node, "workspace_file")).lexically_normal();
        }

        jvm_target_ = read_or<string>(node, "jvm_target", "21");

        if (const auto& build = node["build"]) {
            build_.command_ = read_all(build, "command");
            build_.gradle_args_ = read_all(build, "gradle_args");
        }

        const auto& roots_node = read<YAML::Node>(node, "roots");
        for (const auto& root_node : roots_node) {
            roots_.push_back(parse_root_entry(root_node, config_dir));
        }

        if (const auto& opts = node["options"]) {
            options_.include_tests = read_or(opts, "include_tests", false);
            options_.attach_sources = read_or(opts, "attach_sources", true);
            options_.follow_symlinks = read_or(opts, "follow_symlinks", true);
        }
    }

    static RootEntry parse_root_entry(const YAML::Node& node, const fs::path& config_dir) {
        const auto kind = root_kind_from_string(read<string>(node, "kind"));
        auto path = (config_dir / read<string>(node, "path")).lexically_normal();
        auto lib_dir = fs::path{read_or<string>(node, "lib_dir", "build/lib")};
        return RootEntry{kind, std::move(path), std::move(lib_dir)};
    }

    int version_ = 1;
    fs::path workspace_file_;
    string jvm_target_ = "21";
    BuildConfig build_;
    vector<RootEntry> roots_;
    Options options_;
};

}
