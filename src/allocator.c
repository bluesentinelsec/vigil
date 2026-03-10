#include <stdlib.h>
#include <string.h>

#include "internal/basl_internal.h"

static void *basl_default_allocate(void *user_data, size_t size) {
    (void)user_data;
    return calloc(1U, size);
}

static void *basl_default_reallocate(void *user_data, void *memory, size_t size) {
    (void)user_data;
    return realloc(memory, size);
}

static void basl_default_deallocate(void *user_data, void *memory) {
    (void)user_data;
    free(memory);
}

basl_allocator_t basl_default_allocator(void) {
    basl_allocator_t allocator;

    allocator.user_data = NULL;
    allocator.allocate = basl_default_allocate;
    allocator.reallocate = basl_default_reallocate;
    allocator.deallocate = basl_default_deallocate;
    return allocator;
}

int basl_allocator_is_valid(const basl_allocator_t *allocator) {
    if (allocator == NULL) {
        return 0;
    }

    return allocator->allocate != NULL && allocator->deallocate != NULL;
}

basl_status_t basl_runtime_alloc(
    basl_runtime_t *runtime,
    size_t size,
    void **out_memory,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (runtime == NULL || out_memory == NULL || size == 0U) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "runtime_alloc requires runtime, out_memory, and non-zero size"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_memory = runtime->allocator.allocate(runtime->allocator.user_data, size);
    if (*out_memory == NULL) {
        basl_error_set_literal(error, BASL_STATUS_OUT_OF_MEMORY, "allocation failed");
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memset(*out_memory, 0, size);
    return BASL_STATUS_OK;
}

basl_status_t basl_runtime_realloc(
    basl_runtime_t *runtime,
    void **memory,
    size_t size,
    basl_error_t *error
) {
    void *reallocated;

    basl_error_clear(error);

    if (runtime == NULL || memory == NULL || *memory == NULL || size == 0U) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "runtime_realloc requires runtime, memory, and non-zero size"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (runtime->allocator.reallocate == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_UNSUPPORTED,
            "allocator does not support reallocate"
        );
        return BASL_STATUS_UNSUPPORTED;
    }

    reallocated = runtime->allocator.reallocate(
        runtime->allocator.user_data,
        *memory,
        size
    );
    if (reallocated == NULL) {
        basl_error_set_literal(error, BASL_STATUS_OUT_OF_MEMORY, "reallocation failed");
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    *memory = reallocated;
    return BASL_STATUS_OK;
}

void basl_runtime_free(basl_runtime_t *runtime, void **memory) {
    if (runtime == NULL || memory == NULL || *memory == NULL) {
        return;
    }

    runtime->allocator.deallocate(runtime->allocator.user_data, *memory);
    *memory = NULL;
}
