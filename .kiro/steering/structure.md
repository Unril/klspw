# Project structure

## Layout

```text
klspw/
  include/           # Public headers (header-only logic + declarations)
    common.hpp       # Type aliases, namespace imports, glaze opts, require()
    config.hpp       # Config, ConfigData, StarterConfig: config model + YAML loading via glaze
    describe.hpp     # d_info/d_debug/d_trace logging + Describable concept
    files.hpp        # File I/O: read_file, write_file, find_dir, find_file, file_stem
    gradle.hpp       # GradleBuildOutput, GradleProject, SourceSet: Gradle model + parser + workspace conversion
    gradle_runner.hpp # GradleRunner: init script lifecycle, GradleBuildFn type alias
    module_matcher.hpp # ModuleMatcher: library-to-module name resolution
    pipeline.hpp     # Pipeline: orchestrates generate/inspect commands
    process_runner.hpp # ProcessRunner: subprocess execution via reproc++
    ranges.hpp       # Range adaptors: to_vector, unique_by, not_in
    sources.hpp      # JarPath: jar classification, Maven coordinate extraction, source discovery
    strings.hpp      # String utilities: trim, join, split_words, strip_prefixes, extract_between
    validate.hpp     # ValidateContext for collecting validation errors
    workspace.hpp    # WorkspaceData, ModuleData, LibraryData, KotlinSettingsData: workspace.json schema
  src/
    main.cpp         # CLI entry point (CLI11 subcommands)
    strings.cpp      # extract_between implementation
    files.cpp        # File I/O and filesystem search implementations
  test/              # One test binary per file, shared RAII fixtures in a common header
    fixtures/        # Test data (YAML configs, JSON outputs)
      projects/      # Real Gradle projects for integration tests (simple, with-deps, with-serialization, multi, multi-root, kmp)
  fuzz/              # libFuzzer fuzz targets (one per entry point)
  scripts/           # Utility scripts (resolve-action-shas.sh)
  resources/
    init.gradle.kts  # Gradle init script (embedded at build time via configure_file)
    *.hpp.in         # CMake configure_file templates (init script, native stubs, version)
    stubs/           # Precompiled JVM stubs for KMP-only annotations (kotlin.native.*)
  .clusterfuzzlite/  # ClusterFuzzLite CI build integration (Dockerfile, build.sh)
```

## Architecture

The codebase is mostly header-only. Only file I/O (`files.cpp`) and `extract_between` (`strings.cpp`) have separate `.cpp` files; everything else lives in headers.

### Data flow

```text
Config (YAML) -> Pipeline -> GradleRunner (subprocess) -> raw stdout
  -> GradleBuildOutput::from_raw_output (delimiter extraction + JSON parse)
  -> GradleBuildOutput::to_workspace (-> WorkspaceData)
  -> merge across roots + promote_module_deps + inject native stubs (KMP)
  -> write workspace.json
```

### Key types

- `Config` / `ConfigData` (`config.hpp`) -- loaded config with path resolution. `ConfigData` is the plain glaze-deserializable struct; `Config` wraps it with the config file path and resolves relative paths
- `StarterConfig` (`config.hpp`) -- generates starter ConfigData for the `init` subcommand; accepts multiple root args with optional per-root build commands
- `ValidateContext` (`validate.hpp`) -- collects validation errors without throwing; `schema_only` flag controls check depth
- `GradleRunner` (`gradle_runner.hpp`) -- RAII manager for the temp init script file. Writes on construction, removes on destruction. Callable as `GradleBuildFn`
- `GradleBuildOutput` / `GradleProject` / `SourceSet` (`gradle.hpp`) -- mirror the JSON emitted by the init script. Each type owns its conversion to workspace model types (Tell Don't Ask)
- `WorkspaceData` / `ModuleData` / `LibraryData` / `KotlinSettingsData` (`workspace.hpp`) -- match the kotlin-lsp `workspace.json` schema
- `ModuleMatcher` (`module_matcher.hpp`) -- resolves library names to workspace module names using longest-match-first; handles exact, versioned-suffix, AGP jetified prefixes, and Maven coordinate module components
- `Pipeline` (`pipeline.hpp`) -- owns a `Config` and a `GradleBuildFn`, orchestrates the full generate/inspect flow; injects kotlin-native-stubs.jar for KMP projects
- `ProcessRunner` (`process_runner.hpp`) -- subprocess execution via reproc++
- `JarPath` (`sources.hpp`) -- classifies jar paths by origin (Gradle module cache, AGP transform, package cache); extracts Maven coordinates via regex; discovers source jars
- `describe.hpp` -- `d_info`/`d_debug`/`d_trace` free functions for leveled logging with lazy arg evaluation; `Describable` concept; `d_describe` for ranges/optionals. Model types implement `void describe() const` (no context parameter)

### Conventions

- Namespace: `klspw`
- Preconditions via `require(condition, format_string, args...)` -- throws `runtime_error` on failure
- `require` args support lazy evaluation (zero-arg callables), `path`, and `std::error_code` auto-conversion
- Shared type aliases: `strings`, `opt_string`, `string_set` (defined in `common.hpp`)
- Range adaptors: `to_vector()`, `unique_by(proj)`, `not_in(set)` (defined in `ranges.hpp`)
- `GradleBuildFn = std::function<string(const BuildConfig&, const path&)>` for dependency injection in tests
