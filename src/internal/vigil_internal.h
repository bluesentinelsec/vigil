#ifndef VIGIL_INTERNAL_H
#define VIGIL_INTERNAL_H

#include <stddef.h>

#include "vigil/runtime.h"
#include "vigil/status.h"
#include "vigil/value.h"

/* ── Regex pattern cache ─────────────────────────────────────────────
 * Fixed-size open-addressing LRU-approximation cache for compiled regex
 * patterns.  Stored inline in vigil_runtime to avoid extra allocation.
 * VIGIL_REGEX_CACHE_SIZE must be a power of two. */
#define VIGIL_REGEX_CACHE_SIZE 32U

struct vigil_regex; /* forward-declared; defined in stdlib/regex_engine.c */

typedef struct vigil_regex_cache_entry
{
    char *pattern; /* heap-allocated copy; NULL = empty slot */
    size_t pattern_len;
    struct vigil_regex *re; /* compiled regex; NULL = empty slot */
    unsigned int lru_clock; /* incremented on each access */
} vigil_regex_cache_entry_t;

typedef struct vigil_regex_cache
{
    vigil_regex_cache_entry_t entries[VIGIL_REGEX_CACHE_SIZE];
    unsigned int clock; /* global access counter */
} vigil_regex_cache_t;

struct vigil_runtime
{
    vigil_allocator_t allocator;
    vigil_logger_t logger;
    vigil_regex_cache_t regex_cache;
    /* Singleton "ok" error object — reused by stdlib functions that return
       (value, err) on the success path to avoid a heap allocation per call. */
    vigil_object_t *ok_error;
};

typedef struct vigil_runtime_interface_impl_init
{
    size_t interface_index;
    const size_t *function_indices;
    size_t function_count;
} vigil_runtime_interface_impl_init_t;

typedef struct vigil_runtime_class_init
{
    const vigil_runtime_interface_impl_init_t *interface_impls;
    size_t interface_impl_count;
} vigil_runtime_class_init_t;

vigil_allocator_t vigil_default_allocator(void);
int vigil_allocator_is_valid(const vigil_allocator_t *allocator);
void vigil_error_set_literal(vigil_error_t *error, vigil_status_t type, const char *value);
vigil_status_t vigil_function_object_attach_siblings(vigil_object_t *owner_function, vigil_object_t **functions,
                                                     size_t function_count, size_t owner_index,
                                                     const vigil_value_t *initial_globals, size_t global_count,
                                                     const vigil_runtime_class_init_t *classes, size_t class_count,
                                                     vigil_error_t *error);
const vigil_object_t *vigil_function_object_sibling(const vigil_object_t *function, size_t index);
const vigil_object_t *vigil_function_object_resolve_interface_method(const vigil_object_t *function, size_t class_index,
                                                                     size_t interface_index, size_t method_index);
int vigil_function_object_get_global(const vigil_object_t *function, size_t index, vigil_value_t *out_value);
vigil_status_t vigil_function_object_set_global(const vigil_object_t *function, size_t index,
                                                const vigil_value_t *value, vigil_error_t *error);
const vigil_object_t *vigil_callable_object_function(const vigil_object_t *callable);
size_t vigil_callable_object_arity(const vigil_object_t *callable);
size_t vigil_callable_object_return_count(const vigil_object_t *callable);
const vigil_chunk_t *vigil_callable_object_chunk(const vigil_object_t *callable);

/* Push the singleton "ok" error value onto the VM stack.
   Avoids allocating a new error object on every stdlib success path. */
vigil_status_t vigil_runtime_push_ok_error(vigil_runtime_t *runtime, vigil_vm_t *vm, vigil_error_t *error);

/* Return the pre-encoded nanbox value for the singleton "ok" error.
   Callers can push this directly with VIGIL_VM_PUSH to skip the
   retain/release overhead of vigil_runtime_push_ok_error(). */
vigil_value_t vigil_runtime_ok_error_value(vigil_runtime_t *runtime);

#endif
