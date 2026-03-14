#ifndef BASL_STDLIB_H
#define BASL_STDLIB_H

#include <string.h>

#include "basl/native_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Each stdlib module exports a const descriptor. */
extern BASL_API const basl_native_module_t basl_stdlib_fmt;

/* Register all built-in stdlib modules into a native registry. */
static inline basl_status_t basl_stdlib_register_all(
    basl_native_registry_t *registry,
    basl_error_t *error
) {
    return basl_native_registry_add(registry, &basl_stdlib_fmt, error);
}

/* Check if an import name is a native stdlib module. */
static inline int basl_stdlib_is_native_module(
    const char *name,
    size_t name_length
) {
    return name_length == 3U && memcmp(name, "fmt", 3U) == 0;
}

#ifdef __cplusplus
}
#endif

#endif
