# Tech stack

## Language

- C++23 (`cxx_std_23`, `-std=c++23`)
- Compiler flags: `-Wall -Wextra -Wpedantic`
- Clang-tidy enabled via `.clangd` with broad rule set

## Build system

- CMake 3.25+ with CMake Presets (`CMakePresets.json`)
- Ninja generator
- vcpkg manifest mode for dependency management (`vcpkg.json`)
- `just` task runner wrapping CMake commands (`justfile`)
- Presets: `dev` (Debug), `release` (Release), `coverage` (Debug + clang coverage)

## Dependencies

All managed via vcpkg:

- [CLI11](https://github.com/CLIUtils/CLI11) -- command-line parsing
- [glaze](https://github.com/stephenberry/glaze) -- JSON and YAML serialization (compile-time reflection, no macros)
- [reproc++](https://github.com/DaanDeMeyer/reproc) -- subprocess execution
- [spdlog](https://github.com/gabime/spdlog) -- logging
- [doctest](https://github.com/doctest/doctest) -- testing framework

## Code formatting

- `.clang-format` based on LLVM style
- 120 column limit, 4-space indent
- Pointer/reference alignment: left (`int* p`, `int& r`)
- Include sorting: system headers first, then library headers, then project headers

## Common commands

```bash
# Prerequisites
brew install cmake ninja just
export VCPKG_ROOT="$HOME/vcpkg"

# Build and test (dev preset)
just check          # configure + build + test (all-in-one)
just configure      # cmake --preset dev
just build          # cmake --build --preset dev
just test           # ctest --preset dev

# Run the tool
just run -c config.yaml generate

# Clean rebuild
just rebuild        # clean + configure + build

# Release build
just build preset=release

# Coverage
just coverage       # build + run + merge + report + html
```

## Testing

- Framework: doctest (header-only, `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`)
- Test binaries: one per test file (named `*_test` or `*_tests`)
- Fixtures in `test/fixtures/` (YAML configs, JSON outputs)
- RAII test helpers for temp files/dirs in a shared test header
- Tests run from the source directory (working directory set in CMake)
- Integration tests require Gradle on PATH and network access; gated behind `ENABLE_INTEGRATION_TESTS` CMake option

```bash
# Integration tests
just integration    # configure with flag + build + run integration label
```

## Serialization conventions

- glaze handles both JSON and YAML via compile-time reflection
- C++ fields use `snake_case`; JSON keys use `camelCase` via `glz::camel_case` meta
- Tagged variant dispatch for `DependencyData` using `"type"` discriminator field
- Variant alternative structs keep `camelCase` C++ field names (glaze limitation with tagged variants)
- Write opts: pretty-print with 2-space indent, skip null optionals
- Read opts: ignore unknown keys for forward compatibility
