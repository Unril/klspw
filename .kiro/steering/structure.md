# Project structure

## Layout

```text
klspw/
  include/           # Public headers (header-only logic + declarations)
    common.hpp       # Type aliases, namespace imports, glaze opts, require()
    strings.hpp      # String utilities: trim, join, split_words, strip_prefixes, extract_between
    files.hpp        # File I/O: read_file, write_file, find_dir, find_file, file_stem
    ranges.hpp       # Range adaptors: to_vector, unique_by, not_in
    describe.hpp     # d_info/d_debug/d_trace logging + Describable concept
    validate.hpp     # ValidateContext for collecting validation errors
  src/
    main.cpp         # CLI entry point (CLI11 subcommands)
    strings.cpp      # extract_between implementation
    files.cpp        # File I/O and filesystem search implementations
  test/              # One test binary per file, shared RAII fixtures in a common header
    fixtures/        # Test data (YAML configs, JSON outputs)
      projects/      # Real Gradle projects for integration tests (simple, with-deps, multi, multi-root)
  fuzz/              # libFuzzer fuzz targets (one per entry point)
  scripts/           # Utility scripts (resolve-action-shas.sh)
  resources/
    init.gradle.kts  # Gradle init script (embedded at build time via configure_file)
  .clusterfuzzlite/  # ClusterFuzzLite CI build integration (Dockerfile, build.sh)
```

## Architecture

The codebase is mostly header-only. Only file I/O (`files.cpp`) and `extract_between` (`strings.cpp`) have separate `.cpp` files; everything else lives in headers.

### Data flow

```text
Config (YAML) -> Pipeline -> GradleRunner (subprocess) -> raw stdout
  -> GradleBuildOutput::from_raw_output (delimiter extraction + JSON parse)
  -> GradleBuildOutput::to_workspace (-> WorkspaceData)
  -> merge across roots + promote_module_deps
  -> write workspace.json
```

### Key types

- `Config` / `ConfigData` (`config.hpp`) -- loaded config with path resolution. `ConfigData` is the plain glaze-deserializable struct; `Config` wraps it with the config file path and resolves relative paths
- `StarterConfig` (`config.hpp`) -- generates starter ConfigData for the `init` subcommand; accepts multiple root args with optional per-root build commands
- `ValidateContext` (`validate.hpp`) -- collects validation errors without throwing; `schema_only` flag controls check depth
- `GradleRunner` (`gradle_runner.hpp`) -- RAII manager for the temp init script file. Writes on construction, removes on destruction. Callable as `GradleBuildFn`
- `GradleBuildOutput` / `GradleProject` / `SourceSet` (`gradle.hpp`) -- mirror the JSON emitted by the init script. Each type owns its conversion to workspace model types (Tell Don't Ask)
- `WorkspaceData` / `ModuleData` / `LibraryData` / `KotlinSettingsData` (`workspace.hpp`) -- match the kotlin-lsp `workspace.json` schema
- `Pipeline` (`pipeline.hpp`) -- owns a `Config` and a `GradleBuildFn`, orchestrates the full generate/inspect flow
- `ProcessRunner` (`process_runner.hpp`) -- subprocess execution via reproc++
- `SourceResolver` (`sources.hpp`) -- source jar/directory discovery for library attachment
- `DescribeContext` (`describe.hpp`) -- empty struct passed to describe() methods; `d_info`/`d_debug`/`d_trace` free functions for leveled logging with lazy arg evaluation; `Describable` concept; `d_describe` for ranges/optionals

### Conventions

- Namespace: `klspw`
- Preconditions via `require(condition, format_string, args...)` -- throws `runtime_error` on failure
- `require` args support lazy evaluation (zero-arg callables), `path`, and `std::error_code` auto-conversion
- Shared type aliases: `strings`, `opt_string`, `string_set` (defined in `common.hpp`)
- Range adaptors: `to_vector()`, `unique_by(proj)`, `not_in(set)` (defined in `ranges.hpp`)
- `GradleBuildFn = std::function<string(const BuildConfig&, const path&)>` for dependency injection in tests
