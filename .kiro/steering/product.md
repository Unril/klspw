# klspw

klspw generates `workspace.json` files for [kotlin-lsp](https://github.com/Kotlin/kotlin-lsp) from Gradle builds.

It targets repositories where the default kotlin-lsp project import fails -- when the build runs through a wrapper around Gradle, or when dependencies come from custom package/cache layouts.

## What it does

1. Reads a YAML config describing Gradle roots and build commands
2. Runs each root's Gradle build with an injected init script that dumps project metadata as JSON (between `KLSPW_BEGIN`/`KLSPW_END` delimiters)
3. Parses source sets, classpaths, and project structure from the Gradle output
4. Converts to the kotlin-lsp workspace model (modules, libraries, kotlin settings)
5. Uses Maven coordinates as library names (from Gradle component IDs and cache paths) to avoid naming collisions in KMP projects
6. For Android projects, picks one build variant (debug) to avoid class redeclaration errors
7. Merges results across roots, deduplicates libraries by name
8. Promotes library dependencies to module dependencies when a library matches a workspace module
9. Attaches source jars from Gradle-resolved mappings, filesystem discovery, and coordinate-based cache search
10. Injects kotlin-native-stubs.jar for KMP projects (provides JVM stubs for `kotlin.native.*` annotations)
11. Includes Kotlin compiler plugin classpaths (serialization, compose) in compiler arguments
12. Writes deterministic, pretty-printed `workspace.json`

## CLI subcommands

- `klspw init {roots...}` -- generate a starter config YAML; each arg is `"path [build_command...]"`
- `klspw generate` -- run Gradle, write `workspace.json` (uses `./klspw.yaml` by default)
- `klspw inspect` -- run Gradle, log discovered modules/libraries
- `klspw validate` -- check config paths and build commands
- `-c` flag overrides the config path (file or directory)
- `-b` flag sets the global build command (init only)
- `-d` flag on `init` discovers Gradle roots recursively under the given directories

## Config file

The config file (default name: `klspw.yaml`) defines:

- Global and per-root build commands (e.g., `./gradlew`)
- Gradle root project paths (resolved relative to config file directory)
- Output workspace file path
- JVM target version
- Options: `include_tests`, `attach_sources`, `remove_missing_paths`
