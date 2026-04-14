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

  void describe() const { d_debug("  build: {} {}", join(command), join(gradle_args)); }
};

/// A project root to process. Optionally overrides the global build config.
struct RootEntry {
  string path;  ///< Relative or absolute path to the Gradle root project directory.
  optional<BuildConfig> build;  ///< Per-root build override. Absent = inherit global.

  bool has_build() const { return build.has_value(); }

  void validate(ValidateContext& ctx) const {
    ctx.check(!path.empty(), "Config missing required field: path");
    ctx.validate(build);
  }

  void describe() const {
    if (has_build()) {
      d_debug("  root: {} (build: {})", path, join(build->command));
    } else {
      d_debug("  root: {}", path);
    }
  }
};

/// Plain YAML-deserializable config data. Never mutated after deserialization.
struct ConfigData {
  int version = 0;  ///< Config schema version (must be 1).
  string workspace_file;  ///< Output workspace.json path (relative to config dir). Optional.
  string jvm_target = "21";  ///< JVM target version for kotlin-lsp compilerArguments.
  optional<BuildConfig> build;  ///< Global build command and Gradle args. Optional.
  vector<RootEntry> roots;  ///< Gradle root projects to process (at least one required).
  GenerationOptions options;  ///< Behavioral flags for workspace generation.

  void describe() const {
    d_info("  {} root(s), jvm_target={}, workspace_file={}", roots.size(), jvm_target,
           has_workspace_file() ? workspace_file : "(not set)");
    d_describe(build);
    d_describe(roots);
  }

  bool has_workspace_file() const { return !workspace_file.empty(); }

  void validate(ValidateContext& ctx) const {
    ctx.check(version != 0, "Config missing required field: version");
    if (version != 0) {
      ctx.check(version == 1, format("Unsupported config version: {}", version));
    }
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
    for (const auto& dir : search_dirs) {
      const auto roots = find_dirs_with_markers(path{dir}, gradle_markers);
      for (const auto& root : roots) {
        d_info("Discovered Gradle root: {}", root.string());
        config.data_.roots.push_back(RootEntry{.path = config.relative_path(root)});
      }
    }
    require(!config.data_.roots.empty(), "no Gradle projects found under: {}", join(search_dirs, ", "));
    config.ensure_default_build();
    return config;
  }

  StarterConfig& set_jvm_target(string value) {
    data_.jvm_target = std::move(value);
    return *this;
  }

  /// Override the global build command. Words are split on spaces.
  StarterConfig& set_build(string_view command) {
    data_.build = BuildConfig{.command = split_words(trim(command))};
    return *this;
  }

  const path& config_path() const { return config_path_; }

  /// Set config output path. Root paths are rebased relative to the new config directory.
  StarterConfig& set_config_path(const path& path) {
    config_path_ = path;
    config_dir_ = fs::weakly_canonical(resolve_config_path(path)).parent_path();
    rebase_roots();
    return *this;
  }

  const ConfigData& to_data() const { return data_; }

  string to_yaml() const { return data_.to_yaml(); }

  /// Save to a file. config_path can be a directory (appends klspw.yaml) or a file path.
  void save_yaml_file() const {
    require(!config_path_.empty(), "no config path specified (use to_yaml() for stdout)");
    const auto resolved = resolve_config_path(config_path_);
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

    RootEntry entry{.path = relative_path(fs::weakly_canonical(entry_path))};

    if (words.size() > 1) {
      entry.build = BuildConfig{.command = {words.begin() + 1, words.end()}};
    }

    data_.roots.push_back(std::move(entry));
  }

  string relative_path(const path& absolute) const { return "./" + absolute.lexically_relative(config_dir_).string(); }

  /// Add default ./gradlew global build when not all roots have their own.
  void ensure_default_build() {
    if (!data_.has_build()) {
      const auto all_have_build = r::all_of(data_.roots, &RootEntry::has_build);
      if (!all_have_build) {
        data_.build = BuildConfig{.command = {string(default_gradle_command)}};
      }
    }
  }

  /// Recompute root paths relative to the current config_dir.
  void rebase_roots() {
    for (auto& root : data_.roots) {
      const auto abs = (old_config_dir_ / root.path).lexically_normal();
      root.path = relative_path(abs);
    }
    old_config_dir_ = config_dir_;
  }

  ConfigData data_{.version = 1, .workspace_file = string(default_workspace_file)};
  path config_path_;
  path config_dir_ = fs::weakly_canonical(resolve_config_path({})).parent_path();
  path old_config_dir_ = config_dir_;

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

  const path& config_dir() const { return config_dir_; }

  /// Workspace output path resolved against config dir. Empty if not configured.
  path workspace_file() const {
    if (data_.has_workspace_file()) {
      return resolve(data_.workspace_file);
    }
    return {};
  }

  /// Root path resolved against config dir.
  path root_path(const RootEntry& root) const { return resolve(root.path); }

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

  void describe() const {
    d_info("Config: {}", config_file_);
    data_.describe();
  }

 private:
  explicit Config(ConfigData data, const path& config_path) : data_{std::move(data)} {
    const auto canonical = fs::weakly_canonical(config_path);
    config_file_ = canonical;
    config_dir_ = canonical.parent_path();
  }

  path resolve(const string& relative) const { return (config_dir_ / relative).lexically_normal(); }

  ConfigData data_;
  path config_file_;
  path config_dir_;
};

}  // namespace klspw
