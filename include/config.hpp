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
#include "ranges.hpp"
#include "strings.hpp"
#include "validate.hpp"

namespace klspw {

/// Gradle task name registered by the init script.
inline constexpr string_view gradle_task = "dumpKotlinLspModel";

/// Behavioral flags controlling workspace generation.
struct GenerationOptions {
  bool include_tests = true;  ///< Include test source sets in the workspace.
  bool attach_sources = true;  ///< Discover and attach source jars (-sources.jar) to libraries.
  bool remove_missing_paths = true;  ///< Warn and remove paths that don't exist on disk.
};

/// Gradle build command and extra arguments.
///
/// The process runs in the root project directory. The resulting command line is:
///   {command...} --init-script {path} {gradle_args...} dumpKotlinLspModel
struct BuildConfig {
  strings command;  ///< Build tool executable and fixed args (e.g., ["./gradlew"]).
  strings gradle_args;  ///< Extra Gradle flags (e.g., ["--quiet", "--no-daemon"]).

  /// Build the full command-line args for a Gradle invocation.
  /// The resulting command line is:
  ///   {command...} --no-configuration-cache --init-script {path} {gradle_args...}
  ///   dumpKotlinLspModel
  strings args_for(const path& init_script) const {
    strings args;
    args.append_range(command);
    args.emplace_back("--no-configuration-cache");
    args.append_range(std::array{"--init-script"s, init_script.string()});
    args.append_range(gradle_args);
    args.emplace_back(gradle_task);
    return args;
  }

  bool has_commands() const { return !command.empty(); }

  bool has_args() const { return !gradle_args.empty(); }

  void validate(ValidateContext& ctx) const {
    ctx.check(has_commands(), "BuildConfig missing required field: command");
  }

  void describe() const { d_debug("  build: {} {}", command | join_to_string(), gradle_args | join_to_string()); }
};

/// A project root to process. Optionally overrides the global build config.
struct RootEntry {
  string path;  ///< Relative or absolute path to the Gradle root project directory.
  optional<BuildConfig> build;  ///< Per-root build override. Absent = inherit global.

  /// Create a root entry from a pre-computed relative path string (no build override).
  static RootEntry from_path(string rel_path) { return {.path = std::move(rel_path)}; }

  bool has_build() const { return build.has_value(); }

  void validate(ValidateContext& ctx) const {
    ctx.check(!path.empty(), "Config missing required field: path");
    ctx.validate(build);
  }

  void describe() const {
    if (has_build()) {
      d_debug("  root: {} (build: {})", path, build->command | join_to_string());
    } else {
      d_debug("  root: {}", path);
    }
  }
};

/// Plain YAML-deserializable config data. Never mutated after deserialization.
struct ConfigData {
  int version = 0;  ///< Config schema version (must be 1).
  opt_string workspace_file;  ///< Output workspace.json path (relative to config dir). Optional.
  string jvm_target = "21";  ///< JVM target version for kotlin-lsp compilerArguments.
  optional<BuildConfig> build;  ///< Global build command and Gradle args. Optional.
  vector<RootEntry> roots;  ///< Gradle root projects to process (at least one required).
  GenerationOptions options;  ///< Behavioral flags for workspace generation.

  void describe() const {
    d_info("  {} root(s), jvm_target={}, workspace_file={}", roots.size(), jvm_target,
           workspace_file.value_or("(not set)"));
    d_describe(build);
    d_describe(roots);
  }

  bool has_workspace_file() const { return workspace_file.has_value(); }

  bool has_roots() const { return !roots.empty(); }

  void validate(ValidateContext& ctx) const {
    ctx.check(version != 0, "Config missing required field: version");
    if (version != 0) {
      ctx.check(version == 1, format("Unsupported config version: {}", version));
    }
    ctx.check(has_roots(), "Config missing required field: roots");
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
    if (root.has_build()) {
      return *root.build;
    }
    require(has_build(), "no build command configured for root: {}", root.path);
    return *build;
  }

  bool has_build() const { return build.has_value(); }

  bool has_any_build_command() const { return has_build() || r::any_of(roots, &RootEntry::has_build); }

  /// Format compilerArguments for kotlin-lsp KotlinSettingsData.
  /// J prefix is a kotlin-lsp convention: J-prefixed JSON string for compiler args.
  /// Note: superseded by GradleProject::build_compiler_arguments() which also includes
  /// plugin classpaths. Kept for config-level tests and backward compatibility.
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
inline path resolve_config_path(const path& path) {
  if (path.empty()) {
    return fs::current_path() / default_config_filename;
  }
  return fs::is_directory(path) ? path / default_config_filename : path;
}

/// The directory containing the config file. Provides path resolution and relativization.
class ConfigDir {
 public:
  /// From a config file path argument: resolve to concrete file, take parent directory.
  static ConfigDir from_config_path(const path& config_path) {
    return ConfigDir{fs::weakly_canonical(resolve_config_path(config_path)).parent_path()};
  }

  /// Resolve a relative path against this config directory.
  path resolve(const string& relative) const { return (dir_ / relative).lexically_normal(); }

  /// Make an absolute path relative to this config directory, prefixed with "./".
  string relativize(const path& absolute) const { return "./" + absolute.lexically_relative(dir_).string(); }

  /// Create a RootEntry with a path relative to this directory.
  RootEntry make_root(const path& absolute) const { return {.path = relativize(absolute)}; }

  /// Rebase a RootEntry from this directory to another.
  RootEntry rebase_root(const RootEntry& root, const ConfigDir& new_dir) const {
    return {.path = new_dir.relativize(resolve(root.path)), .build = root.build};
  }

  const path& dir() const { return dir_; }

 private:
  explicit ConfigDir(path dir) : dir_{std::move(dir)} {}

  path dir_;
};

/// Generates a starter ConfigData for the `init` subcommand.
///
/// Each root arg is "path [build_command...]" -- first word is the directory,
/// remaining words become the per-root build command.
/// Roots without a build command inherit the global build (default: ./gradlew).
class StarterConfig {
 public:
  static constexpr auto default_workspace_file = "./workspace.json"sv;
  static constexpr auto default_gradle_command = "./gradlew"sv;

  /// Construct from CLI root arguments. Each arg is "path [build_command...]".
  /// First word is the root path, remaining words are the per-root build command.
  explicit StarterConfig(const strings& root_args) {
    require(!root_args.empty(), "at least one root argument is required");
    for (const auto& arg : root_args) {
      add_root(arg);
    }
    ensure_default_build();
  }

  /// Discover Gradle root projects under the given search directories.
  /// Each directory is searched recursively for settings.gradle[.kts] files.
  static StarterConfig discover(const strings& search_dirs) {
    require(!search_dirs.empty(), "at least one search directory is required");

    static constexpr std::array gradle_markers = {"settings.gradle"sv, "settings.gradle.kts"sv, "build.gradle"sv,
                                                  "build.gradle.kts"sv};

    StarterConfig config;
    auto to_roots = [&](const path& dir) { return find_dirs_with_markers(dir, gradle_markers); };
    auto to_entry = [&](const path& root) { return config.config_dir_.make_root(root); };

    config.data_.roots =
        search_dirs | v::transform(compose(to_path, to_roots)) | v::join | v::transform(to_entry) | to_vector();

    if (config.data_.has_roots()) {
      d_info("Discovered {} Gradle root(s): {}", config.data_.roots.size(),
             config.data_.roots | v::transform(&RootEntry::path) | join_to_string(", "));
    }
    require(config.data_.has_roots(), "no Gradle projects found under: {}",
            [&] { return search_dirs | join_to_string(", "); });
    config.ensure_default_build();
    return config;
  }

  StarterConfig& with_jvm_target(string value) {
    if (!value.empty()) {
      data_.jvm_target = std::move(value);
    }
    return *this;
  }

  /// Override the global build command. Words are split on spaces. No-op if empty.
  StarterConfig& with_build(string_view command) {
    if (const auto trimmed = trim(command); !trimmed.empty()) {
      data_.build = BuildConfig{.command = split_words(trimmed)};
    }
    return *this;
  }

  const opt_path& config_path() const { return config_path_; }

  /// Set config output path. Root paths are rebased relative to the new config directory. No-op if empty.
  StarterConfig& with_config_path(const path& path) {
    if (path.empty()) {
      return *this;
    }
    config_path_ = path;
    const auto old_dir = config_dir_;
    config_dir_ = ConfigDir::from_config_path(path);
    rebase_roots(old_dir);
    return *this;
  }

  const ConfigData& to_data() const { return data_; }

  string to_yaml() const { return data_.to_yaml(); }

  /// Save to a file. config_path can be a directory (appends klspw.yaml) or a file path.
  void save_yaml_file() const {
    require(config_path_.has_value(), "no config path specified (use to_yaml() for stdout)");
    const auto resolved = resolve_config_path(*config_path_);
    write_file(resolved, to_yaml());
    d_info("Wrote config to {}", resolved.string());
  }

 private:
  /// Parse a root arg and append to data_.roots.
  /// First word = directory path (must exist), remaining words = per-root build command.
  void add_root(string_view arg) {
    auto words = split_words(arg);
    require(!words.empty(), "root argument must not be empty");

    const auto entry_path = path{words[0]};
    require(fs::is_directory(entry_path), "root path must be an existing directory: {}", entry_path);

    auto entry = config_dir_.make_root(fs::weakly_canonical(entry_path));

    if (words.size() > 1) {
      entry.build = BuildConfig{.command = words | v::drop(1) | r::to<strings>()};
    }

    data_.roots.push_back(std::move(entry));
  }

  /// Add default ./gradlew global build when not all roots have their own.
  void ensure_default_build() {
    if (!data_.has_build() && !r::all_of(data_.roots, &RootEntry::has_build)) {
      data_.build = BuildConfig{.command = {string(default_gradle_command)}};
    }
  }

  /// Recompute root paths relative to the current config_dir.
  void rebase_roots(const ConfigDir& old_dir) {
    auto rebase = [&](const RootEntry& root) { return old_dir.rebase_root(root, config_dir_); };
    data_.roots = data_.roots | v::transform(rebase) | to_vector();
  }

  ConfigData data_{.version = 1, .workspace_file = string(default_workspace_file)};
  opt_path config_path_;
  ConfigDir config_dir_ = ConfigDir::from_config_path({});

  /// Default constructor for factory methods.
  StarterConfig() = default;
};

/// Loaded and validated config. Resolves relative paths against config_dir on demand.
class Config {
 public:
  /// Load config from a YAML file. Validates after parsing.
  static Config load_yaml_file(const path& config_path) {
    const auto resolved = resolve_config_path(config_path);
    require(fs::exists(resolved), "Config file not found: {}", resolved);
    auto data = ConfigData::from_yaml(read_file(resolved));
    auto cfg = Config{std::move(data), resolved};
    cfg.describe();
    return cfg;
  }

  const ConfigData& data() const { return data_; }

  const path& config_file() const { return config_file_; }

  const path& config_dir() const { return config_dir_.dir(); }

  /// Workspace output path resolved against config dir. Empty if not configured.
  opt_path workspace_file() const {
    return data_.workspace_file.transform([&](const string& ws) { return config_dir_.resolve(ws); });
  }

  /// Root path resolved against config dir.
  path root_path(const RootEntry& root) const { return config_dir_.resolve(root.path); }

  /// Validate config data, resolved paths, and build commands.
  void validate(ValidateContext& ctx) const {
    data_.validate(ctx);

    if (const auto ws_file = workspace_file()) {
      const auto parent = ws_file->parent_path();
      ctx.check(parent.empty() || fs::is_directory(parent),
                format("workspace_file parent does not exist: {}", parent.string()));
    }

    for (const auto& root : data_.roots) {
      const auto resolved = root_path(root);
      ctx.check(fs::is_directory(resolved), format("root path does not exist: {}", resolved.string()));
    }
  }

  void describe() const {
    d_info("Config: {}", config_file_);
    data_.describe();
  }

 private:
  explicit Config(ConfigData data, const path& config_path)
      : data_{std::move(data)},
        config_file_{fs::weakly_canonical(config_path)},
        config_dir_{ConfigDir::from_config_path(config_path)} {}

  ConfigData data_;
  path config_file_;
  ConfigDir config_dir_;
};

}  // namespace klspw
