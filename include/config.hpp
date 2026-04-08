#pragma once

/// Config model for workspace-kotlin-lsp-config.yaml (version 1).
///
/// The config file describes which project roots to process, how to invoke
/// Gradle, and behavioral flags for workspace generation. Paths in the YAML
/// are relative to the config file's directory and normalized to absolute
/// at load time.
///
/// Parsing uses glaze YAML into intermediate raw structs, followed by
/// validation and path normalization in the Config constructor.
///
/// Example config:
///   version: 1
///   workspace_file: ./workspace.json
///   jvm_target: "21"
///   build:
///     command: ["./gradlew"]
///     gradle_args: ["--quiet"]
///   roots:
///     - kind: kotlin_gradle
///       path: ./src/my-service
///     - kind: java_binary
///       path: ./src/my-tool
///       lib_dir: build/lib
///   options:
///     include_tests: false

#include <utility>

#include <glaze/yaml.hpp>

#include "common.hpp"

namespace klspw {

/// Root kind determines how a project root is processed.
///   kotlin_gradle: Gradle-based Kotlin/JVM project. The tool runs Gradle with
///                  an init script to extract source sets, classpaths, and metadata.
///   java_binary:   Pre-built Java project. The tool scans lib_dir for jars
///                  directly, skipping Gradle entirely.
enum class RootKind : std::uint8_t {
    kotlin_gradle,
    java_binary,
};

/// Parse a root kind string from YAML. Throws on unknown values.
inline RootKind root_kind_from_string(string_view s) {
    if (s == "kotlin_gradle") {
        return RootKind::kotlin_gradle;
    }
    if (s == "java_binary") {
        return RootKind::java_binary;
    }
    require(false, "Unknown root kind: {}", s);
    std::unreachable();
}

/// Gradle build command and extra arguments.
/// Assembled into a full command line by gradle_args_for().
///
/// The resulting command line is:
///   <command...> --init-script <path> <gradle_args...> -p <root> dumpKotlinLspModel
class BuildConfig {
  public:
    const strings& command() const { return command_; } ///< Build wrapper command (e.g., ["./gradlew"]).
    const strings& gradle_args() const { return gradle_args_; } ///< Extra Gradle flags (e.g., ["--quiet"]).

    /// Assemble the full Gradle command line for a given root and init script.
    strings gradle_args_for(const fs::path& root, const fs::path& init_script) const {
        strings args;
        args.reserve(command_.size() + gradle_args_.size() + 4);
        args.append_range(command_);
        args.emplace_back("--init-script");
        args.push_back(init_script.string());
        args.append_range(gradle_args_);
        args.emplace_back("-p");
        args.push_back(root.string());
        args.emplace_back("dumpKotlinLspModel");
        return args;
    }

  private:
    friend class Config;
    strings command_; ///< Build wrapper command tokens.
    strings gradle_args_; ///< Extra Gradle arguments.
};

/// Boolean flags controlling workspace generation behavior.
struct Options {
    bool include_tests = false; ///< Include test source sets in modules and kotlin settings.
    bool attach_sources = true; ///< Discover and attach source jars to libraries.
    bool follow_symlinks = true; ///< Follow symlinks during source discovery.
};

/// A project root to process.
/// Paths are absolute (normalized against config file directory at load time).
/// lib_dir is relative to path; only meaningful for java_binary roots.
class RootEntry {
  public:
    explicit RootEntry(RootKind kind, fs::path path, fs::path lib_dir = "build/lib")
        : kind_{kind}, path_{std::move(path)}, lib_dir_{std::move(lib_dir)} {}

    RootKind kind() const { return kind_; } ///< How this root is processed.
    const fs::path& path() const { return path_; } ///< Absolute path to the project root.
    const fs::path& lib_dir() const { return lib_dir_; } ///< Relative jar directory (java_binary only).
    fs::path resolved_lib_dir() const { return path_ / lib_dir_; } ///< Absolute path to jar directory.

  private:
    RootKind kind_;
    fs::path path_;
    fs::path lib_dir_;
};

// --- Raw YAML config structs ---
// Deserialized by glaze from YAML, then validated and normalized by Config.
// Field names match the YAML keys exactly (snake_case).

namespace raw {

struct RootEntryRaw {
    string kind;
    string path;
    string lib_dir = "build/lib";
};

struct BuildRaw {
    strings command;
    strings gradle_args;
};

struct OptionsRaw {
    bool include_tests = false;
    bool attach_sources = true;
    bool follow_symlinks = true;
};

struct ConfigRaw {
    int version = 0; ///< 0 means "not present in YAML" (detected during validation).
    string workspace_file;
    string jvm_target = "21";
    BuildRaw build;
    vector<RootEntryRaw> roots;
    OptionsRaw options;
};

} // namespace raw

/// Top-level config loaded from workspace-kotlin-lsp-config.yaml.
/// All paths are normalized to absolute at load time (relative to config file dir).
/// The config file is parsed in two phases:
///   1. Glaze YAML deserializes into raw:: structs (no validation, no path normalization).
///   2. Config constructor validates required fields and normalizes paths.
class Config {
  public:
    /// Load and validate config from a YAML file.
    /// Throws runtime_error on missing file, parse failure, or validation errors.
    static Config from_yaml(const fs::path& config_path) {
        require(fs::exists(config_path), "Config file not found: {}", config_path);

        const auto yaml_str = read_file(config_path);

        raw::ConfigRaw raw;
        constexpr glz::opts yaml_opts{.format = glz::YAML, .error_on_unknown_keys = false};
        const auto ec = glz::read<yaml_opts>(raw, yaml_str);
        require(!ec, "Failed to parse config: {}", [&] { return glz::format_error(ec, yaml_str); });

        const auto config_dir = fs::weakly_canonical(config_path).parent_path();
        return Config{raw, config_dir};
    }

    int version() const { return version_; } ///< Config schema version (currently 1).
    const fs::path& workspace_file() const { return workspace_file_; } ///< Output workspace.json path.
    const string& jvm_target() const { return jvm_target_; } ///< JVM target for compilerArguments (e.g., "21").
    const BuildConfig& build() const { return build_; } ///< Gradle build command and args.
    const vector<RootEntry>& roots() const { return roots_; } ///< Project roots to process.
    const Options& options() const { return options_; } ///< Behavioral flags.

    /// Format compilerArguments for kotlin-lsp KotlinSettingsData.
    /// Returns J{"jvmTarget":"<target>"} -- JSON-in-string prefixed with 'J'.
    string compiler_arguments_json() const { return format(R"(J{{"jvmTarget":"{}"}})", jvm_target_); }

  private:
    explicit Config(const raw::ConfigRaw& raw, fs::path config_dir)
        : version_{raw.version}, config_dir_{std::move(config_dir)} {
        require(!config_dir_.empty(), "Config directory is empty");
        require(version_ != 0, "Config missing required field: version");
        require(version_ == 1, "Unsupported config version: {}", version_);

        if (!raw.workspace_file.empty()) {
            workspace_file_ = resolve(raw.workspace_file);
        }

        jvm_target_ = raw.jvm_target;

        build_.command_ = raw.build.command;
        build_.gradle_args_ = raw.build.gradle_args;

        require(!raw.roots.empty(), "Config missing required field: roots");
        roots_.assign_range(raw.roots | v::transform([&](const auto& r) { return parse_root_entry(r); }));

        options_.include_tests = raw.options.include_tests;
        options_.attach_sources = raw.options.attach_sources;
        options_.follow_symlinks = raw.options.follow_symlinks;
    }

    /// Resolve a relative path against the config file directory.
    fs::path resolve(const string& relative) const { return (config_dir_ / relative).lexically_normal(); }

    RootEntry parse_root_entry(const raw::RootEntryRaw& raw) const {
        require(!raw.kind.empty(), "Config missing required field: kind");
        require(!raw.path.empty(), "Config missing required field: path");
        const auto kind = root_kind_from_string(raw.kind);
        auto path = resolve(raw.path);
        auto lib_dir = fs::path{raw.lib_dir};
        return RootEntry{kind, std::move(path), std::move(lib_dir)};
    }

    int version_ = 1;
    fs::path config_dir_;
    fs::path workspace_file_;
    string jvm_target_ = "21";
    BuildConfig build_;
    vector<RootEntry> roots_;
    Options options_;
};

} // namespace klspw
