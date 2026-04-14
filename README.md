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

## Installation

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

### Quick start for any Gradle project

```bash
cd my-project
klspw -c . init -d ./              # discover Gradle roots, write klspw.yaml
klspw generate                     # run Gradle, write workspace.json
```

Open the folder in VS Code or Kiro with the [kotlin-lsp extension](https://github.com/Kotlin/kotlin-lsp/releases) installed. kotlin-lsp detects `workspace.json` and imports the workspace.

### All commands

```bash
# Generate a starter config (-c . writes to ./klspw.yaml; without -c, prints to stdout)
klspw -c . init ./my-project              # explicit root
klspw -c . init -d ./src                  # discover roots under ./src
klspw -c . init "./proj gradlew"          # custom build command
klspw -c . init ./proj_1 ./proj_2 -b cmd  # multiple roots, global build

# Run Gradle and write workspace.json (reads ./klspw.yaml by default)
klspw generate
klspw -c config.yaml generate             # explicit config path

# Inspect discovered modules and libraries without writing
klspw inspect

# Validate config paths and build commands
klspw validate

# Save raw Gradle output for debugging
klspw generate --save-gradle-output output.txt
klspw inspect --save-gradle-output output.txt   # also works on inspect

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
3. The init script dumps project metadata as JSON between `KLSPW_BEGIN`/`KLSPW_END` delimiters, including Gradle-resolved source jar mappings, classpath coordinates (Maven `group:module:version` for each jar), and compiler plugin classpaths
4. Parse source sets, classpaths, and project structure from the JSON output
5. Name libraries using Maven coordinates (from classpath coordinates or Gradle cache paths) to avoid collisions in KMP projects where multiple libraries produce identically-named jars
6. For Android projects, pick one build variant (debug) to avoid class redeclaration errors from variant-specific source directories; include the R class jar from build intermediates so `R.layout.*`, `R.string.*`, etc. resolve correctly
7. Attach source jars to libraries (from Gradle resolution, filesystem discovery, then coordinate-based cache search as fallback)
8. Convert to kotlin-lsp workspace model (modules, libraries, kotlin settings with compiler plugin classpaths)
9. Merge results across roots, deduplicating libraries by name
10. Promote library dependencies to module dependencies when a library matches a workspace module (sibling Gradle root)
11. For KMP projects, inject a kotlin-native-stubs.jar providing JVM stubs for `kotlin.native.*` annotations that only exist in Kotlin/Native metadata
12. Write deterministic, pretty-printed `workspace.json`

## Editor setup with kotlin-lsp

[kotlin-lsp] provides Kotlin language support (completion, diagnostics, navigation, refactoring) for VS Code and [Kiro](https://kiro.dev/docs). It normally imports Gradle projects automatically, but that fails when the build runs through a wrapper or dependencies come from non-standard locations. klspw bridges this gap by generating a `workspace.json` that kotlin-lsp can import directly.

Prerequisites: Java 17+ on PATH.

1. Install kotlin-lsp (the language server):

   ```bash
   brew install JetBrains/utils/kotlin-lsp
   ```

2. Install the editor extension: download the latest `.vsix` from the [kotlin-lsp releases page](https://github.com/Kotlin/kotlin-lsp/releases), then install it via Extensions > `...` > Install from VSIX.

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

5. Open the project folder in VS Code or Kiro. kotlin-lsp detects `workspace.json` and uses it for project import instead of running Gradle itself.

6. If you change dependencies or project structure, re-run `klspw generate` and restart the language server with the `Kotlin LSP: Restart` command from the command palette.

To verify the import worked, check the kotlin-lsp output panel for messages about loaded modules and libraries.

## Project structure

```text
include/          # Public headers (header-only logic + declarations)
src/              # CLI entry point and .cpp implementations
test/             # Unit and integration tests (doctest)
  fixtures/       # Test data (YAML configs, Gradle projects)
resources/        # Gradle init script, CMake templates, and KMP stub jars
fuzz/             # libFuzzer fuzz targets
scripts/          # Utility scripts
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, dependencies, CI, and release process.

## License

[MIT](LICENSE)

[kotlin-lsp]: https://github.com/Kotlin/kotlin-lsp
