# Contributing to klspw

## Developer Certificate of Origin

By contributing to this project, you agree to the
[Developer Certificate of Origin (DCO)](https://developercertificate.org/).
You certify that you wrote the contribution or otherwise have the right to
submit it under the project's MIT license.

Sign off your commits by adding a `Signed-off-by` trailer:

```bash
git commit -s -m "Add feature X"
```

This adds a line like `Signed-off-by: Your Name <your@email.com>` to the commit
message. All commits in a PR must be signed off.

## Test policy

Major new functionality must include tests. Test files mirror source files 1:1
(e.g., `config.hpp` has `config_test.cpp`). Bug fixes should include a
regression test when feasible. PRs without tests for new behavior will be asked
to add them before merging.

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
just check       # build + test (configure must be run first)
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
  common.hpp            # type aliases, namespace imports, glaze opts, require(), projections, compose()
  strings.hpp           # string utilities: trim, split_words, strip_prefixes, escape_json, extract_between, MavenCoords, join_to_string
  files.hpp             # file I/O: read_file, write_file, write_binary_file, find_dir, find_file, file_stem
  ranges.hpp            # range adaptors: to_vector, to_set, unique_by, not_in, concat_to_vector, find_map, find_opt
  describe.hpp          # d_info/d_debug/d_trace logging + Describable concept
  validate.hpp          # ValidateContext for collecting validation errors
  config.hpp            # config model + ConfigDir + YAML loading via glaze
  gradle_runner.hpp     # GradleRunner: init script lifecycle
  gradle.hpp            # Gradle model types + parser + workspace conversion
  workspace.hpp         # kotlin-lsp workspace.json schema types
  module_matcher.hpp    # ModuleMatcher: library-to-module name resolution
  pipeline.hpp          # Pipeline: orchestrates generate/inspect commands
  process_runner.hpp    # ProcessRunner: subprocess execution via reproc++
  sources.hpp           # JarPath: jar classification, Maven coordinates, source discovery
src/
  main.cpp              # CLI entry point (CLI11)
  strings.cpp           # extract_between, escape_json implementations
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
  init.gradle.kts       # Gradle init script (embedded at configure time)
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

GitHub Actions workflows automate building, testing, security scanning, and releasing. The `master` branch is protected: all CI checks must pass before merging, force push and deletion are disabled.

`ci.yml` runs on every push to `master` and on pull requests. It builds and tests on macOS 26 (arm64 and Intel, both using Xcode 26.4) and Ubuntu 24.04 (GCC 15). The workflow uses the `release` CMake preset with vcpkg for dependency management. Cached vcpkg binaries speed up repeat runs. After building, it installs into a staging directory and runs a smoke test (`--version` + `init` on a fixture project).

`release.yml` triggers when a `v*` tag is pushed. It builds release binaries for macOS arm64, macOS x86_64, and Linux x86_64, packages them as `.tar.gz` archives, generates [artifact attestations](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations) for supply-chain integrity, and uploads them to a GitHub Release with auto-generated notes.

`codeql.yml` runs CodeQL static analysis on pushes to `master`, pull requests, and weekly. It scans both C++ source code (manual build with GCC 15 on Ubuntu) and GitHub Actions workflow files. Skips runs triggered by changes to markdown files, `.kiro/` steering, and `scripts/`.

`dependency-review.yml` runs on pull requests and flags newly introduced vulnerable dependencies or license violations.

`scorecard.yml` runs the [OpenSSF Scorecard](https://scorecard.dev) analysis weekly and on pushes to `master`, publishing results to the code scanning dashboard.

`fuzzing.yml` runs [ClusterFuzzLite](https://google.github.io/clusterfuzzlite/) on pull requests, only when source, fuzz, or build files change. Fuzzes the YAML config parser and Gradle output parser with AddressSanitizer for 5 minutes per run.

`update-tap.yml` triggers when a `v*` tag is pushed (or manually via `workflow_dispatch`). It computes the source tarball sha256, checks out the [homebrew-tap](https://github.com/Unril/homebrew-tap) repo, updates the formula URL and sha256 (stripping any stale bottle block), verifies the formula syntax, and opens a PR against the tap.

Homebrew bottles (prebuilt binaries) are built by the [homebrew-tap](https://github.com/Unril/homebrew-tap) repo's own CI workflows using `brew test-bot`. Label the tap PR `pr-pull` to trigger bottle builds and publishing.

`coverage.yml` runs on pushes to `master` and on manual dispatch. It builds with clang source-based coverage instrumentation, runs tests, merges profiles, exports lcov, and uploads to [Codecov](https://codecov.io).

## Updating GitHub Actions SHAs

All workflow files pin actions to full commit SHAs for supply-chain security. Use the resolver script to audit and update them:

```bash
# Full report: show all actions, their pinned SHAs, and available tags
./scripts/resolve-action-shas.sh

# Check a single action
./scripts/resolve-action-shas.sh actions/cache

# Show only the latest 3 tags per action
./scripts/resolve-action-shas.sh -n 3

# Update all workflow files in-place to the latest SHA for each pinned tag
./scripts/resolve-action-shas.sh --update
```

Requires `uv` (Python package runner). The script also checks vcpkg and Docker base image pinning.

## Publishing a release

The `master` branch has protection rules: required CI status checks, no force push, no deletion. All changes go through PRs. Feature branches are automatically deleted after PR merge.

1. Update the version in `CMakeLists.txt` (`project(klspw VERSION x.y.z ...)`) and `vcpkg.json`.
2. Update `CHANGELOG.md` with the new version and changes.
3. Push to a feature branch and open a PR:

   ```bash
   git checkout -b release/v0.2.0
   git push -u origin release/v0.2.0
   gh pr create --base master --head release/v0.2.0
   ```

4. Wait for CI to pass, then merge the PR.
5. Tag the merge commit on master with a signed tag and push it:

   ```bash
   git checkout master && git pull
   git tag -s v0.2.0 -m "v0.2.0"
   git push origin v0.2.0
   ```

   Signed tags require a GPG or SSH signing key configured in git. See
   [GitHub's guide on signing tags](https://docs.github.com/en/authentication/managing-commit-signature-verification/signing-tags).

6. The `release.yml` workflow builds all three platforms, runs tests, packages `.tar.gz` archives, attests them, and creates a GitHub Release.
7. The `update-tap.yml` workflow automatically opens a PR against the [homebrew-tap](https://github.com/Unril/homebrew-tap) with the updated formula. Label the tap PR `pr-pull` to trigger bottle builds and publishing.

Users can verify the provenance of downloaded release binaries:

```bash
gh attestation verify ./klspw-darwin-arm64.tar.gz -R Unril/klspw
```

Users upgrade with `brew upgrade klspw`.
