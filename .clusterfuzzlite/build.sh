#!/bin/bash -eu
# ClusterFuzzLite build script for klspw.
# Builds fuzz targets using the OSS-Fuzz toolchain (Clang + libFuzzer).
#
# Environment variables provided by the base image:
#   $CC, $CXX           -- Clang compilers
#   $CFLAGS, $CXXFLAGS  -- sanitizer + coverage flags (includes fuzzer-no-link)
#   $LIB_FUZZING_ENGINE -- libFuzzer archive to link fuzz targets against
#   $OUT                -- output directory for fuzz target binaries
#   $SRC                -- source directory

cd "$SRC/klspw"

# Build the core library with sanitizer instrumentation but without libFuzzer main.
# CXXFLAGS already contains -fsanitize=fuzzer-no-link from the base image.
# Do NOT pass -fsanitize=fuzzer as a global linker flag -- it would conflict
# with main() in the klspw executable.
cmake -S . -B build/fuzz \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_CXX_STANDARD=23 \
  -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_CXX_EXTENSIONS=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
  -DBUILD_TESTING=OFF

# Build only the static library (not the klspw executable which has its own main).
cmake --build build/fuzz --target klspw_lib --parallel

# Link each fuzz target against the instrumented library and libFuzzer.
for fuzz_src in fuzz/fuzz_*.cpp; do
  target_name=$(basename "${fuzz_src%.cpp}")
  $CXX $CXXFLAGS -std=c++23 \
    -Iinclude \
    -Ibuild/fuzz/generated \
    -isystem build/fuzz/vcpkg_installed/x64-linux/include \
    -c "$fuzz_src" \
    -o "build/fuzz/${target_name}.o"

  $CXX $CXXFLAGS \
    "build/fuzz/${target_name}.o" \
    build/fuzz/libklspw_lib.a \
    -Lbuild/fuzz/vcpkg_installed/x64-linux/lib \
    -lspdlog -lfmt -lreproc++ -lreproc \
    $LIB_FUZZING_ENGINE \
    -lstdc++ -lm \
    -o "$OUT/$target_name"
done
