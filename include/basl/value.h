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
    BASL_OBJECT_STRING = 1,
    BASL_OBJECT_FUNCTION = 2,
    BASL_OBJECT_CLOSURE = 3,
    BASL_OBJECT_INSTANCE = 4,
    BASL_OBJECT_ERROR = 5,
    BASL_OBJECT_ARRAY = 6,
    BASL_OBJECT_MAP = 7
} basl_object_type_t;

typedef struct basl_object basl_object_t;
typedef struct basl_chunk basl_chunk_t;

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

BASL_API basl_status_t basl_error_object_new(
    basl_runtime_t *runtime,
    const char *message,
    size_t length,
    int64_t kind,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API basl_status_t basl_error_object_new_cstr(
    basl_runtime_t *runtime,
    const char *message,
    int64_t kind,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API const char *basl_error_object_message(const basl_object_t *object);
BASL_API size_t basl_error_object_message_length(const basl_object_t *object);
BASL_API int64_t basl_error_object_kind(const basl_object_t *object);

BASL_API basl_status_t basl_function_object_new(
    basl_runtime_t *runtime,
    const char *name,
    size_t name_length,
    size_t arity,
    size_t return_count,
    basl_chunk_t *chunk,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API basl_status_t basl_function_object_new_cstr(
    basl_runtime_t *runtime,
    const char *name,
    size_t arity,
    size_t return_count,
    basl_chunk_t *chunk,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API const char *basl_function_object_name(const basl_object_t *object);
BASL_API size_t basl_function_object_arity(const basl_object_t *object);
BASL_API size_t basl_function_object_return_count(const basl_object_t *object);
BASL_API const basl_chunk_t *basl_function_object_chunk(const basl_object_t *object);

BASL_API basl_status_t basl_closure_object_new(
    basl_runtime_t *runtime,
    basl_object_t *function,
    const basl_value_t *captures,
    size_t capture_count,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API const basl_object_t *basl_closure_object_function(const basl_object_t *object);
BASL_API size_t basl_closure_object_capture_count(const basl_object_t *object);
BASL_API int basl_closure_object_get_capture(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_value
);
BASL_API basl_status_t basl_closure_object_set_capture(
    basl_object_t *object,
    size_t index,
    const basl_value_t *value,
    basl_error_t *error
);

BASL_API basl_status_t basl_instance_object_new(
    basl_runtime_t *runtime,
    size_t class_index,
    const basl_value_t *fields,
    size_t field_count,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API size_t basl_instance_object_class_index(const basl_object_t *object);
BASL_API size_t basl_instance_object_field_count(const basl_object_t *object);
BASL_API int basl_instance_object_get_field(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_value
);
BASL_API basl_status_t basl_instance_object_set_field(
    basl_object_t *object,
    size_t index,
    const basl_value_t *value,
    basl_error_t *error
);

BASL_API basl_status_t basl_array_object_new(
    basl_runtime_t *runtime,
    const basl_value_t *items,
    size_t item_count,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API size_t basl_array_object_length(const basl_object_t *object);
BASL_API int basl_array_object_get(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_value
);
BASL_API basl_status_t basl_array_object_set(
    basl_object_t *object,
    size_t index,
    const basl_value_t *value,
    basl_error_t *error
);

BASL_API basl_status_t basl_map_object_new(
    basl_runtime_t *runtime,
    basl_object_t **out_object,
    basl_error_t *error
);
BASL_API size_t basl_map_object_count(const basl_object_t *object);
BASL_API int basl_map_object_get(
    const basl_object_t *object,
    const basl_value_t *key,
    basl_value_t *out_value
);
BASL_API int basl_map_object_key_at(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_key
);
BASL_API int basl_map_object_value_at(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_value
);
BASL_API basl_status_t basl_map_object_set(
    basl_object_t *object,
    const basl_value_t *key,
    const basl_value_t *value,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
