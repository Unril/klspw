# Contributing to klspw

## Build

Prerequisites:

```bash
brew install cmake ninja just
export VCPKG_ROOT="$HOME/vcpkg"
```

Commands:

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
  module_matcher.hpp    # ModuleMatcher: library-to-module name resolution
  pipeline.hpp          # Pipeline: orchestrates generate/inspect commands
  process_runner.hpp    # ProcessRunner: subprocess execution via reproc++
  sources.hpp           # JarPath: jar classification, Maven coordinates, source discovery
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
  workspace_test.cpp       # workspace model serialization and promote_module_deps tests
  module_matcher_test.cpp  # ModuleMatcher name resolution tests
  common_test.cpp       # utility function tests
  describe_test.cpp     # logging and describe tests
  files_test.cpp        # file I/O and filesystem search tests
  ranges_test.cpp       # range adaptor tests
  runners_test.cpp      # subprocess execution tests
  strings_test.cpp      # string utility tests
  sources_test.cpp      # JarPath classification and source discovery tests
  integration_test.cpp  # end-to-end tests with real Gradle projects
resources/
  init.gradle.kts       # Gradle init script (embedded at build time)
  stubs/                # Precompiled JVM stubs for KMP-only annotations
fuzz/
  fuzz_config_yaml.cpp  # fuzz target for YAML config parsing
  fuzz_gradle_output.cpp # fuzz target for Gradle output parsing
scripts/
  resolve-action-shas.sh # resolve GitHub Actions tags to commit SHAs
```

## References

- [kotlin-lsp](https://github.com/Kotlin/kotlin-lsp)
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

Homebrew bottles (prebuilt binaries) are built separately by the [homebrew-tap](https://github.com/Unril/homebrew-tap) repo's own CI workflows using `brew test-bot`.

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

5. In the [homebrew-tap](https://github.com/Unril/homebrew-tap) repo, update `Formula/klspw.rb` with the new `url`, `sha256`, and version. Open a PR, let the tap CI build bottles, then label the PR `pr-pull` to publish them.

Users can verify the provenance of downloaded release binaries:

```bash
gh attestation verify ./klspw-darwin-arm64.tar.gz -R Unril/klspw
```

Users upgrade with `brew upgrade klspw`.
