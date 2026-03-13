#ifndef BASL_MAP_H
#define BASL_MAP_H

#include <stddef.h>

#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/status.h"
#include "basl/value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct basl_map {
    basl_runtime_t *runtime;
    void *entries;
    size_t count;
    size_t capacity;
    size_t tombstone_count;
} basl_map_t;

BASL_API void basl_map_init(
    basl_map_t *map,
    basl_runtime_t *runtime
);
BASL_API void basl_map_clear(basl_map_t *map);
BASL_API void basl_map_free(basl_map_t *map);
BASL_API size_t basl_map_count(const basl_map_t *map);
BASL_API basl_status_t basl_map_set(
    basl_map_t *map,
    const char *key,
    size_t key_length,
    const basl_value_t *value,
    basl_error_t *error
);
BASL_API basl_status_t basl_map_set_cstr(
    basl_map_t *map,
    const char *key,
    const basl_value_t *value,
    basl_error_t *error
);
/*
 * Returns a pointer into the map's internal storage. Any later mutating map
 * operation may invalidate the returned pointer.
 */
BASL_API const basl_value_t *basl_map_get(
    const basl_map_t *map,
    const char *key,
    size_t key_length
);
/*
 * Returns a pointer into the map's internal storage. Any later mutating map
 * operation may invalidate the returned pointer.
 */
BASL_API const basl_value_t *basl_map_get_cstr(
    const basl_map_t *map,
    const char *key
);
BASL_API int basl_map_contains(
    const basl_map_t *map,
    const char *key,
    size_t key_length
);
BASL_API int basl_map_contains_cstr(
    const basl_map_t *map,
    const char *key
);
/*
 * Iterates occupied entries by stable slot order. The returned key and value
 * point into the map's internal storage and are invalidated by later
 * mutations.
 */
BASL_API int basl_map_entry_at(
    const basl_map_t *map,
    size_t index,
    const char **out_key,
    size_t *out_key_length,
    const basl_value_t **out_value
);
BASL_API basl_status_t basl_map_remove(
    basl_map_t *map,
    const char *key,
    size_t key_length,
    int *out_removed,
    basl_error_t *error
);
BASL_API basl_status_t basl_map_remove_cstr(
    basl_map_t *map,
    const char *key,
    int *out_removed,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
