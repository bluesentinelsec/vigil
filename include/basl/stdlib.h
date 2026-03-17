#ifndef BASL_STDLIB_H
#define BASL_STDLIB_H

#include <string.h>

#include "basl/native_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Each stdlib module exports a const descriptor. */
extern BASL_API const basl_native_module_t basl_stdlib_args;
extern BASL_API const basl_native_module_t basl_stdlib_ffi;
extern BASL_API const basl_native_module_t basl_stdlib_fmt;
extern BASL_API const basl_native_module_t basl_stdlib_fs;
extern BASL_API const basl_native_module_t basl_stdlib_math;
extern BASL_API const basl_native_module_t basl_stdlib_random;
extern BASL_API const basl_native_module_t basl_stdlib_readline;
extern BASL_API const basl_native_module_t basl_stdlib_regex;
extern BASL_API const basl_native_module_t basl_stdlib_test;
extern BASL_API const basl_native_module_t basl_stdlib_unsafe;
extern BASL_API const basl_native_module_t basl_stdlib_url;
extern BASL_API const basl_native_module_t basl_stdlib_yaml;

/* Register all built-in stdlib modules into a native registry. */
static inline basl_status_t basl_stdlib_register_all(
    basl_native_registry_t *registry,
    basl_error_t *error
) {
    basl_status_t status;
    status = basl_native_registry_add(registry, &basl_stdlib_args, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_ffi, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_fmt, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_fs, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_math, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_random, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_readline, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_regex, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_test, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_unsafe, error);
    if (status != BASL_STATUS_OK) return status;
    status = basl_native_registry_add(registry, &basl_stdlib_url, error);
    if (status != BASL_STATUS_OK) return status;
    return basl_native_registry_add(registry, &basl_stdlib_yaml, error);
}

/* Check if an import name is a native stdlib module. */
static inline int basl_stdlib_is_native_module(
    const char *name,
    size_t name_length
) {
    return (name_length == 4U && memcmp(name, "args", 4U) == 0) ||
           (name_length == 3U && memcmp(name, "ffi", 3U) == 0) ||
           (name_length == 3U && memcmp(name, "fmt", 3U) == 0) ||
           (name_length == 2U && memcmp(name, "fs", 2U) == 0) ||
           (name_length == 4U && memcmp(name, "math", 4U) == 0) ||
           (name_length == 6U && memcmp(name, "random", 6U) == 0) ||
           (name_length == 8U && memcmp(name, "readline", 8U) == 0) ||
           (name_length == 5U && memcmp(name, "regex", 5U) == 0) ||
           (name_length == 4U && memcmp(name, "test", 4U) == 0) ||
           (name_length == 6U && memcmp(name, "unsafe", 6U) == 0) ||
           (name_length == 3U && memcmp(name, "url", 3U) == 0) ||
           (name_length == 4U && memcmp(name, "yaml", 4U) == 0);
}

#ifdef __cplusplus
}
#endif

#endif
