#include <stddef.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/source.h"

static basl_status_t basl_source_registry_validate_mutable(
    const basl_source_registry_t *registry,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (registry == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "source registry must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (registry->runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "source registry runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (registry->files == NULL && (registry->count != 0U || registry->capacity != 0U)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "source registry state is inconsistent"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (registry->count > registry->capacity) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "source registry count exceeds capacity"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_source_registry_grow(
    basl_source_registry_t *registry,
    size_t minimum_capacity,
    basl_error_t *error
) {
    size_t old_capacity;
    size_t capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= registry->capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    old_capacity = registry->capacity;
    capacity = old_capacity == 0U ? 4U : old_capacity;
    next_capacity = capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > (SIZE_MAX / 2U)) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*registry->files))) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "source registry allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (registry->files == NULL) {
        memory = NULL;
        status = basl_runtime_alloc(
            registry->runtime,
            next_capacity * sizeof(*registry->files),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        registry->files = (basl_source_file_t *)memory;
        registry->capacity = next_capacity;
        return BASL_STATUS_OK;
    }

    memory = registry->files;
    status = basl_runtime_realloc(
        registry->runtime,
        &memory,
        next_capacity * sizeof(*registry->files),
        error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    registry->files = (basl_source_file_t *)memory;
    memset(
        registry->files + old_capacity,
        0,
        (next_capacity - old_capacity) * sizeof(*registry->files)
    );
    registry->capacity = next_capacity;
    return BASL_STATUS_OK;
}

void basl_source_span_clear(basl_source_span_t *span) {
    if (span == NULL) {
        return;
    }

    memset(span, 0, sizeof(*span));
}

void basl_source_registry_init(
    basl_source_registry_t *registry,
    basl_runtime_t *runtime
) {
    if (registry == NULL) {
        return;
    }

    memset(registry, 0, sizeof(*registry));
    registry->runtime = runtime;
}

void basl_source_registry_free(basl_source_registry_t *registry) {
    size_t i;
    void *memory;

    if (registry == NULL) {
        return;
    }

    for (i = 0U; i < registry->count; ++i) {
        basl_string_free(&registry->files[i].path);
        basl_string_free(&registry->files[i].text);
    }

    memory = registry->files;
    if (registry->runtime != NULL) {
        basl_runtime_free(registry->runtime, &memory);
    }

    memset(registry, 0, sizeof(*registry));
}

size_t basl_source_registry_count(const basl_source_registry_t *registry) {
    if (registry == NULL) {
        return 0U;
    }

    return registry->count;
}

const basl_source_file_t *basl_source_registry_get(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id
) {
    if (registry == NULL || source_id == 0U) {
        return NULL;
    }

    if ((size_t)source_id > registry->count) {
        return NULL;
    }

    return &registry->files[source_id - 1U];
}

basl_status_t basl_source_registry_register(
    basl_source_registry_t *registry,
    const char *path,
    size_t path_length,
    const char *text,
    size_t text_length,
    basl_source_id_t *out_source_id,
    basl_error_t *error
) {
    basl_status_t status;
    basl_source_file_t *file;

    status = basl_source_registry_validate_mutable(registry, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (path == NULL || text == NULL || out_source_id == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "source register requires path, text, and out_source_id"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (registry->count == UINT32_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "source registry exhausted source ids"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (registry->count == SIZE_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "source registry capacity overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_source_registry_grow(registry, registry->count + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    file = &registry->files[registry->count];
    basl_string_init(&file->path, registry->runtime);
    basl_string_init(&file->text, registry->runtime);

    status = basl_string_assign(&file->path, path, path_length, error);
    if (status != BASL_STATUS_OK) {
        basl_string_free(&file->path);
        basl_string_free(&file->text);
        return status;
    }

    status = basl_string_assign(&file->text, text, text_length, error);
    if (status != BASL_STATUS_OK) {
        basl_string_free(&file->path);
        basl_string_free(&file->text);
        return status;
    }

    file->id = (basl_source_id_t)(registry->count + 1U);
    registry->count += 1U;
    *out_source_id = file->id;
    return BASL_STATUS_OK;
}

basl_status_t basl_source_registry_register_cstr(
    basl_source_registry_t *registry,
    const char *path,
    const char *text,
    basl_source_id_t *out_source_id,
    basl_error_t *error
) {
    if (path == NULL || text == NULL || out_source_id == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "source register requires path, text, and out_source_id"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return basl_source_registry_register(
        registry,
        path,
        strlen(path),
        text,
        strlen(text),
        out_source_id,
        error
    );
}
