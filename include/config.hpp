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
#include "describe.hpp"
#include "files.hpp"
#include "strings.hpp"
#include "validate.hpp"

namespace klspw {

/// Gradle task name registered by the init script.
inline constexpr string_view gradle_task = "dumpKotlinLspModel";

/// Behavioral flags controlling workspace generation.
struct GenerationOptions {
    bool include_tests = true; ///< Include test source sets in the workspace.
    bool attach_sources = true; ///< Discover and attach source jars (-sources.jar) to libraries.
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

    bool has_commands() const { return !command.empty(); }

    bool has_args() const { return !gradle_args.empty(); }

    void validate(ValidateContext& ctx) const {
        ctx.check(has_commands(), "BuildConfig missing required field: command");
    }

    void describe(DescribeContext& ctx) const { ctx.add(format("  build: {} {}", join(command), join(gradle_args))); }
};

/// A project root to process. Optionally overrides the global build config.
struct RootEntry {
    string path; ///< Relative or absolute path to the Gradle root project directory.
    optional<BuildConfig> build; ///< Per-root build override. Absent = inherit global.

    void validate(ValidateContext& ctx) const {
        ctx.check(!path.empty(), "Config missing required field: path");
        ctx.validate(build);
    }

    void describe(DescribeContext& ctx) const {
        auto line = format("  root: {}", path);
        if (build) {
            line += format(" (build: {})", join(build->command));
        }
        ctx.add(std::move(line));
    }
};

/// Plain YAML-deserializable config data. Never mutated after deserialization.
struct ConfigData {
    int version = 0; ///< Config schema version (must be 1).
    string workspace_file; ///< Output workspace.json path (relative to config dir). Optional.
    string jvm_target = "21"; ///< JVM target version for kotlin-lsp compilerArguments.
    optional<BuildConfig> build; ///< Global build command and Gradle args. Optional.
    vector<RootEntry> roots; ///< Gradle root projects to process (at least one required).
    GenerationOptions options; ///< Behavioral flags for workspace generation.

    bool has_workspace_file() const { return !workspace_file.empty(); }

    void validate(ValidateContext& ctx) const {
        ctx.check(version != 0, "Config missing required field: version");
        ctx.check(version == 1 || version == 0, format("Unsupported config version: {}", version));
        ctx.check(!roots.empty(), "Config missing required field: roots");
        ctx.validate(build);
        ctx.validate(roots);
        if (!ctx.schema_only()) {
            ctx.check(has_any_build_command(), "no build command configured (global or per-root)");
        }
    }

    /// Resolve the effective build config for a root.
    /// Uses per-root build if present, otherwise falls back to the global build.
    /// Throws if neither is configured.
    const BuildConfig& build_for(const RootEntry& root) const {
        if (root.build) {
            return *root.build;
        }
        require(build.has_value(), "no build command configured for root: {}", root.path);
        return *build;
    }

    bool has_any_build_command() const {
        return build.has_value() || r::any_of(roots, [](const auto& entry) { return entry.build.has_value(); });
    }

    void describe(DescribeContext& ctx) const {
        ctx.add(format("  {} root(s), jvm_target={}, workspace_file={}",
            roots.size(),
            jvm_target,
            has_workspace_file() ? workspace_file : "(not set)"));
        if (ctx.verbose()) {
            ctx.describe(build);
            ctx.describe(roots);
        }
    }

    /// Format compilerArguments for kotlin-lsp KotlinSettingsData.
    /// J prefix is a kotlin-lsp convention: J-prefixed JSON string for compiler args.
    string compiler_arguments_json() const { return format(R"(J{{"jvmTarget":"{}"}})", jvm_target); }

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
        ValidateContext::require_valid(data, true);
        return data;
    }
};

/// Default config filename used when a directory is given instead of a file path.
inline constexpr auto default_config_filename = "klspw.yaml"sv;

/// Resolve a -c/--config argument to a concrete file path.
/// Empty -> cwd/klspw.yaml. Directory -> dir/klspw.yaml. File -> as-is.
inline fs::path resolve_config_path(const fs::path& path) {
    if (path.empty()) {
        return fs::current_path() / default_config_filename;
    }
    return fs::is_directory(path) ? path / default_config_filename : path;
}

/// Generates a starter ConfigData for the `init` subcommand.
///
/// Constructed from a Gradle root path and an optional config file path.
/// config_path can be empty (stdout), a directory, or a file path.
/// The config directory is derived from config_path for relative root resolution.
class StarterConfig {
  public:
    static constexpr auto default_workspace_file = "./workspace.json"s;
    static constexpr auto default_gradle_command = "./gradlew"s;

    explicit StarterConfig(const fs::path& root_path) : root_path_{fs::weakly_canonical(root_path)} {
        require(fs::is_directory(root_path), "root_path must be an existing directory: {}", root_path);
    }

    const string& jvm_target() const { return jvm_target_; }

    StarterConfig& set_jvm_target(string value) {
        jvm_target_ = std::move(value);
        return *this;
    }

    const fs::path& config_path() const { return config_path_; }

    StarterConfig& set_config_path(const fs::path& path) {
        config_path_ = path;
        config_dir_ = fs::weakly_canonical(resolve_config_path(path)).parent_path();
        return *this;
    }

    /// Build a ConfigData with paths relative to config_dir.
    ConfigData to_config_data() const {
        const auto rel = "./" + root_path_.lexically_relative(config_dir_).string();
        return {
            .version = 1,
            .workspace_file = default_workspace_file,
            .jvm_target = jvm_target_,
            .build = BuildConfig{.command = {default_gradle_command}},
            .roots = {{.path = rel}},
        };
    }

    /// Serialize directly to YAML.
    string to_yaml() const { return to_config_data().to_yaml(); }

    /// Save to a file. If config_path is a directory, appends "klspw.yaml".
    void save_yaml_file() const {
        require(!config_path_.empty(), "no config path specified (use to_yaml() for stdout)");
        const auto resolved = resolve_config_path(config_path_);
        write_file(resolved, to_yaml());
        spdlog::info("Wrote config to {}", resolved.string());
    }

  private:
    fs::path root_path_;
    fs::path config_path_;
    fs::path config_dir_ = fs::weakly_canonical(resolve_config_path({})).parent_path();
    string jvm_target_ = "21";
};

/// Loaded and validated config. Resolves relative paths against config_dir on demand.
class Config {
  public:
    /// Load config from a YAML file. Validates after parsing.
    static Config load_yaml_file(const fs::path& config_path) {
        const auto resolved = resolve_config_path(config_path);
        require(fs::exists(resolved), "Config file not found: {}", resolved);
        spdlog::info("Loading config: {}", resolved.string());
        auto data = ConfigData::from_yaml(read_file(resolved));
        auto cfg = Config{std::move(data), resolved};
        DescribeContext ctx;
        cfg.describe(ctx);
        ctx.log(spdlog::level::info);
        return cfg;
    }

    const ConfigData& data() const { return data_; }

    const fs::path& config_file() const { return config_file_; }

    const fs::path& config_dir() const { return config_dir_; }

    /// Workspace output path resolved against config dir. Empty if not configured.
    fs::path workspace_file() const {
        if (data_.has_workspace_file()) {
            return resolve(data_.workspace_file);
        }
        return {};
    }

    /// Root path resolved against config dir.
    fs::path root_path(const RootEntry& root) const { return resolve(root.path); }

    string to_yaml() const { return data_.to_yaml(); }

    void save_yaml_file(const fs::path& config_path) const { write_file(config_path, data_.to_yaml()); }

    void describe(DescribeContext& ctx) const {
        ctx.add(format("Config: {}", config_file_.string()));
        data_.describe(ctx);
    }

    /// Validate config data, resolved paths, and build commands.
    void validate(ValidateContext& ctx) const {
        data_.validate(ctx);

        if (const auto ws_file = workspace_file(); !ws_file.empty()) {
            const auto parent = ws_file.parent_path();
            ctx.check(parent.empty() || fs::is_directory(parent),
                format("workspace_file parent does not exist: {}", parent.string()));
        }

        for (const auto& root : data_.roots) {
            const auto resolved = root_path(root);
            ctx.check(fs::is_directory(resolved), format("root path does not exist: {}", resolved.string()));
        }
    }

  private:
    explicit Config(ConfigData data, const fs::path& config_path) : data_{std::move(data)} {
        const auto canonical = fs::weakly_canonical(config_path);
        config_file_ = canonical;
        config_dir_ = canonical.parent_path();
    }

    fs::path resolve(const string& relative) const { return (config_dir_ / relative).lexically_normal(); }

    ConfigData data_;
    fs::path config_file_;
    fs::path config_dir_;
};

} // namespace klspw
