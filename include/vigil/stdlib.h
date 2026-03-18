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
extern VIGIL_API const vigil_native_module_t vigil_stdlib_ffi;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_fmt;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_fs;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_http;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_log;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_math;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_net;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_parse;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_random;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_readline;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_regex;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_test;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_thread;
extern VIGIL_API const vigil_native_module_t vigil_stdlib_time;
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
    status = vigil_native_registry_add(registry, &vigil_stdlib_ffi, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_fmt, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_fs, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_http, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_log, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_math, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_net, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_parse, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_random, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_readline, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_regex, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_test, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_thread, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_time, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_unsafe, error);
    if (status != VIGIL_STATUS_OK) return status;
    status = vigil_native_registry_add(registry, &vigil_stdlib_url, error);
    if (status != VIGIL_STATUS_OK) return status;
    return vigil_native_registry_add(registry, &vigil_stdlib_yaml, error);
}

/* Check if an import name is a native stdlib module. */
static inline int vigil_stdlib_is_native_module(
    const char *name,
    size_t name_length
) {
    return (name_length == 4U && memcmp(name, "args", 4U) == 0) ||
           (name_length == 6U && memcmp(name, "atomic", 6U) == 0) ||
           (name_length == 8U && memcmp(name, "compress", 8U) == 0) ||
           (name_length == 6U && memcmp(name, "crypto", 6U) == 0) ||
           (name_length == 3U && memcmp(name, "csv", 3U) == 0) ||
           (name_length == 3U && memcmp(name, "ffi", 3U) == 0) ||
           (name_length == 3U && memcmp(name, "fmt", 3U) == 0) ||
           (name_length == 2U && memcmp(name, "fs", 2U) == 0) ||
           (name_length == 4U && memcmp(name, "http", 4U) == 0) ||
           (name_length == 3U && memcmp(name, "log", 3U) == 0) ||
           (name_length == 4U && memcmp(name, "math", 4U) == 0) ||
           (name_length == 3U && memcmp(name, "net", 3U) == 0) ||
           (name_length == 5U && memcmp(name, "parse", 5U) == 0) ||
           (name_length == 6U && memcmp(name, "random", 6U) == 0) ||
           (name_length == 8U && memcmp(name, "readline", 8U) == 0) ||
           (name_length == 5U && memcmp(name, "regex", 5U) == 0) ||
           (name_length == 4U && memcmp(name, "test", 4U) == 0) ||
           (name_length == 6U && memcmp(name, "thread", 6U) == 0) ||
           (name_length == 4U && memcmp(name, "time", 4U) == 0) ||
           (name_length == 6U && memcmp(name, "unsafe", 6U) == 0) ||
           (name_length == 3U && memcmp(name, "url", 3U) == 0) ||
           (name_length == 4U && memcmp(name, "yaml", 4U) == 0);
}

#ifdef __cplusplus
}
#endif

#endif
