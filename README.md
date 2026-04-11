# klspw

Generate `workspace.json` for [kotlin-lsp] from Gradle builds.

Targets repositories where the default kotlin-lsp project import does not work -- when the build runs through a wrapper around Gradle, or when dependencies come from custom package or cache layouts.

## Quick start

```bash
# Homebrew (macOS)
brew install Unril/tap/klspw

# From source
brew install cmake ninja just
export VCPKG_ROOT="$HOME/vcpkg"
just check                        # configure + build + test
just install                      # release build + install to /usr/local
```

## Usage

```bash
# Generate a starter config for a Gradle root
klspw init ./my-project
klspw -c . init ./my-project              # write to ./klspw.yaml
klspw -c . init "./proj gradlew"          # custom build command
klspw -c . init ./proj_1 ./proj_2 -b cmd  # multiple roots, global build

# Run Gradle and write workspace.json (uses ./klspw.yaml by default)
klspw generate
klspw -c config.yaml generate           # explicit config path

# Inspect discovered modules and libraries without writing
klspw inspect

# Validate config paths and build commands
klspw validate

# Save raw Gradle output for debugging
klspw -c config.yaml generate --save-gradle-output output.txt

# Verbose logging
klspw --log-level debug generate
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
```

- `build` sets the default Gradle command for all roots
- Each root can override `build` with its own `command` and `gradle_args`
- Paths resolve relative to the config file directory
- `include_tests` controls whether test source sets appear in the workspace
- `attach_sources` discovers and attaches source jars to libraries (Gradle-resolved source jars and package cache layouts)
- `remove_missing_paths` warns and removes source roots and classpath jars that don't exist on disk (default: true)

## How it works

1. Read config, validate paths
2. For each root, run the configured Gradle command with a temporary init script
3. The init script dumps project metadata as JSON between `KLSPW_BEGIN`/`KLSPW_END` delimiters, including Gradle-resolved source jar mappings
4. Parse source sets, classpaths, and project structure from the JSON output
5. Attach source jars to libraries (from Gradle resolution, then filesystem discovery as fallback)
6. Convert to kotlin-lsp workspace model (modules, libraries, kotlin settings)
7. Merge results across roots, deduplicating libraries by name
8. Promote library dependencies to module dependencies when a library matches a workspace module (sibling Gradle root)
9. Write deterministic, pretty-printed `workspace.json`

## Project structure

```text
include/
  common.hpp            # type aliases, namespace imports, glaze opts, require()
  strings.hpp           # string utilities: trim, join, split_words, strip_prefixes, extract_between
  files.hpp             # file I/O: read_file, write_file, find_dir, find_file, file_stem
  ranges.hpp            # range adaptors: to_vector, unique_by, not_in
  describe.hpp          # d_info/d_debug/d_trace logging + Describable concept
  validate.hpp          # ValidateContext for collecting validation errors
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
  test_common.hpp       # shared RAII test fixtures (TempDir, TempConfig, TempFile)
  smoke.cpp             # basic smoke tests
  config_test.cpp       # config loading tests
  config_validate_test.cpp  # config validation tests
  config_save_test.cpp  # config YAML round-trip and StarterConfig tests
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
just release     # configure + build + test with release preset
just sanitize    # ASan + UBSan build and test
just install     # release build + install to /usr/local
just install /opt/klspw  # install to custom prefix

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

## CI

GitHub Actions runs on every push to `master` and on pull requests, building and testing on macOS arm64 and Intel (both macOS 26 with Xcode 26.4). Pushing a `v*` tag also creates a GitHub Release automatically.

## Publishing a release

1. Update the version in `CMakeLists.txt` (`project(klspw VERSION x.y.z ...)`) and `vcpkg.json`.
2. Commit, tag, and push:
   ```bash
   git tag v0.2.0
   git push origin v0.2.0
   ```
3. CI builds and tests both architectures. The release workflow creates a GitHub Release with auto-generated notes.
4. Get the tarball sha256:
   ```bash
   curl -sL https://github.com/Unril/klspw/archive/refs/tags/v0.2.0.tar.gz | shasum -a 256
   ```
5. In the [homebrew-tap](https://github.com/Unril/homebrew-tap) repo, update `Formula/klspw.rb` with the new `url`, `sha256`, and version. Commit and push.

Users upgrade with `brew upgrade klspw`.

## License

[MIT](LICENSE)

[kotlin-lsp]: https://github.com/Kotlin/kotlin-lsp
