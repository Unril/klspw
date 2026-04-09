# klspw

Generate `workspace.json` for [kotlin-lsp] from Gradle builds.

Targets repositories where the default kotlin-lsp project import does not work -- when the build runs through a wrapper around Gradle, or when dependencies come from custom package/cache layouts.

## Quick start

```bash
brew install cmake ninja just
export VCPKG_ROOT="$HOME/vcpkg"  # or wherever your vcpkg checkout lives
just check                        # configure + build + test
```

## Usage

```bash
klspw -c config.yaml generate   # run Gradle, write workspace.json
klspw -c config.yaml inspect    # run Gradle, log discovered modules/libraries
klspw -c config.yaml validate   # check config paths and build commands
klspw --log-level debug -c config.yaml generate  # verbose output
```

## Configuration

Config file: `workspace-kotlin-lsp-config.yaml`

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
    command: ["gradle"]
    gradle_args: ["--no-daemon"]

options:
  include_tests: true
  attach_sources: true
  follow_symlinks: true
```

- `build` sets the default Gradle command for all roots
- Each root can override `command` and `gradle_args`
- Paths are resolved relative to the config file directory
- `options.include_tests` controls whether test source sets appear in the workspace

## How it works

1. Read config, validate paths
2. For each root, run the configured Gradle command with a temporary init script
3. Extract JSON between `KLSPW_BEGIN`/`KLSPW_END` delimiters in Gradle stdout
4. Parse source sets, classpaths, and project metadata
5. Convert to kotlin-lsp workspace model (modules, libraries, kotlin settings)
6. Merge results across roots, deduplicating libraries by name
7. Write deterministic, pretty-printed `workspace.json`

## Project structure

```text
include/
  common.hpp            # type aliases, utilities, glaze opts
  config.hpp            # config model + YAML loading via glaze
  gradle.hpp            # GradleRunner: init script lifecycle
  gradle_output.hpp     # Gradle model types + parser + workspace conversion
  workspace_model.hpp   # kotlin-lsp workspace.json schema types
  pipeline.hpp          # Pipeline: orchestrates generate/inspect commands
  process.hpp           # ProcessRunner: subprocess execution via reproc++
src/
  main.cpp              # CLI entry point (CLI11)
  common.cpp            # file I/O and string utilities
test/
  test_common.hpp       # shared RAII test fixtures
  smoke.cpp             # basic smoke tests
  config_test.cpp       # config loading and validation
  gradle_test.cpp       # Gradle parsing + workspace conversion
  workspace_json_test.cpp  # workspace model round-trip serialization
  common_test.cpp       # utility function tests
  process_test.cpp      # subprocess execution tests
  pipeline_test.cpp     # config validation tests
resources/
  init.gradle.kts       # Gradle init script (embedded at build time)
```

## Build

```bash
just configure   # cmake --preset dev
just build       # cmake --build --preset dev
just test        # ctest --preset dev
just check       # all three
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
