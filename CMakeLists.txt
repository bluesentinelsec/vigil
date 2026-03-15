cmake_minimum_required(VERSION 3.21)

project(basl VERSION 0.1.3 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BASL_BUILD_TESTS "Build BASL unit tests" ON)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(BASL_LIBRARY_TYPE STATIC)
else()
    set(BASL_LIBRARY_TYPE SHARED)
endif()

# ── Core interpreter library ─────────────────────────────────────────
# Pure C11, no platform-specific headers.  Must compile on any
# conforming C11 toolchain (GCC, Clang, MSVC, Emscripten).
# When adding stdlib modules that need POSIX/Win32 APIs, put them in
# a separate basl_stdlib target that links against basl_core.
add_library(basl_core OBJECT
    src/allocator.c
    src/array.c
    src/binding.c
    src/checker.c
    src/chunk.c
    src/cli_lib.c
    src/compiler.c
    src/dap.c
    src/doc.c
    src/embed.c
    src/fmt.c
    src/package.c
    src/debug_info.c
    src/debugger.c
    src/compiler_program.c
    src/compiler_builtins.c
    src/compiler_strings.c
    src/compiler_typeparsing.c
    src/compiler_declarations.c
    src/compiler_types.c
    src/diagnostic.c
    src/json.c
    src/jsonrpc.c
    src/toml.c
    src/lexer.c
    src/log.c
    src/map.c
    src/native_module.c
    src/runtime.c
    src/source.c
    src/string.c
    src/status.c
    src/symbol.c
    src/token.c
    src/type.c
    src/value.c
    src/vm.c
)

target_include_directories(basl_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(basl_core PUBLIC c_std_11)

# Objects may end up in a shared library, so they need PIC on Linux.
set_target_properties(basl_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── Public shared/static library ────────────────────────────────────
# Today this is the core plus stdlib modules.  Stdlib modules may use
# platform-specific headers; the core (basl_core) stays pure C11.
add_library(basl ${BASL_LIBRARY_TYPE}
    $<TARGET_OBJECTS:basl_core>
    src/stdlib/args.c
    src/stdlib/fmt.c
    src/stdlib/math.c
    src/stdlib/test.c
)

# ── Platform abstraction layer ──────────────────────────────────────
if(WIN32)
    target_sources(basl PRIVATE src/platform/platform_win32.c)
elseif(UNIX OR EMSCRIPTEN)
    target_sources(basl PRIVATE src/platform/platform_posix.c)
else()
    target_sources(basl PRIVATE src/platform/platform_stub.c)
endif()

target_include_directories(basl
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(basl PUBLIC c_std_11)

if(NOT EMSCRIPTEN)
    target_compile_definitions(basl
        PUBLIC BASL_SHARED
        PRIVATE BASL_EXPORTS
    )
    target_compile_definitions(basl_core PRIVATE BASL_EXPORTS BASL_SHARED)
endif()

# Link libm where needed (Linux/glibc; MSVC/macOS/Emscripten have it built-in).
include(CheckLibraryExists)
check_library_exists(m floor "" BASL_NEEDS_LIBM)
if(BASL_NEEDS_LIBM)
    target_link_libraries(basl PRIVATE m)
endif()

# On MSVC multi-config generators, an OBJECT-library-backed shared
# library may place its import library (.lib) in a different directory
# than dependents expect.  Pin all output directories so the DLL,
# import lib, and executables land together.
if(MSVC)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    foreach(CFG RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/${CFG}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/${CFG}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/${CFG}")
    endforeach()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_executable(basl_cli
        src/cli/main.c
    )

    set_target_properties(basl_cli PROPERTIES OUTPUT_NAME basl)
    target_link_libraries(basl_cli PRIVATE basl)
    target_include_directories(basl_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(basl_cli PRIVATE c_std_11)
endif()

if(BASL_BUILD_TESTS)
    include(CTest)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    add_executable(basl_tests
        tests/test_main.c
        tests/array_test.c
        tests/basl_new_test.c
        tests/binding_test.c
        tests/checker_test.c
        tests/chunk_test.c
        tests/cli_lib_test.c
        tests/compiler_test.c
        tests/dap_test.c
        tests/doc_test.c
        tests/debug_info_test.c
        tests/debugger_test.c
        tests/diagnostic_test.c
        tests/json_test.c
        tests/lexer_test.c
        tests/log_test.c
        tests/toml_test.c
        tests/map_test.c
        tests/platform_test.c
        tests/runtime_test.c
        tests/source_test.c
        tests/stdlib_test.c
        tests/string_test.c
        tests/status_test.c
        tests/symbol_test.c
        tests/token_test.c
        tests/type_test.c
        tests/value_test.c
        tests/vm_test.c
    )

    target_link_libraries(basl_tests PRIVATE basl)
    target_include_directories(basl_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME basl_tests COMMAND basl_tests)

    if (NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        add_test(
            NAME BaslSyntaxIntegrationTest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/test_syntax_integration.py
        )
        set_tests_properties(
            BaslSyntaxIntegrationTest
            PROPERTIES ENVIRONMENT "BASL_BIN=$<TARGET_FILE:basl_cli>"
        )
        add_test(
            NAME BaslRegressionTest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/test_regression.py
        )
        set_tests_properties(
            BaslRegressionTest
            PROPERTIES ENVIRONMENT "BASL_BIN=$<TARGET_FILE:basl_cli>"
        )
        add_test(
            NAME BaslDebugDAPTest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/test_debug_dap.py
        )
        set_tests_properties(
            BaslDebugDAPTest
            PROPERTIES ENVIRONMENT "BASL_BIN=$<TARGET_FILE:basl_cli>"
        )
        add_test(
            NAME BaslDocTest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/test_doc.py
        )
        set_tests_properties(
            BaslDocTest
            PROPERTIES ENVIRONMENT "BASL_BIN=$<TARGET_FILE:basl_cli>"
        )
        add_test(
            NAME BaslPackageTest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/test_package.py
        )
        set_tests_properties(
            BaslPackageTest
            PROPERTIES ENVIRONMENT "BASL_BIN=$<TARGET_FILE:basl_cli>"
        )
        add_test(
            NAME BaslFmtTest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/test_fmt.py
        )
        set_tests_properties(
            BaslFmtTest
            PROPERTIES ENVIRONMENT "BASL_BIN=$<TARGET_FILE:basl_cli>"
        )
        add_test(
            NAME BaslEmbedTest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/test_embed.py
        )
        set_tests_properties(
            BaslEmbedTest
            PROPERTIES ENVIRONMENT "BASL_BIN=$<TARGET_FILE:basl_cli>"
        )
        add_test(
            NAME BaslTestTest
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/test_test.py
        )
        set_tests_properties(
            BaslTestTest
            PROPERTIES ENVIRONMENT "BASL_BIN=$<TARGET_FILE:basl_cli>"
        )
    endif()
endif()
