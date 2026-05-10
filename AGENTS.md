# klspw

klspw generates `workspace.json` files for [kotlin-lsp](https://github.com/Kotlin/kotlin-lsp) from Gradle builds.

It targets repositories where the default kotlin-lsp project import fails -- when the build runs through a wrapper around Gradle, or when dependencies come from custom package/cache layouts.

## What it does

1. Reads a YAML config describing Gradle roots and build commands
2. Runs each root's Gradle build with an injected init script that dumps project metadata as JSON (between `KLSPW_BEGIN`/`KLSPW_END` delimiters)
3. Parses source sets, classpaths, and project structure from the Gradle output
4. Converts to the kotlin-lsp workspace model (modules, libraries, kotlin settings)
5. Uses Maven coordinates as library names (from Gradle component IDs and cache paths) to avoid naming collisions in KMP projects
6. For Android projects, picks one build variant (first non-test, typically `debug`) and its test variant to avoid class redeclaration errors from variant-specific source directories
7. Merges results across roots, deduplicates libraries by name
8. Promotes library dependencies to module dependencies when a library matches a workspace module
9. Attaches source jars from Gradle-resolved mappings, filesystem discovery, and coordinate-based cache search
10. Injects kotlin-native-stubs.jar for KMP projects (provides JVM stubs for `kotlin.native.*` annotations)
11. Includes Kotlin compiler plugin classpaths (serialization, compose) in compiler arguments
12. For composite builds (`includeBuild` with `dependencySubstitution`), detects cross-root project dependencies via `ProjectComponentIdentifier` in resolved artifacts
13. Discovers databinding generated source directories (`data_binding_base_class_source_out`)
14. Writes deterministic, pretty-printed `workspace.json`

## CLI subcommands

- `klspw init {roots...}` -- generate a starter config YAML; each arg is `"path [build_command...]"`
- `klspw generate` -- run Gradle, write `workspace.json` (uses `./klspw.yaml` by default)
- `klspw inspect` -- run Gradle, log discovered modules/libraries
- `klspw validate` -- check config paths and build commands

Flags:

- `-c, --config` overrides the config path (file or directory)
- `-l, --log-level` sets logging verbosity (`trace`, `debug`, `info`, `warn`, `error`, `off`; default `info`)
- `-V, --version` prints the version
- `--save-gradle-output {path}` on `generate` and `inspect` writes raw Gradle stdout to a file
- `init` accepts `--jvm-target {version}` (default `21`), `-b, --build {cmd}` for a global build command, and `-d, --discover` to find Gradle roots recursively under the given directories

## Config file

The config file (default name: `klspw.yaml`) defines:

- Global and per-root build commands (e.g., `./gradlew`)
- Gradle root project paths (resolved relative to config file directory)
- Output workspace file path
- JVM target version
- Options: `include_tests`, `attach_sources`, `remove_missing_paths` (all default to `true`)

## Tech stack

### Language

- C++23 (`cxx_std_23`, `-std=c++23`)
- Compiler flags: `-Wall -Wextra -Wpedantic`
- Clang-tidy enabled via `.clangd` with broad rule set

### Build system

- CMake 3.25+ with CMake Presets (`CMakePresets.json`)
- Ninja generator
- vcpkg manifest mode for dependency management (`vcpkg.json`)
- `just` task runner wrapping CMake commands (`justfile`)
- Presets: `dev` (Debug), `release` (Release), `sanitize` (Debug + ASan/UBSan), `coverage` (Debug + clang coverage), `fuzz` (Release + libFuzzer/ASan, requires Clang)
- ccache enabled automatically when available
- GCC 15 required on Linux (ubuntu-toolchain-r/test PPA) for full C++23 library support

### Dependencies

All managed via vcpkg:

- [CLI11](https://github.com/CLIUtils/CLI11) -- command-line parsing
- [glaze](https://github.com/stephenberry/glaze) -- JSON and YAML serialization (compile-time reflection, no macros)
- [reproc++](https://github.com/DaanDeMeyer/reproc) -- subprocess execution
- [spdlog](https://github.com/gabime/spdlog) -- logging
- [doctest](https://github.com/doctest/doctest) -- testing framework

### Code formatting

- `.clang-format` based on LLVM style for C++ (120 columns, 4-space indent)
- `.gersemirc` for CMake formatting via [gersemi](https://github.com/BlankSpruce/gersemi) (120 columns)
- [prettier](https://prettier.io/) for YAML and JSON files
- [shfmt](https://github.com/mvdan/sh) for shell scripts (2-space indent)
- Pointer/reference alignment: left (`int* p`, `int& r`)
- Include sorting: system headers first, then library headers, then project headers
- `just format` runs all formatters

### Common commands

```bash
# Prerequisites
brew install cmake ninja just
export VCPKG_ROOT="$HOME/vcpkg"

# Build and test (dev preset)
just check          # build + test (configure must be run first)
just configure      # cmake --preset dev
just build          # cmake --build --preset dev
just test           # ctest --preset dev

# Run the tool
just run -c config.yaml generate

# Clean rebuild
just rebuild        # clean + configure + build

# Release build
just release        # configure + build + test with release preset

# Sanitizer build (ASan + UBSan, separate build dir)
just sanitize

# Fuzzing (requires Clang with libFuzzer)
just fuzz                              # build + run fuzz_config_yaml for 60s
just fuzz fuzz_gradle_output 120       # run specific target for 120s

# Coverage
just coverage       # build + run + merge + report + html
```

### Testing

- Framework: doctest (header-only, `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`)
- Test binaries: one per test file (named `*_test.cpp`), auto-discovered via `file(GLOB)` in CMakeLists.txt
- Fixtures in `test/fixtures/` (YAML configs, JSON outputs)
- RAII test helpers for temp files/dirs in a shared test header
- Tests run from the source directory (working directory set in CMake)
- Integration tests require Gradle on PATH and network access; gated behind `ENABLE_INTEGRATION_TESTS` CMake option

```bash
# Integration tests
just integration    # configure with flag + build + run integration label
```

### Fuzzing

- Local: libFuzzer + ASan via `just fuzz` (requires Clang, `ENABLE_FUZZING` CMake option)
- CI: ClusterFuzzLite on PRs via `.github/workflows/fuzzing.yml`
- Fuzz targets in `fuzz/` directory, one per entry point
- Corpus stored in `fuzz/corpus/` (gitignored, built up over runs)
- ClusterFuzzLite build integration in `.clusterfuzzlite/` (Dockerfile, build.sh, project.yaml)

### Serialization conventions

- glaze handles both JSON and YAML via compile-time reflection
- C++ fields use `snake_case`; JSON keys use `camelCase` via `glz::camel_case` meta
- Tagged variant dispatch for `DependencyData` using `"type"` discriminator field
- Variant alternative structs keep `camelCase` C++ field names (glaze limitation with tagged variants)
- Write opts: pretty-print with 2-space indent, write `null` for missing optionals (kotlin-lsp requires them)
- Read opts: ignore unknown keys for forward compatibility

## Project structure

### Layout

```text
klspw/
  include/           # Public headers (header-only logic + declarations)
    common.hpp       # Type aliases, namespace imports, glaze opts, require(), projections (to_path, to_string, has_value, deref), compose()
    config.hpp       # Config, ConfigDir, ConfigData, StarterConfig: config model + YAML loading via glaze
    describe.hpp     # d_info/d_debug/d_trace logging + Describable concept
    files.hpp        # File I/O: read_file, write_file, write_binary_file, find_dir, find_file, file_stem
    gradle.hpp       # GradleBuildOutput, GradleProject, SourceSet: Gradle model + parser + workspace conversion
    gradle_runner.hpp # GradleRunner: init script lifecycle, GradleBuildFn type alias
    module_matcher.hpp # ModuleMatcher: library-to-module name resolution, maven_module_component
    pipeline.hpp     # Pipeline: orchestrates generate/inspect commands
    process_runner.hpp # ProcessRunner: subprocess execution via reproc++
    ranges.hpp       # Range adaptors: to_vector, to_set, unique_by, not_in, concat_to_vector, find_map, find_opt, OptionalMapper
    sources.hpp      # JarPath: jar classification, Maven coordinate extraction, source discovery
    strings.hpp      # String utilities: trim, split_words, strip_prefixes, escape_json, extract_between, MavenCoords, join_to_string
    validate.hpp     # ValidateContext for collecting validation errors
    workspace.hpp    # WorkspaceData, ModuleData, LibraryData, KotlinSettingsData: workspace.json schema
  src/
    main.cpp         # CLI entry point (CLI11 subcommands)
    strings.cpp      # extract_between, escape_json implementations
    files.cpp        # File I/O and filesystem search implementations
  test/              # One test binary per *_test.cpp file, shared RAII fixtures in test_common.hpp
    fixtures/        # Test data (YAML configs, JSON outputs)
      projects/      # Real Gradle projects for integration tests (simple, with-deps, with-serialization, multi, multi-root, kmp)
  fuzz/              # libFuzzer fuzz targets (one per entry point)
  scripts/           # Utility scripts (resolve-action-shas.sh + its Python implementation)
  resources/
    init.gradle.kts  # Gradle init script (embedded at configure time via configure_file)
    *.hpp.in         # CMake configure_file templates (init script, native stubs, version)
    stubs/           # Precompiled JVM stubs for KMP-only annotations (kotlin.native.*)
  .clusterfuzzlite/  # ClusterFuzzLite CI build integration (Dockerfile, build.sh)
```

### Architecture

The codebase is mostly header-only. File I/O (`files.cpp`) and string utilities (`strings.cpp`) have separate `.cpp` files; everything else lives in headers.

#### Data flow

```text
Config (YAML) -> Pipeline -> GradleRunner (subprocess) -> raw stdout
  -> GradleBuildOutput::from_raw_output (delimiter extraction + JSON parse)
  -> GradleBuildOutput::to_workspace (-> WorkspaceData)
  -> merge across roots + promote_module_deps + inject native stubs (KMP)
  -> write workspace.json
```

#### Key types

- `Config` / `ConfigData` / `ConfigDir` (`config.hpp`) -- loaded config with path resolution. `ConfigData` is the plain glaze-deserializable struct; `Config` wraps it with the config file path; `ConfigDir` encapsulates directory-relative path resolution and relativization
- `StarterConfig` (`config.hpp`) -- generates starter ConfigData for the `init` subcommand; builder pattern with `with_*` methods that no-op on empty input
- `ValidateContext` (`validate.hpp`) -- collects validation errors without throwing; `schema_only` flag controls check depth
- `GradleRunner` (`gradle_runner.hpp`) -- RAII manager for the temp init script file. Writes on construction, removes on destruction. Callable as `GradleBuildFn`
- `GradleBuildOutput` / `GradleProject` / `SourceSet` (`gradle.hpp`) -- mirror the JSON emitted by the init script. Each type owns its conversion to workspace model types (Tell Don't Ask). `GradleBuildOutput` provides `active_projects()` lazy view
- `WorkspaceData` / `ModuleData` / `LibraryData` / `KotlinSettingsData` (`workspace.hpp`) -- match the kotlin-lsp `workspace.json` schema. `WorkspaceData` provides `add_module`, `add_lib`, `add_libs`, `add_kotlin_settings` methods. `ModuleData` provides `add_dep`, `add_deps`
- `ModuleMatcher` (`module_matcher.hpp`) -- resolves library names to workspace module names using longest-match-first; handles exact, versioned-suffix, AGP jetified prefixes, and Maven coordinate module components
- `MavenCoords` (`strings.hpp`) -- parsed Maven coordinates with `MavenCoords::parse()` factory
- `Pipeline` (`pipeline.hpp`) -- owns a `Config` and a `GradleBuildFn`, orchestrates the full generate/inspect flow; injects kotlin-native-stubs.jar for KMP projects
- `ProcessRunner` (`process_runner.hpp`) -- subprocess execution via reproc++
- `JarPath` (`sources.hpp`) -- classifies jar paths by origin (Gradle module cache, AGP transform, package cache) using `optional<GradleCacheInfo>` and `optional<PkgCacheInfo>` detection structs; extracts Maven coordinates via regex; discovers source jars
- `describe.hpp` -- `d_info`/`d_debug`/`d_trace` free functions for leveled logging with lazy arg evaluation; `Describable` concept; `d_describe` for ranges/optionals. Model types implement `void describe() const` (no context parameter)

### Conventions

- Namespace: `klspw`
- Preconditions via `require(condition, format_string, args...)` -- throws `runtime_error` on failure
- `require` args support lazy evaluation (zero-arg callables), `path`, and `std::error_code` auto-conversion
- Shared type aliases: `strings`, `opt_string`, `string_set` (defined in `common.hpp`)
- Shared projections: `to_path`, `to_string`, `has_value`, `deref`, `compose` (defined in `common.hpp`)
- Range adaptors: `to_vector()`, `to_set()`, `unique_by(proj)`, `not_in(set)`, `concat_to_vector(ranges...)`, `find_map(range, fn)`, `find_opt(range, pred)` (defined in `ranges.hpp`)
- String adaptors: `join_to_string(sep)` pipe adaptor (defined in `strings.hpp`)
- Use `optional` for absence instead of empty strings/paths as logic switches
- `write_file` / `write_binary_file` auto-create parent directories
- `GradleBuildFn = std::function<string(const BuildConfig&, const path&)>` for dependency injection in tests
- `master` branch is protected: changes go through PRs, CI must pass, no force push; tag releases after merge; feature branches are automatically deleted after PR merge
