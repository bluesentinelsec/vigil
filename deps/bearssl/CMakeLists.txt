cmake_minimum_required(VERSION 3.21)

include(FetchContent)

FetchContent_Declare(
    bearssl_upstream
    GIT_REPOSITORY https://bearssl.org/git/BearSSL
    GIT_TAG        v0.6
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(bearssl_upstream)

file(GLOB_RECURSE BEARSSL_SOURCES "${bearssl_upstream_SOURCE_DIR}/src/*.c")

add_library(bearssl_static STATIC ${BEARSSL_SOURCES})

target_include_directories(bearssl_static
    PUBLIC  "${bearssl_upstream_SOURCE_DIR}/inc"
    PRIVATE "${bearssl_upstream_SOURCE_DIR}/src"
)

# Suppress warnings in vendored third-party code.
if(MSVC)
    target_compile_options(bearssl_static PRIVATE /w)
else()
    target_compile_options(bearssl_static PRIVATE -w)
endif()

set_target_properties(bearssl_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
