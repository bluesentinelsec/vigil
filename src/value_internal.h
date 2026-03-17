/*
 * Internal header for VM-level access to function object fields.
 *
 * This exposes the basl_object and basl_function_object_t layouts so
 * the VM dispatch loop can inline sibling/chunk lookups instead of
 * calling through the public API.  Only vm.c should include this.
 */
#ifndef BASL_VALUE_INTERNAL_H
#define BASL_VALUE_INTERNAL_H

#include "basl/value.h"
#include "basl/chunk.h"
#include "basl/string.h"

/* Mirror of the opaque basl_object struct from value.c. */
struct basl_object_internal {
    basl_runtime_t *runtime;
    basl_object_type_t type;
    volatile int64_t ref_count;  /* Atomic for thread safety */
};

struct basl_function_object_internal {
    struct basl_object_internal base;
    basl_string_t name;
    size_t arity;
    size_t return_count;
    basl_chunk_t chunk;
    basl_object_t **functions;
    size_t function_count;
};

/*
 * Inline sibling lookup — equivalent to basl_function_object_sibling().
 */
static inline const basl_object_t *basl_vm_function_sibling(
    const basl_object_t *function, size_t index
) {
    const struct basl_function_object_internal *fo =
        (const struct basl_function_object_internal *)function;
    if (fo->functions == NULL || index >= fo->function_count) {
        return NULL;
    }
    return fo->functions[index];
}

/*
 * Inline chunk accessor — equivalent to basl_function_object_chunk().
 */
static inline const basl_chunk_t *basl_vm_function_chunk(
    const basl_object_t *function
) {
    const struct basl_function_object_internal *fo =
        (const struct basl_function_object_internal *)function;
    return &fo->chunk;
}

#endif /* BASL_VALUE_INTERNAL_H */
