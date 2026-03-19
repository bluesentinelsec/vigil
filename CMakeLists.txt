cmake_minimum_required(VERSION 3.21)

project(vigil VERSION 0.1.3 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VIGIL_BUILD_TESTS "Build VIGIL unit tests" ON)
option(VIGIL_USE_LIBFFI "Build with vendored libffi for full FFI support" ON)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(VIGIL_LIBRARY_TYPE STATIC)
else()
    set(VIGIL_LIBRARY_TYPE SHARED)
endif()

# ── Core interpreter library ─────────────────────────────────────────
# Pure C11, no platform-specific headers.  Must compile on any
# conforming C11 toolchain (GCC, Clang, MSVC, Emscripten).
# When adding stdlib modules that need POSIX/Win32 APIs, put them in
# a separate vigil_stdlib target that links against vigil_core.
add_library(vigil_core OBJECT
    src/allocator.c
    src/array.c
    src/binding.c
    src/checker.c
    src/chunk.c
    src/cli_lib.c
    src/compiler.c
    src/dap.c
    src/doc.c
    src/doc_registry.c
    src/embed.c
    src/ffi_callback.c
    src/fmt.c
    src/pkg.c
    src/package.c
    src/debug_info.c
    src/semantic.c
    src/lsp.c
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

target_include_directories(vigil_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(vigil_core PUBLIC c_std_11)

# Objects may end up in a shared library, so they need PIC on Linux.
set_target_properties(vigil_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── Public shared/static library ────────────────────────────────────
# Today this is the core plus stdlib modules.  Stdlib modules may use
# platform-specific headers; the core (vigil_core) stays pure C11.
add_library(vigil ${VIGIL_LIBRARY_TYPE}
    $<TARGET_OBJECTS:vigil_core>
    src/stdlib/args.c
    src/stdlib/atomic.c
    src/stdlib/compress.c
    src/stdlib/crypto.c
    src/stdlib/csv.c
    src/stdlib/ffi.c
    src/stdlib/fmt.c
    src/stdlib/fs.c
    src/stdlib/http.c
    src/stdlib/log.c
    src/stdlib/math.c
    src/stdlib/net.c
    src/stdlib/parse.c
    src/stdlib/random.c
    src/stdlib/readline.c
    src/stdlib/regex.c
    src/stdlib/regex_engine.c
    src/stdlib/test.c
    src/stdlib/thread.c
    src/stdlib/time.c
    src/stdlib/unsafe.c
    src/stdlib/url.c
    src/stdlib/yaml.c
    src/url.c
    src/yaml.c
)

# ── Platform abstraction layer ──────────────────────────────────────
if(WIN32)
    target_sources(vigil PRIVATE src/platform/platform_win32.c src/platform/line_editor.c)
elseif(UNIX OR EMSCRIPTEN)
    target_sources(vigil PRIVATE src/platform/platform_posix.c src/platform/line_editor.c)
else()
    target_sources(vigil PRIVATE src/platform/platform_stub.c src/platform/line_editor.c)
endif()

target_include_directories(vigil
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(vigil PUBLIC c_std_11)
target_compile_definitions(vigil PUBLIC VIGIL_VERSION="${PROJECT_VERSION}")

if(NOT EMSCRIPTEN)
    target_compile_definitions(vigil
        PUBLIC VIGIL_SHARED
        PRIVATE VIGIL_EXPORTS
    )
    target_compile_definitions(vigil_core PRIVATE VIGIL_EXPORTS VIGIL_SHARED)
endif()

# Link libm where needed (Linux/glibc; MSVC/macOS/Emscripten have it built-in).
include(CheckLibraryExists)
check_library_exists(m floor "" VIGIL_NEEDS_LIBM)
if(VIGIL_NEEDS_LIBM)
    target_link_libraries(vigil PRIVATE m)
endif()

# Link libdl where needed (Linux; macOS has it in libSystem).
check_library_exists(dl dlopen "" VIGIL_NEEDS_LIBDL)
if(VIGIL_NEEDS_LIBDL)
    target_link_libraries(vigil PRIVATE dl)
endif()

# Vendored libffi for full FFI support.
# Disabled on platforms that lack dynamic linking or cross-compile.
if(VIGIL_USE_LIBFFI AND NOT EMSCRIPTEN
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_subdirectory(deps/libffi)
    target_link_libraries(vigil PRIVATE ffi_static)
    target_compile_definitions(vigil PRIVATE VIGIL_HAS_LIBFFI)
    target_compile_definitions(vigil_core PRIVATE VIGIL_HAS_LIBFFI)
endif()

# Vendored miniz for compression support.
add_subdirectory(deps/miniz)
target_link_libraries(vigil PRIVATE miniz)

# Vendored lz4 for fast compression.
add_subdirectory(deps/lz4)
target_link_libraries(vigil PRIVATE lz4)

# Vendored crypto for cryptographic operations.
add_subdirectory(deps/crypto)
target_link_libraries(vigil PRIVATE vigil_crypto)

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
    add_executable(vigil_cli
        src/cli/main.c
    )

    set_target_properties(vigil_cli PROPERTIES OUTPUT_NAME vigil)
    target_link_libraries(vigil_cli PRIVATE vigil)
    target_include_directories(vigil_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(vigil_cli PRIVATE c_std_11)
endif()

if(VIGIL_BUILD_TESTS)
    include(CTest)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    # Expose http.c internals for unit testing.
    target_compile_definitions(vigil PRIVATE VIGIL_HTTP_TESTING)

    add_executable(vigil_tests
        tests/test_main.c
        tests/array_test.c
        tests/vigil_new_test.c
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
        tests/ffi_test.c
        tests/fs_test.c
        tests/http_test.c
        tests/thread_test.c
        tests/unsafe_test.c
        tests/json_test.c
        tests/lexer_test.c
        tests/log_test.c
        tests/toml_test.c
        tests/url_test.c
        tests/yaml_test.c
        tests/map_test.c
        tests/platform_test.c
        tests/runtime_test.c
        tests/semantic_test.c
        tests/lsp_test.c
        tests/doc_registry_test.c
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

    target_link_libraries(vigil_tests PRIVATE vigil)
    target_include_directories(vigil_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    # Vendored libffi for tests.
    if(VIGIL_USE_LIBFFI AND NOT EMSCRIPTEN
       AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"
       AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        target_link_libraries(vigil_tests PRIVATE ffi_static)
        target_compile_definitions(vigil_tests PRIVATE VIGIL_HAS_LIBFFI)
    endif()

    # Small shared library for FFI dlopen tests.
    if(NOT EMSCRIPTEN)
        add_library(ffi_testlib SHARED tests/ffi_testlib.c)
        set_target_properties(ffi_testlib PROPERTIES
            PREFIX ""   # no "lib" prefix — predictable name
        )
        target_compile_definitions(vigil_tests PRIVATE
            FFI_TESTLIB_PATH="$<TARGET_FILE:ffi_testlib>"
        )
        add_dependencies(vigil_tests ffi_testlib)
    endif()

    add_test(NAME vigil_tests COMMAND vigil_tests)

    if (NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        function(vigil_add_integration_test test_name script_name)
            add_test(
                NAME ${test_name}
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/${script_name}
            )
            set_tests_properties(
                ${test_name}
                PROPERTIES ENVIRONMENT "VIGIL_BIN=$<TARGET_FILE:vigil_cli>"
            )
        endfunction()

        vigil_add_integration_test(VigilArgsTest test_args.py)
        vigil_add_integration_test(VigilAtomicTest test_atomic.py)
        vigil_add_integration_test(VigilCompressTest test_compress.py)
        vigil_add_integration_test(VigilCryptoTest test_crypto.py)
        vigil_add_integration_test(VigilCsvTest test_csv.py)
        vigil_add_integration_test(VigilDebugDAPTest test_debug_dap.py)
        vigil_add_integration_test(VigilDocTest test_doc.py)
        vigil_add_integration_test(VigilEmbedTest test_embed.py)
        vigil_add_integration_test(VigilFmtTest test_fmt.py)
        vigil_add_integration_test(VigilFsTest test_fs.py)
        vigil_add_integration_test(VigilLogTest test_log.py)
        vigil_add_integration_test(VigilMathTest test_math.py)
        vigil_add_integration_test(VigilNetTest test_net.py)
        vigil_add_integration_test(VigilPackageTest test_package.py)
        vigil_add_integration_test(VigilPkgTest test_pkg.py)
        vigil_add_integration_test(VigilRandomTest test_random.py)
        vigil_add_integration_test(VigilRegexTest test_regex.py)
        vigil_add_integration_test(VigilRegressionTest test_regression.py)
        vigil_add_integration_test(VigilReplTest test_repl.py)
        vigil_add_integration_test(VigilStringMethodsTest test_string_methods.py)
        vigil_add_integration_test(VigilSyntaxIntegrationTest test_syntax_integration.py)
        vigil_add_integration_test(VigilTestTest test_test.py)
        vigil_add_integration_test(VigilThreadTest test_thread.py)
        vigil_add_integration_test(VigilTimeTest test_time.py)
        vigil_add_integration_test(VigilUrlTest test_url.py)
        vigil_add_integration_test(VigilYamlTest test_yaml.py)
    endif()
endif()
