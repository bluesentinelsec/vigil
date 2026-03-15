#ifndef BASL_JSON_H
#define BASL_JSON_H

#include <stddef.h>
#include <stdint.h>

#include "basl/allocator.h"
#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Value types ─────────────────────────────────────────────────── */

typedef enum basl_json_type {
    BASL_JSON_NULL = 0,
    BASL_JSON_BOOL = 1,
    BASL_JSON_NUMBER = 2,
    BASL_JSON_STRING = 3,
    BASL_JSON_ARRAY = 4,
    BASL_JSON_OBJECT = 5
} basl_json_type_t;

typedef struct basl_json_value basl_json_value_t;

/* ── Lifecycle ───────────────────────────────────────────────────── */

/*
 * Create a JSON value.  If allocator is NULL, malloc/realloc/free are used.
 * The allocator pointer is stored and must remain valid for the value's
 * lifetime.  All child values share the root's allocator.
 */
BASL_API basl_status_t basl_json_null_new(
    const basl_allocator_t *allocator,
    basl_json_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_json_bool_new(
    const basl_allocator_t *allocator,
    int value,
    basl_json_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_json_number_new(
    const basl_allocator_t *allocator,
    double value,
    basl_json_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_json_string_new(
    const basl_allocator_t *allocator,
    const char *value,
    size_t length,
    basl_json_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_json_array_new(
    const basl_allocator_t *allocator,
    basl_json_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_json_object_new(
    const basl_allocator_t *allocator,
    basl_json_value_t **out,
    basl_error_t *error
);

/* Recursively frees the value and all children. */
BASL_API void basl_json_free(basl_json_value_t **value);

/* ── Type inspection ─────────────────────────────────────────────── */

BASL_API basl_json_type_t basl_json_type(const basl_json_value_t *value);

/* ── Scalar accessors ────────────────────────────────────────────── */

BASL_API int basl_json_bool_value(const basl_json_value_t *value);
BASL_API double basl_json_number_value(const basl_json_value_t *value);
BASL_API const char *basl_json_string_value(const basl_json_value_t *value);
BASL_API size_t basl_json_string_length(const basl_json_value_t *value);

/* ── Array operations ────────────────────────────────────────────── */

BASL_API size_t basl_json_array_count(const basl_json_value_t *array);

BASL_API const basl_json_value_t *basl_json_array_get(
    const basl_json_value_t *array,
    size_t index
);

/* Takes ownership of element. */
BASL_API basl_status_t basl_json_array_push(
    basl_json_value_t *array,
    basl_json_value_t *element,
    basl_error_t *error
);

/* ── Object operations ───────────────────────────────────────────── */

BASL_API size_t basl_json_object_count(const basl_json_value_t *object);

/* Returns NULL if key not found. */
BASL_API const basl_json_value_t *basl_json_object_get(
    const basl_json_value_t *object,
    const char *key
);

/* Takes ownership of value.  Replaces existing key if present. */
BASL_API basl_status_t basl_json_object_set(
    basl_json_value_t *object,
    const char *key,
    size_t key_length,
    basl_json_value_t *value,
    basl_error_t *error
);

/* Iteration: returns key/value at position index (insertion order). */
BASL_API basl_status_t basl_json_object_entry(
    const basl_json_value_t *object,
    size_t index,
    const char **out_key,
    size_t *out_key_length,
    const basl_json_value_t **out_value
);

/* ── Parser ──────────────────────────────────────────────────────── */

BASL_API basl_status_t basl_json_parse(
    const basl_allocator_t *allocator,
    const char *input,
    size_t length,
    basl_json_value_t **out,
    basl_error_t *error
);

/* ── Emitter ─────────────────────────────────────────────────────── */

/*
 * Serialize a JSON value to a string.  The caller must free *out_string
 * via the allocator (or free() if allocator was NULL).
 */
BASL_API basl_status_t basl_json_emit(
    const basl_json_value_t *value,
    char **out_string,
    size_t *out_length,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
