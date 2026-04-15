preset := "dev"

configure:
    cmake --preset {{ preset }}

build:
    cmake --build --preset {{ preset }} --parallel

test:
    ctest --preset {{ preset }}

check: build test

clean:
    rm -rf build/{{ preset }}

rebuild: clean configure build

run *args: build
    ./build/{{ preset }}/klspw {{ args }}

release:
    just preset=release configure build test

# ASan + UBSan build and test (separate build dir).
sanitize:
    just preset=sanitize configure build test

install prefix="/usr/local":
    just release
    cmake --install build/release --prefix {{ prefix }}

# Format all C++ source and header files with clang-format, CMake files with gersemi
format:
    find include src test fuzz -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i
    gersemi -i CMakeLists.txt
    prettier --write '.github/**/*.yml' '.clusterfuzzlite/*.yaml' 'CMakePresets.json'
    shfmt -w -i 2 scripts/ .clusterfuzzlite/build.sh
    uvx black scripts/*.py
    find resources test/fixtures -name '*.kt' -o -name '*.kts' | xargs ktfmt --google-style

# Add or update SPDX license headers on source files (requires: uv)
license-headers:
    uvx reuse annotate --license MIT --copyright "Nikolai Fedorov" --merge-copyrights \
        --skip-unrecognised --recursive include/ src/ fuzz/ scripts/ resources/init.gradle.kts CMakeLists.txt


# Fuzz targets (requires Clang, uses libFuzzer + ASan)
fuzz-build:
    cmake --preset fuzz
    cmake --build --preset fuzz --parallel

# Run a fuzz target for 60 seconds (default: fuzz_config_yaml)
fuzz target="fuzz_config_yaml" seconds="60": fuzz-build
    mkdir -p fuzz/corpus/{{ target }}
    ./build/fuzz/{{ target }} fuzz/corpus/{{ target }} \
        -max_total_time={{ seconds }} \
        -print_final_stats=1 \
        -print_corpus_stats=0 \
        -verbosity=0

# Integration tests (requires Gradle on PATH)
integration-configure:
    cmake --preset {{ preset }} -DENABLE_INTEGRATION_TESTS=ON

integration-build: integration-configure
    cmake --build --preset {{ preset }} --parallel

integration: integration-build
    ctest --preset {{ preset }} -L integration

# --- Coverage (clang source-based, isolated build dir) ---
# Usage: just coverage
# Requires: llvm-profdata, llvm-cov (same LLVM toolchain as clang++)
#
# Uses a dedicated 'coverage' preset with its own build/coverage/ dir.
# Always wipes old profraw/profdata before collecting to avoid stale artifacts.
# LLVM raw profiles are not forward/backward compatible across compiler revisions.

cov-build := "build/coverage"
cov-dir := cov-build + "/coverage"

coverage-clean:
    rm -rf {{ cov-dir }}

coverage-configure:
    cmake --preset coverage

coverage-build: coverage-configure
    cmake --build --preset coverage --parallel

coverage-run: coverage-clean coverage-build
    mkdir -p {{ cov-dir }}
    LLVM_PROFILE_FILE="{{ cov-dir }}/%p-%m.profraw" ctest --preset coverage

coverage-merge: coverage-run
    llvm-profdata merge -sparse {{ cov-dir }}/*.profraw -o {{ cov-dir }}/coverage.profdata

coverage-report: coverage-merge
    #!/usr/bin/env bash
    objects=$(find {{ cov-build }} -maxdepth 1 -name '*_test*' -perm +111 -type f | sort | awk 'NR==1{printf "%s",$0} NR>1{printf " -object %s",$0}')
    llvm-cov report \
        $objects \
        -instr-profile={{ cov-dir }}/coverage.profdata \
        -ignore-filename-regex='(vcpkg_installed|doctest|_deps|test/)'

coverage-html: coverage-merge
    #!/usr/bin/env bash
    objects=$(find {{ cov-build }} -maxdepth 1 -name '*_test*' -perm +111 -type f | sort | awk 'NR==1{printf "%s",$0} NR>1{printf " -object %s",$0}')
    llvm-cov show \
        $objects \
        -instr-profile={{ cov-dir }}/coverage.profdata \
        -ignore-filename-regex='(vcpkg_installed|doctest|_deps|test/)' \
        -format=html \
        -output-dir={{ cov-dir }}/html
    echo "HTML report: {{ cov-dir }}/html/index.html"

coverage: coverage-report coverage-html
    @echo "Report dir: {{ cov-dir }}/html"
