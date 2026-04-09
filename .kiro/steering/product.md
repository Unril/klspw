# klspw

klspw generates `workspace.json` files for [kotlin-lsp](https://github.com/Kotlin/kotlin-lsp) from Gradle builds.

It targets repositories where the default kotlin-lsp project import fails -- when the build runs through a wrapper around Gradle, or when dependencies come from custom package/cache layouts.

## What it does

1. Reads a YAML config describing Gradle roots and build commands
2. Runs each root's Gradle build with an injected init script that dumps project metadata as JSON (between `KLSPW_BEGIN`/`KLSPW_END` delimiters)
3. Parses source sets, classpaths, and project structure from the Gradle output
4. Converts to the kotlin-lsp workspace model (modules, libraries, kotlin settings)
5. Merges results across roots, deduplicates libraries by name
6. Writes deterministic, pretty-printed `workspace.json`

## CLI subcommands

- `klspw init {root}` -- generate a starter config YAML for a Gradle root
- `klspw -c config.yaml generate` -- run Gradle, write `workspace.json`
- `klspw -c config.yaml inspect` -- run Gradle, log discovered modules/libraries
- `klspw -c config.yaml validate` -- check config paths and build commands

## Config file

The config file (`workspace-kotlin-lsp-config.yaml`) defines:

- Global and per-root build commands (e.g., `./gradlew`)
- Gradle root project paths (resolved relative to config file directory)
- Output workspace file path
- JVM target version
- Options: `include_tests`, `attach_sources`, `follow_symlinks`
