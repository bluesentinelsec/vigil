#ifndef BASL_VALUE_H
#define BASL_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum basl_value_kind {
    BASL_VALUE_NIL = 0,
    BASL_VALUE_BOOL = 1,
    BASL_VALUE_INT = 2,
    BASL_VALUE_FLOAT = 3,
    BASL_VALUE_OBJECT = 4
} basl_value_kind_t;

typedef enum basl_object_type {
    BASL_OBJECT_INVALID = 0,
    BASL_OBJECT_STRING = 1
} basl_object_type_t;

typedef struct basl_object basl_object_t;

typedef struct basl_value {
    basl_value_kind_t kind;
    union {
        bool boolean;
        int64_t integer;
        double number;
        basl_object_t *object;
    } as;
} basl_value_t;

BASL_API void basl_value_init_nil(basl_value_t *value);
BASL_API void basl_value_init_bool(basl_value_t *value, bool boolean);
BASL_API void basl_value_init_int(basl_value_t *value, int64_t integer);
BASL_API void basl_value_init_float(basl_value_t *value, double number);
/*
 * Transfers one owned object reference into the value and clears *object.
 * Passing a null object pointer initializes the value to nil.
 */
BASL_API void basl_value_init_object(
    basl_value_t *value,
    basl_object_t **object
);
BASL_API basl_value_t basl_value_copy(const basl_value_t *value);
BASL_API void basl_value_release(basl_value_t *value);
BASL_API basl_value_kind_t basl_value_kind(const basl_value_t *value);
BASL_API bool basl_value_as_bool(const basl_value_t *value);
BASL_API int64_t basl_value_as_int(const basl_value_t *value);
BASL_API double basl_value_as_float(const basl_value_t *value);
BASL_API basl_object_t *basl_value_as_object(const basl_value_t *value);

BASL_API basl_object_type_t basl_object_type(const basl_object_t *object);
BASL_API size_t basl_object_ref_count(const basl_object_t *object);
/*
 * Retains one additional reference. Ref-count overflow is treated as an
 * internal fatal error.
 */
BASL_API void basl_object_retain(basl_object_t *object);
/*
 * Consumes the caller's reference and always clears *object. The underlying
 * object is destroyed only when the released reference was the last one.
 */
BASL_API void basl_object_release(basl_object_t **object);

BASL_API basl_status_t basl_string_object_new(
    basl_runtime_t *runtime,
    const char *value,
    size_t length,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API basl_status_t basl_string_object_new_cstr(
    basl_runtime_t *runtime,
    const char *value,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API const char *basl_string_object_c_str(const basl_object_t *object);
BASL_API size_t basl_string_object_length(const basl_object_t *object);

#ifdef __cplusplus
}
#endif

#endif
