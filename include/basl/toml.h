#ifndef BASL_TOML_H
#define BASL_TOML_H

#include <stddef.h>
#include <stdint.h>

#include "basl/allocator.h"
#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Value types ─────────────────────────────────────────────────── */

typedef enum basl_toml_type {
    BASL_TOML_STRING = 0,
    BASL_TOML_INTEGER = 1,
    BASL_TOML_FLOAT = 2,
    BASL_TOML_BOOL = 3,
    BASL_TOML_DATETIME = 4,
    BASL_TOML_ARRAY = 5,
    BASL_TOML_TABLE = 6
} basl_toml_type_t;

/*
 * Date-time representation.
 * Fields are set based on which components are present:
 *   Offset Date-Time:  all fields valid, has_date=has_time=has_offset=1
 *   Local Date-Time:   has_date=has_time=1, has_offset=0
 *   Local Date:        has_date=1, has_time=has_offset=0
 *   Local Time:        has_time=1, has_date=has_offset=0
 */
typedef struct basl_toml_datetime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int nanosecond;
    int offset_minutes;     /* signed: UTC offset in minutes */
    int has_date;
    int has_time;
    int has_offset;
} basl_toml_datetime_t;

typedef struct basl_toml_value basl_toml_value_t;

/* ── Lifecycle ───────────────────────────────────────────────────── */

BASL_API basl_status_t basl_toml_string_new(
    const basl_allocator_t *allocator,
    const char *value,
    size_t length,
    basl_toml_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_toml_integer_new(
    const basl_allocator_t *allocator,
    int64_t value,
    basl_toml_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_toml_float_new(
    const basl_allocator_t *allocator,
    double value,
    basl_toml_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_toml_bool_new(
    const basl_allocator_t *allocator,
    int value,
    basl_toml_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_toml_datetime_new(
    const basl_allocator_t *allocator,
    const basl_toml_datetime_t *value,
    basl_toml_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_toml_array_new(
    const basl_allocator_t *allocator,
    basl_toml_value_t **out,
    basl_error_t *error
);

BASL_API basl_status_t basl_toml_table_new(
    const basl_allocator_t *allocator,
    basl_toml_value_t **out,
    basl_error_t *error
);

BASL_API void basl_toml_free(basl_toml_value_t **value);

/* ── Type inspection ─────────────────────────────────────────────── */

BASL_API basl_toml_type_t basl_toml_type(const basl_toml_value_t *value);

/* ── Scalar accessors ────────────────────────────────────────────── */

BASL_API const char *basl_toml_string_value(const basl_toml_value_t *value);
BASL_API size_t basl_toml_string_length(const basl_toml_value_t *value);
BASL_API int64_t basl_toml_integer_value(const basl_toml_value_t *value);
BASL_API double basl_toml_float_value(const basl_toml_value_t *value);
BASL_API int basl_toml_bool_value(const basl_toml_value_t *value);
BASL_API const basl_toml_datetime_t *basl_toml_datetime_value(
    const basl_toml_value_t *value
);

/* ── Array operations ────────────────────────────────────────────── */

BASL_API size_t basl_toml_array_count(const basl_toml_value_t *array);

BASL_API const basl_toml_value_t *basl_toml_array_get(
    const basl_toml_value_t *array,
    size_t index
);

/** Takes ownership of element. */
BASL_API basl_status_t basl_toml_array_push(
    basl_toml_value_t *array,
    basl_toml_value_t *element,
    basl_error_t *error
);

/* ── Table operations ────────────────────────────────────────────── */

BASL_API size_t basl_toml_table_count(const basl_toml_value_t *table);

/** Returns NULL if key not found. */
BASL_API const basl_toml_value_t *basl_toml_table_get(
    const basl_toml_value_t *table,
    const char *key
);

/** Takes ownership of value.  Errors if key already exists. */
BASL_API basl_status_t basl_toml_table_set(
    basl_toml_value_t *table,
    const char *key,
    size_t key_length,
    basl_toml_value_t *value,
    basl_error_t *error
);

/** Iteration: returns key/value at position index (insertion order). */
BASL_API basl_status_t basl_toml_table_entry(
    const basl_toml_value_t *table,
    size_t index,
    const char **out_key,
    size_t *out_key_length,
    const basl_toml_value_t **out_value
);

/* ── Dotted key convenience ──────────────────────────────────────── */

/**
 * Traverse nested tables by dotted path.
 * basl_toml_table_get_path(root, "deps.json_schema") is equivalent to
 * basl_toml_table_get(basl_toml_table_get(root, "deps"), "json_schema").
 * Returns NULL if any segment is missing or not a table.
 */
BASL_API const basl_toml_value_t *basl_toml_table_get_path(
    const basl_toml_value_t *table,
    const char *dotted_key
);

/* ── Parser ──────────────────────────────────────────────────────── */

/**
 * Parse a TOML document.  Returns a table value representing the root.
 * Conforms to TOML v1.0.0.
 */
BASL_API basl_status_t basl_toml_parse(
    const basl_allocator_t *allocator,
    const char *input,
    size_t length,
    basl_toml_value_t **out,
    basl_error_t *error
);

/* ── Emitter ─────────────────────────────────────────────────────── */

/**
 * Serialize a TOML table to a string.  The root value must be a table.
 * Caller must free *out_string via the allocator (or free() if NULL).
 */
BASL_API basl_status_t basl_toml_emit(
    const basl_toml_value_t *value,
    char **out_string,
    size_t *out_length,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
