# Project structure

## Layout

```text
klspw/
  include/           # Public headers (header-only logic + declarations)
    common.hpp       # Type aliases, utilities, preconditions, glaze opts
  src/
    main.cpp         # CLI entry point (CLI11 subcommands)
    common.cpp       # File I/O and string utilities
  test/              # One test binary per file, shared RAII fixtures in a common header
    fixtures/        # Test data (YAML configs, JSON outputs)
      projects/      # Real Gradle projects for integration tests (simple, with-deps, multi, multi-root)
  resources/
    init.gradle.kts  # Gradle init script (embedded at build time via configure_file)
```

## Architecture

The codebase is mostly header-only. Only file I/O and string utilities have a separate `.cpp` file; everything else lives in headers.

### Data flow

```text
Config (YAML) -> Pipeline -> GradleRunner (subprocess) -> raw stdout
  -> extract_gradle_json (delimiter extraction)
  -> parse_gradle_output (JSON -> GradleBuildOutput)
  -> GradleBuildOutput.to_workspace (-> WorkspaceData)
  -> merge across roots
  -> write workspace.json
```

### Key types

- `Config` / `ConfigData` -- loaded config with path resolution. `ConfigData` is the plain glaze-deserializable struct; `Config` wraps it with the config file path and resolves relative paths
- `GradleRunner` -- RAII manager for the temp init script file. Writes on construction, removes on destruction. Callable as `GradleBuildFn`
- `GradleBuildOutput` / `GradleProject` / `SourceSet` -- mirror the JSON emitted by the init script. Each type owns its conversion to workspace model types (Tell Don't Ask)
- `WorkspaceData` / `ModuleData` / `LibraryData` / `KotlinSettingsData` -- match the kotlin-lsp `workspace.json` schema
- `Pipeline` -- owns a `Config` and a `GradleBuildFn`, orchestrates the full generate/inspect flow

### Conventions

- Namespace: `klspw`
- Preconditions via `require(condition, format_string, args...)` -- throws `runtime_error` on failure
- `require` args support lazy evaluation (zero-arg callables), `fs::path`, and `std::error_code` auto-conversion
- Shared type aliases: `strings`, `opt_string`, `string_set` (defined in the common header)
- Range adaptors: `to_vector()`, `unique_by(proj)`, `not_in(set)`
- `GradleBuildFn = std::function<string(const BuildConfig&, const fs::path&)>` for dependency injection in tests
