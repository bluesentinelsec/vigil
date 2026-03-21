cmake_minimum_required(VERSION 3.21)

# BearSSL v0.6 — vendored from https://bearssl.org/git/BearSSL (tag v0.6)
# Source lives in deps/bearssl/src/; headers in deps/bearssl/inc/.

file(GLOB_RECURSE BEARSSL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(bearssl_static STATIC ${BEARSSL_SOURCES})

target_include_directories(bearssl_static
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Suppress warnings in vendored third-party code.
if(MSVC)
    target_compile_options(bearssl_static PRIVATE /w)
else()
    target_compile_options(bearssl_static PRIVATE -w)
endif()

set_target_properties(bearssl_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
