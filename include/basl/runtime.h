#ifndef BASL_RUNTIME_H
#define BASL_RUNTIME_H

#include <stddef.h>

#include "basl/allocator.h"
#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct basl_runtime_options {
    const basl_allocator_t *allocator;
} basl_runtime_options_t;

typedef struct basl_runtime basl_runtime_t;

BASL_API void basl_runtime_options_init(basl_runtime_options_t *options);
BASL_API basl_status_t basl_runtime_open(
    basl_runtime_t **out_runtime,
    const basl_runtime_options_t *options,
    basl_error_t *error
);
BASL_API void basl_runtime_close(basl_runtime_t **runtime);
BASL_API const basl_allocator_t *basl_runtime_allocator(const basl_runtime_t *runtime);
/* basl_runtime_alloc zero-initializes the returned memory on success. */
BASL_API basl_status_t basl_runtime_alloc(
    basl_runtime_t *runtime,
    size_t size,
    void **out_memory,
    basl_error_t *error
);
/*
 * On basl_runtime_realloc failure, *memory is left unchanged and still points
 * to the original allocation.
 */
BASL_API basl_status_t basl_runtime_realloc(
    basl_runtime_t *runtime,
    void **memory,
    size_t size,
    basl_error_t *error
);
BASL_API void basl_runtime_free(basl_runtime_t *runtime, void **memory);

#ifdef __cplusplus
}
#endif

#endif
