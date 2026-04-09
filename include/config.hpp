#pragma once

/// Config model for workspace-kotlin-lsp-config.yaml (version 1).
///
/// ConfigData and its nested structs are plain glaze-deserializable types
/// holding string paths matching the YAML. Config wraps ConfigData with the
/// config file path, validates fields, and resolves relative paths on demand.
/// ConfigData is never mutated after deserialization.
///
/// Example config:
///   version: 1
///   workspace_file: ./workspace.json
///   jvm_target: "21"
///   build:
///     command: ["./gradlew"]
///     gradle_args: ["--quiet"]
///   roots:
///     - path: ./src/my-service
///     - path: ./src/other-service
///       command: ["brazil-build", "gradle"]
///       gradle_args: ["--no-daemon"]
///   options:
///     include_tests: false

#include <glaze/yaml.hpp>

#include "common.hpp"

namespace klspw {

/// Behavioral flags controlling workspace generation.
struct GenerationOptions {
    bool include_tests = true; ///< Include test source sets in the workspace.
    bool attach_sources = true; ///< Discover and attach source jars to libraries (not yet implemented).
    bool follow_symlinks = true; ///< Follow symlinks when resolving paths (not yet implemented).
};

/// Gradle build command and extra arguments.
///
/// The resulting command line is:
///   {command...} --init-script {path} {gradle_args...} -p {root} dumpKotlinLspModel
struct BuildConfig {
    strings command; ///< Build tool executable and fixed args (e.g., ["./gradlew"] or ["brazil-build", "gradle"]).
    strings gradle_args; ///< Extra Gradle flags (e.g., ["--quiet", "--no-daemon"]).

    strings args_for(const fs::path& root, const fs::path& init_script) const {
        strings args;
        args.append_range(command);
        args.insert(args.end(), {"--init-script", init_script.string()});
        args.append_range(gradle_args);
        args.insert(args.end(), {"-p", root.string(), "dumpKotlinLspModel"});
        return args;
    }

    [[nodiscard]] bool empty() const { return command.empty(); }
};

/// A project root to process. Optionally overrides the global build config.
struct RootEntry {
    string path; ///< Relative or absolute path to the Gradle root project directory.
    strings command; ///< Per-root build command override. Empty = inherit global.
    strings gradle_args; ///< Per-root Gradle args override. Empty = inherit global.
};

/// Plain YAML-deserializable config data. Never mutated after deserialization.
struct ConfigData {
    int version = 0; ///< Config schema version (must be 1).
    string workspace_file; ///< Output workspace.json path (relative to config dir). Optional.
    string jvm_target = "21"; ///< JVM target version for kotlin-lsp compilerArguments.
    BuildConfig build; ///< Global build command and Gradle args.
    vector<RootEntry> roots; ///< Gradle root projects to process (at least one required).
    GenerationOptions options; ///< Behavioral flags for workspace generation.

    void validate() const {
        require(version != 0, "Config missing required field: version");
        require(version == 1, "Unsupported config version: {}", version);
        require(!roots.empty(), "Config missing required field: roots");
        for (const auto& root : roots) {
            require(!root.path.empty(), "Config missing required field: path");
        }
    }
};

/// Loaded and validated config. Resolves relative paths against config_dir on demand.
class Config {
  public:
    static Config from_yaml(const fs::path& config_path) {
        require(fs::exists(config_path), "Config file not found: {}", config_path);
        const auto yaml_str = read_file(config_path);
        ConfigData data;
        constexpr glz::opts yaml_opts{.format = glz::YAML, .error_on_unknown_keys = false};
        const auto ec = glz::read<yaml_opts>(data, yaml_str);
        require(!ec, "Failed to parse config: {}", [&] { return glz::format_error(ec, yaml_str); });
        data.validate();
        return Config{std::move(data), config_path};
    }

    const fs::path& config_file() const { return config_file_; }
    const fs::path& config_dir() const { return config_dir_; }

    // --- Forwarded accessors ---
    int version() const { return data_.version; }
    const string& jvm_target() const { return data_.jvm_target; }
    const BuildConfig& build() const { return data_.build; }
    const vector<RootEntry>& roots() const { return data_.roots; }
    const GenerationOptions& options() const { return data_.options; }

    /// Workspace output path resolved against config dir. Empty if not configured.
    fs::path workspace_file() const {
        if (data_.workspace_file.empty()) {
            return {};
        }
        return resolve(data_.workspace_file);
    }

    /// Root path resolved against config dir.
    fs::path root_path(const RootEntry& root) const { return resolve(root.path); }

    /// Resolve the effective build config for a root.
    /// Uses per-root command/gradle_args if present, otherwise falls back to the global.
    BuildConfig build_for(const RootEntry& root) const {
        return {
            .command = root.command.empty() ? data_.build.command : root.command,
            .gradle_args = root.gradle_args.empty() ? data_.build.gradle_args : root.gradle_args,
        };
    }

    /// Format compilerArguments for kotlin-lsp KotlinSettingsData.
    /// J prefix is a kotlin-lsp convention: J-prefixed JSON string for compiler args.
    string compiler_arguments_json() const { return format(R"(J{{"jvmTarget":"{}"}})", data_.jvm_target); }

    /// Check resolved paths and build commands. Throws on first issue found.
    void validate() const {
        if (const auto ws_file = workspace_file(); !ws_file.empty()) {
            const auto parent = ws_file.parent_path();
            require(parent.empty() || fs::is_directory(parent), "workspace_file parent does not exist: {}", parent);
        }

        for (const auto& root : data_.roots) {
            const auto resolved = root_path(root);
            require(fs::is_directory(resolved), "root path does not exist: {}", resolved);
        }

        require(has_any_build_command(), "no build command configured (global or per-root)");
    }

  private:
    explicit Config(ConfigData data, const fs::path& config_path) : data_{std::move(data)} {
        const auto canonical = fs::weakly_canonical(config_path);
        config_file_ = canonical;
        config_dir_ = canonical.parent_path();
    }

    fs::path resolve(const string& relative) const { return (config_dir_ / relative).lexically_normal(); }

    [[nodiscard]] bool has_any_build_command() const {
        return !data_.build.empty() || r::any_of(data_.roots, [](const auto& entry) { return !entry.command.empty(); });
    }

    ConfigData data_;
    fs::path config_file_;
    fs::path config_dir_;
};

} // namespace klspw
