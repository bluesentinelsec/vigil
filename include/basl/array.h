#ifndef BASL_ARRAY_H
#define BASL_ARRAY_H

#include <stddef.h>
#include <stdint.h>

#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct basl_byte_buffer {
    basl_runtime_t *runtime;
    uint8_t *data;
    size_t length;
    size_t capacity;
} basl_byte_buffer_t;

BASL_API void basl_byte_buffer_init(
    basl_byte_buffer_t *buffer,
    basl_runtime_t *runtime
);
BASL_API void basl_byte_buffer_clear(basl_byte_buffer_t *buffer);
BASL_API void basl_byte_buffer_free(basl_byte_buffer_t *buffer);
BASL_API basl_status_t basl_byte_buffer_reserve(
    basl_byte_buffer_t *buffer,
    size_t capacity,
    basl_error_t *error
);
BASL_API basl_status_t basl_byte_buffer_resize(
    basl_byte_buffer_t *buffer,
    size_t length,
    basl_error_t *error
);
BASL_API basl_status_t basl_byte_buffer_append(
    basl_byte_buffer_t *buffer,
    const void *data,
    size_t length,
    basl_error_t *error
);
BASL_API basl_status_t basl_byte_buffer_append_byte(
    basl_byte_buffer_t *buffer,
    uint8_t value,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
