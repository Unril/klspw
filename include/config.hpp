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
///       build:
///         command: ["gradle"]
///         gradle_args: ["--no-daemon"]
///   options:
///     include_tests: false

#include <glaze/yaml.hpp>
#include <spdlog/spdlog.h>

#include "common.hpp"

namespace klspw {

/// Gradle task name registered by the init script.
inline constexpr string_view gradle_task = "dumpKotlinLspModel";

/// Behavioral flags controlling workspace generation.
struct GenerationOptions {
    bool include_tests = true; ///< Include test source sets in the workspace.
    bool attach_sources = true; ///< Discover and attach source jars (-sources.jar) to libraries.
    bool follow_symlinks = true; ///< Follow symlinks when resolving paths (not yet implemented).
};

/// Gradle build command and extra arguments.
///
/// The process runs in the root project directory. The resulting command line is:
///   {command...} --init-script {path} {gradle_args...} dumpKotlinLspModel
struct BuildConfig {
    strings command; ///< Build tool executable and fixed args (e.g., ["./gradlew"]).
    strings gradle_args; ///< Extra Gradle flags (e.g., ["--quiet", "--no-daemon"]).

    /// Build the full command-line args for a Gradle invocation.
    /// The resulting command line is:
    ///   {command...} --init-script {path} {gradle_args...} dumpKotlinLspModel
    strings args_for(const fs::path& init_script) const {
        strings args;
        args.append_range(command);
        args.insert(args.end(), {"--init-script", init_script.string()});
        args.append_range(gradle_args);
        args.emplace_back(gradle_task);
        return args;
    }

    [[nodiscard]] bool empty() const { return command.empty(); }
};

/// A project root to process. Optionally overrides the global build config.
struct RootEntry {
    string path; ///< Relative or absolute path to the Gradle root project directory.
    optional<BuildConfig> build; ///< Per-root build override. Absent = inherit global.
};

/// Plain YAML-deserializable config data. Never mutated after deserialization.
struct ConfigData {
    int version = 0; ///< Config schema version (must be 1).
    string workspace_file; ///< Output workspace.json path (relative to config dir). Optional.
    string jvm_target = "21"; ///< JVM target version for kotlin-lsp compilerArguments.
    optional<BuildConfig> build; ///< Global build command and Gradle args. Optional.
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

    /// Serialize to YAML string.
    string to_yaml() const {
        constexpr glz::opts yaml_opts{.format = glz::YAML, .skip_null_members = true, .prettify = true};
        auto yaml_str = glz::write<yaml_opts>(*this);
        require(yaml_str.has_value(), "Failed to serialize config to YAML");
        return std::move(yaml_str).value();
    }

    /// Deserialize from a YAML string. Validates after parsing.
    static ConfigData from_yaml(string_view yaml_str) {
        ConfigData data;
        constexpr glz::opts yaml_opts{.format = glz::YAML, .error_on_unknown_keys = false};
        const auto ec = glz::read<yaml_opts>(data, yaml_str);
        require(!ec, "Failed to parse config YAML: {}", [&] { return glz::format_error(ec, yaml_str); });
        data.validate();
        return data;
    }
};

/// Loaded and validated config. Resolves relative paths against config_dir on demand.
class Config {
  public:
    static constexpr string_view default_filename = "klspw.yaml";
    static constexpr string_view default_workspace_file = "./workspace.json";
    static constexpr string_view default_gradle_command = "./gradlew";

    /// Resolve a config path: if it's a directory, append the default filename.
    static fs::path resolve_path(const fs::path& path) {
        return fs::is_directory(path) ? path / default_filename : path;
    }

    /// Load config from a YAML file. Validates after parsing.
    /// If config_path is a directory, appends "klspw.yaml".
    static Config from_yaml(const fs::path& config_path) {
        const auto resolved = resolve_path(config_path);
        require(fs::exists(resolved), "Config file not found: {}", resolved);
        spdlog::info("Loading config: {}", resolved.string());
        auto data = ConfigData::from_yaml(read_file(resolved));
        auto cfg = Config{std::move(data), resolved};
        for (const auto& line : cfg.describe()) {
            spdlog::info("{}", line);
        }
        return cfg;
    }

    /// Save this config's data as YAML to a file.
    void save(const fs::path& config_path) const { write_file(config_path, data_.to_yaml()); }

    /// Serialize this config's data to a YAML string.
    string to_yaml() const { return data_.to_yaml(); }

    /// Build a starter ConfigData for a single Gradle root.
    /// root_path is resolved relative to config_dir. Both paths are canonicalized internally.
    static ConfigData
    make_starter(const fs::path& root_path, const fs::path& config_dir, const string& jvm_target = "21") {
        require(fs::is_directory(root_path), "root_path must be an existing directory: {}", root_path);
        const auto abs_root = fs::weakly_canonical(root_path);
        const auto abs_cfg = fs::weakly_canonical(config_dir);
        const auto rel = "./" + abs_root.lexically_relative(abs_cfg).string();
        return {
            .version = 1,
            .workspace_file = string{default_workspace_file},
            .jvm_target = jvm_target,
            .build = BuildConfig{.command = {string{default_gradle_command}}},
            .roots = {{.path = rel}},
        };
    }

    const fs::path& config_file() const { return config_file_; }
    const fs::path& config_dir() const { return config_dir_; }

    strings describe(bool verbose = true) const {
        strings out;
        out.push_back(format("Config: {}", config_file_.string()));
        out.push_back(format("  {} root(s), jvm_target={}, workspace_file={}",
            roots().size(),
            jvm_target(),
            data_.workspace_file.empty() ? "(not set)" : data_.workspace_file));
        if (!verbose) {
            return out;
        }
        if (data_.build) {
            out.push_back(format("  build: {} {}", join(data_.build->command), join(data_.build->gradle_args)));
        }
        for (const auto& root : data_.roots) {
            auto line = format("  root: {}", root_path(root).string());
            if (root.build) {
                line += format(" (build: {})", join(root.build->command));
            }
            out.push_back(std::move(line));
        }
        return out;
    }

    // --- Forwarded accessors ---
    int version() const { return data_.version; }
    const string& jvm_target() const { return data_.jvm_target; }
    const optional<BuildConfig>& build() const { return data_.build; }
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
    /// Uses per-root build if present, otherwise falls back to the global build.
    /// Throws if neither is configured.
    const BuildConfig& build_for(const RootEntry& root) const {
        if (root.build) {
            return *root.build;
        }
        require(data_.build.has_value(), "no build command configured for root: {}", root.path);
        return *data_.build;
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
        return data_.build.has_value() ||
               r::any_of(data_.roots, [](const auto& entry) { return entry.build.has_value(); });
    }

    ConfigData data_;
    fs::path config_file_;
    fs::path config_dir_;
};

} // namespace klspw
