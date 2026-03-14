#ifndef BASL_NATIVE_MODULE_H
#define BASL_NATIVE_MODULE_H

#include <stddef.h>

#include "basl/diagnostic.h"
#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/value.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Describes one function exported by a native module.
 * The compiler uses name, param_types, param_count, and return_type
 * for type checking.  The VM uses native_fn at runtime.
 */
typedef struct basl_native_module_function {
    const char *name;
    size_t name_length;
    basl_native_fn_t native_fn;
    size_t param_count;
    const int *param_types;     /* basl_type_kind_t values */
    int return_type;            /* basl_type_kind_t */
    size_t return_count;        /* number of return values (1 or 2 for err) */
    const int *return_types;    /* array of basl_type_kind_t, length = return_count */
} basl_native_module_function_t;

/**
 * Describes a native module (e.g. "fmt", "math").
 */
typedef struct basl_native_module {
    const char *name;
    size_t name_length;
    const basl_native_module_function_t *functions;
    size_t function_count;
} basl_native_module_t;

/**
 * Registry of native modules, shared between compiler and VM.
 */
typedef struct basl_native_registry {
    const basl_native_module_t **modules;
    size_t module_count;
    size_t module_capacity;
} basl_native_registry_t;

BASL_API void basl_native_registry_init(basl_native_registry_t *registry);
BASL_API void basl_native_registry_free(basl_native_registry_t *registry);
BASL_API basl_status_t basl_native_registry_add(
    basl_native_registry_t *registry,
    const basl_native_module_t *module,
    basl_error_t *error
);
BASL_API const basl_native_module_t *basl_native_registry_find(
    const basl_native_registry_t *registry,
    const char *name,
    size_t name_length
);

/**
 * Extended compile API that accepts a native module registry.
 */
BASL_API basl_status_t basl_compile_source_with_natives(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    const basl_native_registry_t *natives,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
