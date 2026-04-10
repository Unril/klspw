# klspw

Generate `workspace.json` for [kotlin-lsp] from Gradle builds.

Targets repositories where the default kotlin-lsp project import does not work -- when the build runs through a wrapper around Gradle, or when dependencies come from custom package or cache layouts.

## Quick start

```bash
brew install cmake ninja just
export VCPKG_ROOT="$HOME/vcpkg"  # or wherever your vcpkg checkout lives
just check                        # configure + build + test
```

## Usage

```bash
# Generate a starter config for a Gradle root
klspw init ./my-project
klspw init ./my-project -c config.yaml  # write to file instead of stdout

# Run Gradle and write workspace.json
klspw -c config.yaml generate

# Inspect discovered modules and libraries without writing
klspw -c config.yaml inspect

# Validate config paths and build commands
klspw -c config.yaml validate

# Save raw Gradle output for debugging
klspw -c config.yaml generate --save-gradle-output output.txt

# Verbose logging
klspw --log-level debug -c config.yaml generate
```

## Configuration

Config file (default name: `klspw.yaml`):

```yaml
version: 1
workspace_file: ./workspace.json
jvm_target: "21"

build:
  command: ["./gradlew"]
  gradle_args: ["--quiet"]

roots:
  - path: ./src/my-service
  - path: ./src/other-service
    build:
      command: ["gradle"]
      gradle_args: ["--no-daemon"]

options:
  include_tests: true
  attach_sources: true
  follow_symlinks: true
```

- `build` sets the default Gradle command for all roots
- Each root can override `build` with its own `command` and `gradle_args`
- Paths resolve relative to the config file directory
- `include_tests` controls whether test source sets appear in the workspace
- `attach_sources` discovers and attaches source jars to libraries (Gradle-resolved source jars and package cache layouts)

## How it works

1. Read config, validate paths
2. For each root, run the configured Gradle command with a temporary init script
3. The init script dumps project metadata as JSON between `KLSPW_BEGIN`/`KLSPW_END` delimiters, including Gradle-resolved source jar mappings
4. Parse source sets, classpaths, and project structure from the JSON output
5. Attach source jars to libraries (from Gradle resolution, then filesystem discovery as fallback)
6. Convert to kotlin-lsp workspace model (modules, libraries, kotlin settings)
7. Merge results across roots, deduplicating libraries by name
8. Write deterministic, pretty-printed `workspace.json`

## Project structure

```text
include/
  common.hpp            # type aliases, namespace imports, glaze opts, require()
  strings.hpp           # string utilities: trim, join, strip_prefixes, extract_between
  files.hpp             # file I/O: read_file, write_file, find_dir, find_file, file_stem
  ranges.hpp            # range adaptors: to_vector, unique_by, not_in
  describe.hpp          # DescribeContext for human-readable model descriptions
  config.hpp            # config model + YAML loading via glaze
  gradle_runner.hpp     # GradleRunner: init script lifecycle
  gradle.hpp            # Gradle model types + parser + workspace conversion
  workspace.hpp         # kotlin-lsp workspace.json schema types
  pipeline.hpp          # Pipeline: orchestrates generate/inspect commands
  process_runner.hpp    # ProcessRunner: subprocess execution via reproc++
  sources.hpp           # source jar/directory discovery for library attachment
src/
  main.cpp              # CLI entry point (CLI11)
  strings.cpp           # extract_between implementation
  files.cpp             # file I/O and filesystem search implementations
test/
  test_common.hpp       # shared RAII test fixtures (TempDir, TempConfig)
  smoke.cpp             # basic smoke tests
  config_test.cpp       # config loading tests
  config_validate_test.cpp  # config validation tests
  config_save_test.cpp  # config YAML round-trip and make_starter tests
  gradle_test.cpp       # Gradle parsing + workspace conversion
  workspace_json_test.cpp  # workspace model round-trip serialization
  common_test.cpp       # utility function tests
  process_test.cpp      # subprocess execution tests
  sources_test.cpp      # source discovery tests
  integration_test.cpp  # end-to-end tests with real Gradle projects
resources/
  init.gradle.kts       # Gradle init script (embedded at build time)
```

## Build

```bash
just configure   # cmake --preset dev
just build       # cmake --build --preset dev
just test        # ctest --preset dev
just check       # all three

# Integration tests (requires Gradle on PATH)
just integration
```

## Dependencies

- [CLI11](https://github.com/CLIUtils/CLI11) -- command-line parsing
- [glaze](https://github.com/stephenberry/glaze) -- JSON and YAML serialization
- [reproc++](https://github.com/DaanDeMeyer/reproc) -- subprocess execution
- [spdlog](https://github.com/gabime/spdlog) -- logging
- [doctest](https://github.com/doctest/doctest) -- testing

All managed via vcpkg manifest mode.

## References

- [kotlin-lsp]
- [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
- [vcpkg manifest mode](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode)

## License

TBD

[kotlin-lsp]: https://github.com/Kotlin/kotlin-lsp
