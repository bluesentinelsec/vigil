cmake_minimum_required(VERSION 3.21)

project(basl VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BASL_BUILD_TESTS "Build BASL unit tests" ON)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(EMSCRIPTEN)
    set(BASL_LIBRARY_TYPE STATIC)
else()
    set(BASL_LIBRARY_TYPE SHARED)
endif()

add_library(basl ${BASL_LIBRARY_TYPE}
    src/allocator.c
    src/array.c
    src/chunk.c
    src/diagnostic.c
    src/runtime.c
    src/source.c
    src/string.c
    src/status.c
    src/value.c
)

target_include_directories(basl
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(basl PUBLIC c_std_11)

if(NOT EMSCRIPTEN)
    target_compile_definitions(basl
        PUBLIC BASL_SHARED
        PRIVATE BASL_EXPORTS
    )
endif()

add_executable(basl_cli
    src/cli/main.c
)

set_target_properties(basl_cli PROPERTIES OUTPUT_NAME basl)
target_link_libraries(basl_cli PRIVATE basl)
target_compile_features(basl_cli PRIVATE c_std_11)

if(BASL_BUILD_TESTS)
    include(CTest)
    include(FetchContent)

    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(basl_tests
        tests/array_test.cpp
        tests/chunk_test.cpp
        tests/diagnostic_test.cpp
        tests/runtime_test.cpp
        tests/source_test.cpp
        tests/string_test.cpp
        tests/status_test.cpp
        tests/value_test.cpp
    )

    target_link_libraries(basl_tests PRIVATE basl GTest::gtest_main)
    target_compile_features(basl_tests PRIVATE cxx_std_17)

    include(GoogleTest)
    gtest_discover_tests(basl_tests)
endif()
