# klspw

[![CI](https://github.com/Unril/klspw/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/Unril/klspw/actions/workflows/ci.yml)
[![CodeQL](https://github.com/Unril/klspw/actions/workflows/codeql.yml/badge.svg?branch=master)](https://github.com/Unril/klspw/actions/workflows/codeql.yml)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/Unril/klspw/badge)](https://scorecard.dev/viewer/?uri=github.com/Unril/klspw)
[![License](https://img.shields.io/github/license/Unril/klspw)](./LICENSE)
[![Release](https://img.shields.io/github/v/release/Unril/klspw?sort=semver)](https://github.com/Unril/klspw/releases)

[![macOS](https://img.shields.io/badge/macOS-Apple%20Silicon%20%7C%20Intel-black)](https://github.com/Unril/klspw)
[![Linux](https://img.shields.io/badge/Linux-x86__64-black)](https://github.com/Unril/klspw)

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
  remove_missing_paths: true
```

- `build` sets the default Gradle command for all roots
- Each root can override `build` with its own `command` and `gradle_args`
- Paths resolve relative to the config file directory
- `include_tests` controls whether test source sets appear in the workspace (default: true)
- `attach_sources` discovers and attaches source jars to libraries via Gradle-resolved mappings and package cache layouts (default: true)
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

## VS Code setup with kotlin-lsp

[kotlin-lsp] provides Kotlin language support for VS Code (completion, diagnostics, navigation, refactoring). It normally imports Gradle projects automatically, but that fails when the build runs through a wrapper or dependencies come from non-standard locations. klspw bridges this gap by generating a `workspace.json` that kotlin-lsp can import directly.

Prerequisites: Java 17+ on PATH.

1. Install kotlin-lsp (the language server):

   ```bash
   brew install JetBrains/utils/kotlin-lsp
   ```

2. Install the VS Code extension: download the latest `.vsix` from the [kotlin-lsp releases page](https://github.com/Kotlin/kotlin-lsp/releases), then install it via Extensions > `...` > Install from VSIX.

3. Create a klspw config in your Kotlin project root:

   ```bash
   klspw -c . init ./my-project
   ```

   Edit `klspw.yaml` if needed (build command, extra roots, options).

4. Generate the workspace:

   ```bash
   klspw generate
   ```

   This writes `workspace.json` next to `klspw.yaml`.

5. Open the project folder in VS Code. kotlin-lsp detects `workspace.json` and uses it for project import instead of running Gradle itself.

6. If you change dependencies or project structure, re-run `klspw generate` and restart the language server with the `Kotlin LSP: Restart` command from the command palette.

To verify the import worked, check the kotlin-lsp output panel in VS Code for messages about loaded modules and libraries.

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
fuzz/
  fuzz_config_yaml.cpp  # fuzz target for YAML config parsing
  fuzz_gradle_output.cpp # fuzz target for Gradle output parsing
scripts/
  resolve-action-shas.sh # resolve GitHub Actions tags to commit SHAs
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

# Fuzzing (requires Clang with libFuzzer)
just fuzz                              # build + run fuzz_config_yaml for 60s
just fuzz fuzz_gradle_output 120       # specific target, 120s

# Format all source files
just format

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

GitHub Actions workflows automate building, testing, security scanning, and releasing:

`ci.yml` runs on every push to `master` and on pull requests. It builds and tests on macOS 26 (arm64 and Intel, both using Xcode 26.4) and Ubuntu 24.04 (GCC 15). The workflow uses the `release` CMake preset with vcpkg for dependency management. Cached vcpkg binaries speed up repeat runs. After building, it installs into a staging directory and runs a smoke test (`--version` + `init` on a fixture project).

`release.yml` triggers when a `v*` tag is pushed. It builds release binaries for macOS arm64, macOS x86_64, and Linux x86_64, packages them as `.tar.gz` archives, generates [artifact attestations](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations) for supply-chain integrity, and uploads them to a GitHub Release with auto-generated notes.

`codeql.yml` runs CodeQL static analysis on pushes to `master`, pull requests, and weekly. It scans both C++ source code (manual build with GCC 15 on Ubuntu) and GitHub Actions workflow files.

`dependency-review.yml` runs on pull requests and flags newly introduced vulnerable dependencies or license violations.

`scorecard.yml` runs the [OpenSSF Scorecard](https://scorecard.dev) analysis weekly and on pushes to `master`, publishing results to the code scanning dashboard.

`fuzzing.yml` runs [ClusterFuzzLite](https://google.github.io/clusterfuzzlite/) on pull requests, fuzzing the YAML config parser and Gradle output parser with AddressSanitizer for 5 minutes per run.

Homebrew bottles (prebuilt binaries) are built separately by the [homebrew-tap] repo's own CI workflows using `brew test-bot`.

## Publishing a release

1. Update the version in `CMakeLists.txt` (`project(klspw VERSION x.y.z ...)`) and `vcpkg.json`.
2. Commit, tag, and push:

   ```bash
   git tag v0.2.0
   git push origin v0.2.0
   ```

3. The release workflow builds all three platforms, runs tests, packages `.tar.gz` archives, attests them, and creates a GitHub Release with the archives attached.
4. Get the source tarball sha256 for Homebrew:

   ```bash
   curl -sL https://github.com/Unril/klspw/archive/refs/tags/v0.2.0.tar.gz | shasum -a 256
   ```

5. In the [homebrew-tap] repo, update `Formula/klspw.rb` with the new `url`, `sha256`, and version. Open a PR, let the tap CI build bottles, then label the PR `pr-pull` to publish them.

Users can verify the provenance of downloaded release binaries:

```bash
gh attestation verify ./klspw-darwin-arm64.tar.gz -R Unril/klspw
```

Users upgrade with `brew upgrade klspw`.

## License

[MIT](LICENSE)

[kotlin-lsp]: https://github.com/Kotlin/kotlin-lsp
[homebrew-tap]: https://github.com/Unril/homebrew-tap
