#!/bin/bash -eu
# ClusterFuzzLite build script for klspw.
# Builds fuzz targets using the OSS-Fuzz toolchain (Clang + libFuzzer).
# Environment variables provided by the base image:
#   $CC, $CXX       -- Clang compilers
#   $CFLAGS, $CXXFLAGS -- sanitizer + coverage flags
#   $LIB_FUZZING_ENGINE -- -fsanitize=fuzzer linker flag
#   $OUT            -- output directory for fuzz target binaries

cd "$SRC/klspw"

# Configure with vcpkg and the OSS-Fuzz toolchain.
# BUILD_TESTING=OFF: skip doctest targets (not needed for fuzzing).
cmake -S . -B build/fuzz \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=23 \
  -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_CXX_EXTENSIONS=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DBUILD_TESTING=OFF \
  -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$LIB_FUZZING_ENGINE"

cmake --build build/fuzz --parallel

# Build each fuzz target manually, linking against the static library and libFuzzer.
INCLUDES="-Iinclude -Ibuild/fuzz/generated -isystem build/fuzz/vcpkg_installed/x64-linux/include"
LIBS="build/fuzz/libklspw_lib.a"
VCPKG_LIBS="build/fuzz/vcpkg_installed/x64-linux/lib"

for fuzz_src in fuzz/fuzz_*.cpp; do
  target_name=$(basename "${fuzz_src%.cpp}")
  $CXX $CXXFLAGS -std=c++23 $INCLUDES \
    "$fuzz_src" \
    $LIBS \
    -L"$VCPKG_LIBS" -lglaze -lspdlog -lfmt -lreproc -lreproc++ -lCLI11 \
    $LIB_FUZZING_ENGINE \
    -o "$OUT/$target_name"
done
