#ifndef BASL_SOURCE_H
#define BASL_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/status.h"
#include "basl/string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t basl_source_id_t;

typedef struct basl_source_span {
    basl_source_id_t source_id;
    size_t start_offset;
    size_t end_offset;
} basl_source_span_t;

typedef struct basl_source_file {
    basl_source_id_t id;
    basl_string_t path;
    basl_string_t text;
} basl_source_file_t;

typedef struct basl_source_registry {
    basl_runtime_t *runtime;
    basl_source_file_t *files;
    size_t count;
    size_t capacity;
} basl_source_registry_t;

BASL_API void basl_source_span_clear(basl_source_span_t *span);
BASL_API void basl_source_registry_init(
    basl_source_registry_t *registry,
    basl_runtime_t *runtime
);
BASL_API void basl_source_registry_free(basl_source_registry_t *registry);
BASL_API size_t basl_source_registry_count(const basl_source_registry_t *registry);
/*
 * The returned pointer is invalidated by any subsequent registry mutation,
 * including register calls that may reallocate the backing array.
 */
BASL_API const basl_source_file_t *basl_source_registry_get(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id
);
BASL_API basl_status_t basl_source_registry_resolve_location(
    const basl_source_registry_t *registry,
    basl_source_location_t *location,
    basl_error_t *error
);
BASL_API basl_status_t basl_source_registry_resolve_span_start(
    const basl_source_registry_t *registry,
    basl_source_span_t span,
    basl_source_location_t *out_location,
    basl_error_t *error
);
BASL_API basl_status_t basl_source_registry_register(
    basl_source_registry_t *registry,
    const char *path,
    size_t path_length,
    const char *text,
    size_t text_length,
    basl_source_id_t *out_source_id,
    basl_error_t *error
);
BASL_API basl_status_t basl_source_registry_register_cstr(
    basl_source_registry_t *registry,
    const char *path,
    const char *text,
    basl_source_id_t *out_source_id,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
