#ifndef BASL_STRING_H
#define BASL_STRING_H

#include <stddef.h>

#include "basl/array.h"
#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct basl_string {
    basl_byte_buffer_t bytes;
} basl_string_t;

BASL_API void basl_string_init(
    basl_string_t *string,
    basl_runtime_t *runtime
);
BASL_API void basl_string_clear(basl_string_t *string);
BASL_API void basl_string_free(basl_string_t *string);
BASL_API size_t basl_string_length(const basl_string_t *string);
BASL_API const char *basl_string_c_str(const basl_string_t *string);
BASL_API basl_status_t basl_string_reserve(
    basl_string_t *string,
    size_t length,
    basl_error_t *error
);
BASL_API basl_status_t basl_string_assign(
    basl_string_t *string,
    const char *value,
    size_t length,
    basl_error_t *error
);
BASL_API basl_status_t basl_string_assign_cstr(
    basl_string_t *string,
    const char *value,
    basl_error_t *error
);
BASL_API basl_status_t basl_string_append(
    basl_string_t *string,
    const char *value,
    size_t length,
    basl_error_t *error
);
BASL_API basl_status_t basl_string_append_cstr(
    basl_string_t *string,
    const char *value,
    basl_error_t *error
);
BASL_API int basl_string_compare(
    const basl_string_t *left,
    const basl_string_t *right
);
BASL_API int basl_string_equals_cstr(
    const basl_string_t *string,
    const char *value
);

#ifdef __cplusplus
}
#endif

#endif
