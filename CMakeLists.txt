cmake_minimum_required(VERSION 3.25)

project(klspw VERSION 0.1.0 LANGUAGES CXX)

# Use ccache when available for faster rebuilds.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

include(CTest)
include(GNUInstallDirs)

find_package(CLI11 CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Embed resources/init.gradle.kts into a generated header at configure time.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/init.gradle.kts" KLSPW_INIT_SCRIPT)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/init_script_content.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/init_script_content.hpp"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp"
    @ONLY
)

# --- Core library ---

add_library(klspw_lib STATIC src/strings.cpp src/files.cpp)
target_include_directories(klspw_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_features(klspw_lib PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(klspw_lib PUBLIC /W4 /permissive-)
else()
    target_compile_options(klspw_lib PUBLIC -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(klspw_lib PUBLIC
    CLI11::CLI11
    glaze::glaze
    reproc++
    spdlog::spdlog
)

# --- Optional instrumentation (interface targets) ---

option(ENABLE_COVERAGE "Enable clang source-based coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING "ENABLE_COVERAGE is designed for Clang; results may vary with ${CMAKE_CXX_COMPILER_ID}")
    endif()
    add_library(klspw_coverage INTERFACE)
    target_compile_options(klspw_coverage INTERFACE -fprofile-instr-generate -fcoverage-mapping -O0 -g)
    target_link_options(klspw_coverage INTERFACE -fprofile-instr-generate)
endif()

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ENABLE_SANITIZERS)
    add_library(klspw_sanitizers INTERFACE)
    target_compile_options(klspw_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(klspw_sanitizers INTERFACE -fsanitize=address,undefined)
endif()

# --- Main executable ---

add_executable(klspw src/main.cpp)
target_link_libraries(klspw PRIVATE
    klspw_lib
    $<$<BOOL:${ENABLE_SANITIZERS}>:klspw_sanitizers>
)
set_target_properties(klspw PROPERTIES OUTPUT_NAME klspw)
install(TARGETS klspw RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# --- Tests ---

option(ENABLE_INTEGRATION_TESTS "Build integration tests (requires Gradle)" OFF)

if(BUILD_TESTING)
    find_package(doctest CONFIG REQUIRED)

    # Helper: add a doctest executable with standard linkage and working directory.
    # Extra args after source are forwarded to set_tests_properties (e.g. LABELS "integration").
    function(klspw_add_test target source)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
            return()
        endif()
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE
            klspw_lib
            doctest::doctest
            $<$<BOOL:${ENABLE_COVERAGE}>:klspw_coverage>
            $<$<BOOL:${ENABLE_SANITIZERS}>:klspw_sanitizers>
        )
        target_compile_definitions(${target} PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            ${ARGN}
        )
    endfunction()

    klspw_add_test(klspw_tests           test/smoke.cpp)
    klspw_add_test(config_test           test/config_test.cpp)
    klspw_add_test(gradle_test           test/gradle_test.cpp)
    klspw_add_test(workspace_json_test   test/workspace_json_test.cpp)
    klspw_add_test(common_test           test/common_test.cpp)
    klspw_add_test(process_test          test/process_test.cpp)
    klspw_add_test(config_validate_test  test/config_validate_test.cpp)
    klspw_add_test(config_save_test      test/config_save_test.cpp)
    klspw_add_test(sources_test          test/sources_test.cpp)

    if(ENABLE_INTEGRATION_TESTS)
        klspw_add_test(integration_test  test/integration_test.cpp  LABELS "integration")
    endif()
endif()
