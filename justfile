preset := "dev"

configure:
    cmake --preset {{ preset }}

build:
    cmake --build --preset {{ preset }}

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

# Format all C++ source and header files with clang-format
format:
    find include src test -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i

# Integration tests (requires Gradle on PATH)
integration-configure:
    cmake --preset {{ preset }} -DENABLE_INTEGRATION_TESTS=ON

integration-build: integration-configure
    cmake --build --preset {{ preset }}

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

# All instrumented test binaries. Add new test targets here.
cov-objects := cov-build + "/klspw_tests " + "-object " + cov-build + "/config_test " + "-object " + cov-build + "/gradle_test " + "-object " + cov-build + "/workspace_json_test"

coverage-clean:
    rm -rf {{ cov-dir }}

coverage-configure:
    cmake --preset coverage

coverage-build: coverage-configure
    cmake --build --preset coverage

coverage-run: coverage-clean coverage-build
    mkdir -p {{ cov-dir }}
    LLVM_PROFILE_FILE="{{ cov-dir }}/%p-%m.profraw" ctest --preset coverage

coverage-merge: coverage-run
    llvm-profdata merge -sparse {{ cov-dir }}/*.profraw -o {{ cov-dir }}/coverage.profdata

coverage-report: coverage-merge
    llvm-cov report \
        {{ cov-objects }} \
        -instr-profile={{ cov-dir }}/coverage.profdata \
        -ignore-filename-regex='(vcpkg_installed|doctest|_deps|test/)'

coverage-html: coverage-merge
    llvm-cov show \
        {{ cov-objects }} \
        -instr-profile={{ cov-dir }}/coverage.profdata \
        -ignore-filename-regex='(vcpkg_installed|doctest|_deps|test/)' \
        -format=html \
        -output-dir={{ cov-dir }}/html
    @echo "HTML report: {{ cov-dir }}/html/index.html"

coverage: coverage-report coverage-html
    @echo "Report dir: {{ cov-dir }}/html"
