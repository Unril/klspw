cmake_minimum_required(VERSION 3.25)

project(klspw VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

find_package(CLI11 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)

add_library(klspw_lib INTERFACE)
target_include_directories(klspw_lib
    INTERFACE    
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(klspw_lib INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(klspw_lib INTERFACE /W4 /permissive-)
else()
    target_compile_options(klspw_lib INTERFACE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(klspw_lib
    INTERFACE
    CLI11::CLI11
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
    reproc++
)

add_executable(klspw
    src/main.cpp
)

target_link_libraries(klspw
    PRIVATE
    klspw_lib
)

set_target_properties(klspw PROPERTIES
    OUTPUT_NAME klspw
)

install(TARGETS klspw
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/smoke.cpp")
    find_package(doctest CONFIG REQUIRED)

    add_executable(klspw_tests
        test/smoke.cpp
    )

    target_link_libraries(klspw_tests
        PRIVATE
        klspw_lib
        doctest::doctest
    )

    add_test(NAME klspw_tests COMMAND klspw_tests)
endif()
