#ifndef BASL_DIAGNOSTIC_H
#define BASL_DIAGNOSTIC_H

#include <stddef.h>

#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum basl_diagnostic_severity {
    BASL_DIAGNOSTIC_ERROR = 0,
    BASL_DIAGNOSTIC_WARNING = 1,
    BASL_DIAGNOSTIC_NOTE = 2
} basl_diagnostic_severity_t;

typedef struct basl_diagnostic {
    basl_diagnostic_severity_t severity;
    basl_source_span_t span;
    basl_string_t message;
} basl_diagnostic_t;

typedef struct basl_diagnostic_list {
    basl_runtime_t *runtime;
    basl_diagnostic_t *items;
    size_t count;
    size_t capacity;
} basl_diagnostic_list_t;

BASL_API void basl_diagnostic_list_init(
    basl_diagnostic_list_t *list,
    basl_runtime_t *runtime
);
BASL_API void basl_diagnostic_list_clear(basl_diagnostic_list_t *list);
BASL_API void basl_diagnostic_list_free(basl_diagnostic_list_t *list);
BASL_API size_t basl_diagnostic_list_count(const basl_diagnostic_list_t *list);
/*
 * The returned pointer is invalidated by any subsequent list mutation,
 * including append calls that may reallocate the backing array.
 */
BASL_API const basl_diagnostic_t *basl_diagnostic_list_get(
    const basl_diagnostic_list_t *list,
    size_t index
);
BASL_API basl_status_t basl_diagnostic_list_append(
    basl_diagnostic_list_t *list,
    basl_diagnostic_severity_t severity,
    basl_source_span_t span,
    const char *message,
    size_t message_length,
    basl_error_t *error
);
BASL_API basl_status_t basl_diagnostic_list_append_cstr(
    basl_diagnostic_list_t *list,
    basl_diagnostic_severity_t severity,
    basl_source_span_t span,
    const char *message,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
