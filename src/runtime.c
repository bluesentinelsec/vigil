#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "basl/basl.h"

struct basl_runtime {
    basl_allocator_t allocator;
};

static void *basl_default_allocate(void *user_data, size_t size) {
    (void)user_data;
    return malloc(size);
}

static void *basl_default_reallocate(void *user_data, void *memory, size_t size) {
    (void)user_data;
    return realloc(memory, size);
}

static void basl_default_deallocate(void *user_data, void *memory) {
    (void)user_data;
    free(memory);
}

static basl_allocator_t basl_default_allocator(void) {
    basl_allocator_t allocator;

    allocator.user_data = NULL;
    allocator.allocate = basl_default_allocate;
    allocator.reallocate = basl_default_reallocate;
    allocator.deallocate = basl_default_deallocate;
    return allocator;
}

static basl_allocator_t basl_resolve_allocator(
    const basl_runtime_options_t *options
) {
    basl_allocator_t allocator;

    allocator = basl_default_allocator();
    if (options != NULL && options->allocator != NULL) {
        allocator = *options->allocator;
    }

    return allocator;
}

static int basl_allocator_is_valid(const basl_allocator_t *allocator) {
    if (allocator == NULL) {
        return 0;
    }

    return allocator->allocate != NULL && allocator->deallocate != NULL;
}

static void basl_error_set_literal(
    basl_error_t *error,
    basl_status_t type,
    const char *value
) {
    if (error == NULL) {
        return;
    }

    error->type = type;
    error->value = value;
    error->length = value == NULL ? 0U : strlen(value);
    basl_source_location_clear(&error->location);
}

void basl_source_location_clear(basl_source_location_t *location) {
    if (location == NULL) {
        return;
    }

    location->source_id = 0U;
    location->line = 0U;
    location->column = 0U;
}

void basl_error_clear(basl_error_t *error) {
    if (error == NULL) {
        return;
    }

    error->type = BASL_STATUS_OK;
    error->value = NULL;
    error->length = 0U;
    basl_source_location_clear(&error->location);
}

const char *basl_status_name(basl_status_t status) {
    switch (status) {
        case BASL_STATUS_OK:
            return "ok";
        case BASL_STATUS_INVALID_ARGUMENT:
            return "invalid_argument";
        case BASL_STATUS_OUT_OF_MEMORY:
            return "out_of_memory";
        case BASL_STATUS_INTERNAL:
            return "internal";
        default:
            return "unknown";
    }
}

void basl_runtime_options_init(basl_runtime_options_t *options) {
    if (options == NULL) {
        return;
    }

    memset(options, 0, sizeof(*options));
}

basl_status_t basl_runtime_open(
    basl_runtime_t **out_runtime,
    const basl_runtime_options_t *options,
    basl_error_t *error
) {
    basl_allocator_t allocator;
    basl_runtime_t *runtime;

    basl_error_clear(error);

    if (out_runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "out_runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_runtime = NULL;

    allocator = basl_resolve_allocator(options);

    if (!basl_allocator_is_valid(&allocator)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "allocator must define allocate and deallocate"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    runtime = (basl_runtime_t *)allocator.allocate(allocator.user_data, sizeof(*runtime));
    if (runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "failed to allocate runtime"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    runtime->allocator = allocator;
    *out_runtime = runtime;
    return BASL_STATUS_OK;
}

void basl_runtime_close(basl_runtime_t *runtime) {
    if (runtime == NULL) {
        return;
    }

    runtime->allocator.deallocate(runtime->allocator.user_data, runtime);
}

const basl_allocator_t *basl_runtime_allocator(const basl_runtime_t *runtime) {
    if (runtime == NULL) {
        return NULL;
    }

    return &runtime->allocator;
}
