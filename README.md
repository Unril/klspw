# klspw

`klspw` is a small native CLI that generates `workspace.json` for [kotlin-lsp].

It targets repositories where the default `kotlin-lsp` project import does not work well enough -- when the real build runs through an internal wrapper around Gradle, or when dependencies come from custom package/cache layouts.

The tool reads `workspace-kotlin-lsp-config.yaml`, runs the configured Gradle command with a temporary init script, extracts source-set and classpath data, optionally enriches dependency source roots, and writes `workspace.json`.

## Status

Early, intentionally narrow in scope.

Current focus:

- Kotlin/JVM and Java/JVM projects
- Gradle-based discovery via temporary init script
- Custom package/cache layouts (e.g. Brazil-style package caches)
- Debuggable `inspect`, `validate`, and `generate` flows

Not a goal right now:

- Full Kotlin Multiplatform modeling
- Replacing Gradle
- General-purpose build tool
- Plugin architecture

## Project structure

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── justfile
├── README.md
├── AGENTS.md
├── src/
│   └── main.cpp
└── test/
    └── smoke.cpp
```

As the project grows, headers will live in `include/` following the layout described in `AGENTS.md`.

## Prerequisites

### macOS (Homebrew)

```bash
brew install cmake ninja vcpkg just
```

### vcpkg setup

Set `VCPKG_ROOT` to point at a vcpkg checkout:

```bash
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
export VCPKG_ROOT="$HOME/vcpkg"
```

Add the export to your shell profile so it persists.

## Build

```bash
just configure
just build
just test
```

Or all at once:

```bash
just check
```

### Manual commands

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

### Release build

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

## Commands

The CLI currently stubs three subcommands:

- `generate` -- generate `workspace.json`
- `inspect` -- print discovered modules, jars, and source roots
- `validate` -- validate config and discovered paths

```bash
./build/dev/klspw --help
./build/dev/klspw generate
./build/dev/klspw inspect
./build/dev/klspw validate
```

## Dependencies

- [CLI11](https://github.com/CLIUtils/CLI11) -- command-line parsing
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) -- YAML parsing
- [nlohmann/json](https://github.com/nlohmann/json) -- JSON emission
- [doctest](https://github.com/doctest/doctest) -- testing

All managed via vcpkg manifest mode.

## How it will work

High-level flow (not yet implemented):

1. Read `workspace-kotlin-lsp-config.yaml`
2. Resolve configured roots
3. Generate a temporary `init.gradle.kts`
4. Run the configured Gradle command
5. Extract a delimited JSON payload from stdout
6. Parse projects, source sets, classpaths, and outputs
7. Discover dependency sources when possible
8. Emit deterministic `workspace.json`

Source discovery strategy:

1. Prefer source roots reported by the build model
2. Apply filesystem heuristics for known package/cache layouts
3. Try source-jar discovery
4. Otherwise keep the dependency binary-only

## References

- [kotlin-lsp](https://github.com/Kotlin/kotlin-lsp)
- [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
- [vcpkg manifest mode](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode)

## License

TBD

[kotlin-lsp]: https://github.com/Kotlin/kotlin-lsp
