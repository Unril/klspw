cmake_minimum_required(VERSION 3.25)

project(klspw VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

find_package(CLI11 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)

# Embed resources/init.gradle.kts into a generated header at configure time.
file(
    READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/init.gradle.kts"
    KLSPW_INIT_SCRIPT
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/init_script_content.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/init_script_content.hpp"
    @ONLY
)

add_library(klspw_lib INTERFACE)
target_include_directories(
    klspw_lib
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_features(klspw_lib INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(klspw_lib INTERFACE /W4 /permissive-)
else()
    target_compile_options(klspw_lib INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Coverage instrumentation (clang source-based, opt-in).
option(ENABLE_COVERAGE "Enable clang source-based coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(
            WARNING
            "ENABLE_COVERAGE is designed for Clang; results may vary with ${CMAKE_CXX_COMPILER_ID}"
        )
    endif()
    add_library(klspw_coverage INTERFACE)
    target_compile_options(
        klspw_coverage
        INTERFACE -fprofile-instr-generate -fcoverage-mapping -O0 -g
    )
    target_link_options(klspw_coverage INTERFACE -fprofile-instr-generate)
endif()

target_link_libraries(
    klspw_lib
    INTERFACE
        CLI11::CLI11
        yaml-cpp::yaml-cpp
        nlohmann_json::nlohmann_json
        reproc++
)

add_executable(klspw src/main.cpp)

target_link_libraries(klspw PRIVATE klspw_lib)

set_target_properties(klspw PROPERTIES OUTPUT_NAME klspw)

install(TARGETS klspw RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/smoke.cpp")
    find_package(doctest CONFIG REQUIRED)

    add_executable(klspw_tests test/smoke.cpp)

    target_link_libraries(
        klspw_tests
        PRIVATE
            klspw_lib
            doctest::doctest
            $<$<BOOL:${ENABLE_COVERAGE}>:klspw_coverage>
    )

    add_test(NAME klspw_tests COMMAND klspw_tests)
    set_tests_properties(
        klspw_tests
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/config_test.cpp")
    find_package(doctest CONFIG REQUIRED)

    add_executable(config_test test/config_test.cpp)

    target_link_libraries(
        config_test
        PRIVATE
            klspw_lib
            doctest::doctest
            $<$<BOOL:${ENABLE_COVERAGE}>:klspw_coverage>
    )

    target_compile_definitions(
        config_test
        PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    )

    add_test(NAME config_test COMMAND config_test)
    set_tests_properties(
        config_test
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/gradle_test.cpp")
    find_package(doctest CONFIG REQUIRED)

    add_executable(gradle_test test/gradle_test.cpp)

    target_link_libraries(
        gradle_test
        PRIVATE
            klspw_lib
            doctest::doctest
            $<$<BOOL:${ENABLE_COVERAGE}>:klspw_coverage>
    )

    target_compile_definitions(
        gradle_test
        PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    )

    add_test(NAME gradle_test COMMAND gradle_test)
    set_tests_properties(
        gradle_test
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/workspace_json_test.cpp")
    find_package(doctest CONFIG REQUIRED)

    add_executable(workspace_json_test
        test/workspace_json_test.cpp
    )

    target_link_libraries(workspace_json_test
        PRIVATE
        klspw_lib
        doctest::doctest
        $<$<BOOL:${ENABLE_COVERAGE}>:klspw_coverage>
    )

    target_compile_definitions(workspace_json_test PRIVATE
        DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    )

    add_test(NAME workspace_json_test COMMAND workspace_json_test)
    set_tests_properties(workspace_json_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/common_test.cpp")
    find_package(doctest CONFIG REQUIRED)

    add_executable(common_test
        test/common_test.cpp
    )

    target_link_libraries(common_test
        PRIVATE
        klspw_lib
        doctest::doctest
        $<$<BOOL:${ENABLE_COVERAGE}>:klspw_coverage>
    )

    target_compile_definitions(common_test PRIVATE
        DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    )

    add_test(NAME common_test COMMAND common_test)
    set_tests_properties(common_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/process_test.cpp")
    find_package(doctest CONFIG REQUIRED)

    add_executable(process_test
        test/process_test.cpp
    )

    target_link_libraries(process_test
        PRIVATE
        klspw_lib
        doctest::doctest
        $<$<BOOL:${ENABLE_COVERAGE}>:klspw_coverage>
    )

    target_compile_definitions(process_test PRIVATE
        DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    )

    add_test(NAME process_test COMMAND process_test)
    set_tests_properties(process_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()
