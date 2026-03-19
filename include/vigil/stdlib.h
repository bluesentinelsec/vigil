#ifndef VIGIL_STDLIB_H
#define VIGIL_STDLIB_H

#include <string.h>

#include "vigil/native_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Custom log handler for embedders ────────────────────────────── */

/**
 * Custom log handler callback type.
 * @param level     Log level: 0=debug, 1=info, 2=warn, 3=error
 * @param msg       The log message
 * @param attrs     JSON-encoded key-value attributes (e.g., {"key":"value"})
 * @param user_data User-provided context from vigil_log_set_handler
 */
typedef void (*vigil_log_handler_t)(
    int level,
    const char *msg,
    const char *attrs,
    void *user_data
);

/**
 * Set a custom log handler to intercept all VIGIL log calls.
 * Pass NULL to restore the default handler.
 */
VIGIL_API void vigil_log_set_handler(vigil_log_handler_t handler, void *user_data);

/* Each stdlib module exports a const descriptor. */
extern VIGIL_API const vigil_native_module_t vigil_stdlib_args;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_atomic;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_compress;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_crypto;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_csv;
#ifdef VIGIL_HAS_STDLIB_FFI
extern VIGIL_API const vigil_native_module_t vigil_stdlib_ffi;
#endif
extern VIGIL_API const vigil_native_module_t vigil_stdlib_fmt;
#ifdef VIGIL_HAS_STDLIB_FS
extern VIGIL_API const vigil_native_module_t vigil_stdlib_fs;
#endif
#ifdef VIGIL_HAS_STDLIB_HTTP
extern VIGIL_API const vigil_native_module_t vigil_stdlib_http;
#endif
extern VIGIL_API const vigil_native_module_t vigil_stdlib_log;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_math;
#ifdef VIGIL_HAS_STDLIB_NET
extern VIGIL_API const vigil_native_module_t vigil_stdlib_net;
#endif
extern VIGIL_API const vigil_native_module_t vigil_stdlib_parse;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_random;
#ifdef VIGIL_HAS_STDLIB_READLINE
extern VIGIL_API const vigil_native_module_t vigil_stdlib_readline;
#endif
extern VIGIL_API const vigil_native_module_t vigil_stdlib_regex;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_test;
#ifdef VIGIL_HAS_STDLIB_THREAD
extern VIGIL_API const vigil_native_module_t vigil_stdlib_thread;
#endif
#ifdef VIGIL_HAS_STDLIB_TIME
extern VIGIL_API const vigil_native_module_t vigil_stdlib_time;
#endif
extern VIGIL_API const vigil_native_module_t vigil_stdlib_unsafe;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_url;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_yaml;

/* Register all built-in stdlib modules into a native registry. */
static inline vigil_status_t vigil_stdlib_register_all(
    vigil_native_registry_t *registry,
    vigil_error_t *error
) {
    vigil_status_t status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_args, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_atomic, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_compress, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_crypto, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_csv, error);
    if (status != VIGIL_STATUS_OK) return status;
#ifdef VIGIL_HAS_STDLIB_FFI
    status = vigil_native_registry_add(registry, &vigil_stdlib_ffi, error);
    if (status != VIGIL_STATUS_OK) return status;
#endif
    status = vigil_native_registry_add(registry, &vigil_stdlib_fmt, error);
    if (status != VIGIL_STATUS_OK) return status;
#ifdef VIGIL_HAS_STDLIB_FS
    status = vigil_native_registry_add(registry, &vigil_stdlib_fs, error);
    if (status != VIGIL_STATUS_OK) return status;
#endif
#ifdef VIGIL_HAS_STDLIB_HTTP
    status = vigil_native_registry_add(registry, &vigil_stdlib_http, error);
    if (status != VIGIL_STATUS_OK) return status;
#endif
    status = vigil_native_registry_add(registry, &vigil_stdlib_log, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_math, error);
    if (status != VIGIL_STATUS_OK) return status;
#ifdef VIGIL_HAS_STDLIB_NET
    status = vigil_native_registry_add(registry, &vigil_stdlib_net, error);
    if (status != VIGIL_STATUS_OK) return status;
#endif
    status = vigil_native_registry_add(registry, &vigil_stdlib_parse, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_random, error);
    if (status != VIGIL_STATUS_OK) return status;
#ifdef VIGIL_HAS_STDLIB_READLINE
    status = vigil_native_registry_add(registry, &vigil_stdlib_readline, error);
    if (status != VIGIL_STATUS_OK) return status;
#endif
    status = vigil_native_registry_add(registry, &vigil_stdlib_regex, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_test, error);
    if (status != VIGIL_STATUS_OK) return status;
#ifdef VIGIL_HAS_STDLIB_THREAD
    status = vigil_native_registry_add(registry, &vigil_stdlib_thread, error);
    if (status != VIGIL_STATUS_OK) return status;
#endif
#ifdef VIGIL_HAS_STDLIB_TIME
    status = vigil_native_registry_add(registry, &vigil_stdlib_time, error);
    if (status != VIGIL_STATUS_OK) return status;
#endif
    status = vigil_native_registry_add(registry, &vigil_stdlib_unsafe, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_url, error);
    if (status != VIGIL_STATUS_OK) return status;
    return vigil_native_registry_add(registry, &vigil_stdlib_yaml, error);
}

static inline int vigil_stdlib_name_equals(
    const char *name,
    size_t name_length,
    const char *literal,
    size_t literal_length
) {
    return name_length == literal_length &&
           memcmp(name, literal, literal_length) == 0;
}

/* Check if an import name refers to any known stdlib module. */
static inline int vigil_stdlib_is_known_module(
    const char *name,
    size_t name_length
) {
    return vigil_stdlib_name_equals(name, name_length, "args", 4U) ||
           vigil_stdlib_name_equals(name, name_length, "atomic", 6U) ||
           vigil_stdlib_name_equals(name, name_length, "compress", 8U) ||
           vigil_stdlib_name_equals(name, name_length, "crypto", 6U) ||
           vigil_stdlib_name_equals(name, name_length, "csv", 3U) ||
           vigil_stdlib_name_equals(name, name_length, "ffi", 3U) ||
           vigil_stdlib_name_equals(name, name_length, "fmt", 3U) ||
           vigil_stdlib_name_equals(name, name_length, "fs", 2U) ||
           vigil_stdlib_name_equals(name, name_length, "http", 4U) ||
           vigil_stdlib_name_equals(name, name_length, "log", 3U) ||
           vigil_stdlib_name_equals(name, name_length, "math", 4U) ||
           vigil_stdlib_name_equals(name, name_length, "net", 3U) ||
           vigil_stdlib_name_equals(name, name_length, "parse", 5U) ||
           vigil_stdlib_name_equals(name, name_length, "random", 6U) ||
           vigil_stdlib_name_equals(name, name_length, "readline", 8U) ||
           vigil_stdlib_name_equals(name, name_length, "regex", 5U) ||
           vigil_stdlib_name_equals(name, name_length, "test", 4U) ||
           vigil_stdlib_name_equals(name, name_length, "thread", 6U) ||
           vigil_stdlib_name_equals(name, name_length, "time", 4U) ||
           vigil_stdlib_name_equals(name, name_length, "unsafe", 6U) ||
           vigil_stdlib_name_equals(name, name_length, "url", 3U) ||
           vigil_stdlib_name_equals(name, name_length, "yaml", 4U);
}

/* Check if an import name is a stdlib module available in this build. */
static inline int vigil_stdlib_is_available_module(
    const char *name,
    size_t name_length
) {
    if (vigil_stdlib_name_equals(name, name_length, "args", 4U) ||
        vigil_stdlib_name_equals(name, name_length, "atomic", 6U) ||
        vigil_stdlib_name_equals(name, name_length, "compress", 8U) ||
        vigil_stdlib_name_equals(name, name_length, "crypto", 6U) ||
        vigil_stdlib_name_equals(name, name_length, "csv", 3U) ||
        vigil_stdlib_name_equals(name, name_length, "fmt", 3U) ||
        vigil_stdlib_name_equals(name, name_length, "log", 3U) ||
        vigil_stdlib_name_equals(name, name_length, "math", 4U) ||
        vigil_stdlib_name_equals(name, name_length, "parse", 5U) ||
        vigil_stdlib_name_equals(name, name_length, "random", 6U) ||
        vigil_stdlib_name_equals(name, name_length, "regex", 5U) ||
        vigil_stdlib_name_equals(name, name_length, "test", 4U) ||
        vigil_stdlib_name_equals(name, name_length, "unsafe", 6U) ||
        vigil_stdlib_name_equals(name, name_length, "url", 3U) ||
        vigil_stdlib_name_equals(name, name_length, "yaml", 4U)) {
        return 1;
    }
#ifdef VIGIL_HAS_STDLIB_FFI
    if (vigil_stdlib_name_equals(name, name_length, "ffi", 3U)) return 1;
#endif
#ifdef VIGIL_HAS_STDLIB_FS
    if (vigil_stdlib_name_equals(name, name_length, "fs", 2U)) return 1;
#endif
#ifdef VIGIL_HAS_STDLIB_HTTP
    if (vigil_stdlib_name_equals(name, name_length, "http", 4U)) return 1;
#endif
#ifdef VIGIL_HAS_STDLIB_NET
    if (vigil_stdlib_name_equals(name, name_length, "net", 3U)) return 1;
#endif
#ifdef VIGIL_HAS_STDLIB_READLINE
    if (vigil_stdlib_name_equals(name, name_length, "readline", 8U)) return 1;
#endif
#ifdef VIGIL_HAS_STDLIB_THREAD
    if (vigil_stdlib_name_equals(name, name_length, "thread", 6U)) return 1;
#endif
#ifdef VIGIL_HAS_STDLIB_TIME
    if (vigil_stdlib_name_equals(name, name_length, "time", 4U)) return 1;
#endif
    return 0;
}

/* Backward-compatible alias for existing callers. */
static inline int vigil_stdlib_is_native_module(
    const char *name,
    size_t name_length
) {
    return vigil_stdlib_is_available_module(name, name_length);
}

#ifdef __cplusplus
}
#endif

#endif
