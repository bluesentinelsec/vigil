#include <stddef.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/array.h"

static basl_status_t basl_byte_buffer_validate(
    const basl_byte_buffer_t *buffer,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (buffer == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "byte buffer must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (buffer->runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "byte buffer runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (buffer->data == NULL && (buffer->length != 0U || buffer->capacity != 0U)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "byte buffer state is inconsistent"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (buffer->length > buffer->capacity) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "byte buffer length exceeds capacity"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_byte_buffer_grow(
    basl_byte_buffer_t *buffer,
    size_t minimum_capacity,
    basl_error_t *error
) {
    size_t capacity;
    size_t next_capacity;
    basl_status_t status;
    void *memory;

    if (minimum_capacity <= buffer->capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    capacity = buffer->capacity == 0U ? 16U : buffer->capacity;
    next_capacity = capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > (SIZE_MAX / 2U)) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (buffer->data == NULL) {
        memory = NULL;
        status = basl_runtime_alloc(buffer->runtime, next_capacity, &memory, error);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        buffer->data = (uint8_t *)memory;
        buffer->capacity = next_capacity;
        return BASL_STATUS_OK;
    }

    memory = buffer->data;
    status = basl_runtime_realloc(buffer->runtime, &memory, next_capacity, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    buffer->data = (uint8_t *)memory;
    buffer->capacity = next_capacity;
    return BASL_STATUS_OK;
}

void basl_byte_buffer_init(
    basl_byte_buffer_t *buffer,
    basl_runtime_t *runtime
) {
    if (buffer == NULL) {
        return;
    }

    memset(buffer, 0, sizeof(*buffer));
    buffer->runtime = runtime;
}

void basl_byte_buffer_clear(basl_byte_buffer_t *buffer) {
    if (buffer == NULL) {
        return;
    }

    buffer->length = 0U;
}

void basl_byte_buffer_free(basl_byte_buffer_t *buffer) {
    void *memory;

    if (buffer == NULL) {
        return;
    }

    if (buffer->runtime != NULL) {
        memory = buffer->data;
        basl_runtime_free(buffer->runtime, &memory);
        buffer->data = NULL;
    } else {
        buffer->data = NULL;
    }

    buffer->length = 0U;
    buffer->capacity = 0U;
    buffer->runtime = NULL;
}

basl_status_t basl_byte_buffer_reserve(
    basl_byte_buffer_t *buffer,
    size_t capacity,
    basl_error_t *error
) {
    basl_status_t status;

    status = basl_byte_buffer_validate(buffer, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_byte_buffer_grow(buffer, capacity, error);
}

basl_status_t basl_byte_buffer_resize(
    basl_byte_buffer_t *buffer,
    size_t length,
    basl_error_t *error
) {
    basl_status_t status;
    size_t old_length;

    status = basl_byte_buffer_validate(buffer, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    old_length = buffer->length;
    if (length > buffer->capacity) {
        status = basl_byte_buffer_grow(buffer, length, error);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        basl_error_clear(error);
    }

    if (length > old_length) {
        memset(buffer->data + old_length, 0, length - old_length);
    }

    buffer->length = length;
    return BASL_STATUS_OK;
}

basl_status_t basl_byte_buffer_append(
    basl_byte_buffer_t *buffer,
    const void *data,
    size_t length,
    basl_error_t *error
) {
    basl_status_t status;
    size_t old_length;

    status = basl_byte_buffer_validate(buffer, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (data == NULL && length != 0U) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "append data must not be null when length is non-zero"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (length > SIZE_MAX - buffer->length) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "byte buffer append would overflow"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    old_length = buffer->length;
    status = basl_byte_buffer_grow(buffer, old_length + length, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (length != 0U) {
        memcpy(buffer->data + old_length, data, length);
    }

    buffer->length = old_length + length;
    return BASL_STATUS_OK;
}

basl_status_t basl_byte_buffer_append_byte(
    basl_byte_buffer_t *buffer,
    uint8_t value,
    basl_error_t *error
) {
    return basl_byte_buffer_append(buffer, &value, 1U, error);
}
