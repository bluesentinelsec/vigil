#ifndef BASL_SYMBOL_H
#define BASL_SYMBOL_H

#include <stddef.h>
#include <stdint.h>

#include "basl/export.h"
#include "basl/map.h"
#include "basl/runtime.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t basl_symbol_t;

#define BASL_SYMBOL_INVALID ((basl_symbol_t)0U)

typedef struct basl_symbol_table {
    basl_runtime_t *runtime;
    basl_map_t by_name;
    void *strings;
    size_t count;
    size_t capacity;
} basl_symbol_table_t;

BASL_API void basl_symbol_table_init(
    basl_symbol_table_t *table,
    basl_runtime_t *runtime
);
BASL_API void basl_symbol_table_clear(basl_symbol_table_t *table);
BASL_API void basl_symbol_table_free(basl_symbol_table_t *table);
BASL_API size_t basl_symbol_table_count(const basl_symbol_table_t *table);
BASL_API basl_status_t basl_symbol_table_intern(
    basl_symbol_table_t *table,
    const char *text,
    size_t length,
    basl_symbol_t *out_symbol,
    basl_error_t *error
);
BASL_API basl_status_t basl_symbol_table_intern_cstr(
    basl_symbol_table_t *table,
    const char *text,
    basl_symbol_t *out_symbol,
    basl_error_t *error
);
BASL_API const char *basl_symbol_table_c_str(
    const basl_symbol_table_t *table,
    basl_symbol_t symbol
);
BASL_API size_t basl_symbol_table_length(
    const basl_symbol_table_t *table,
    basl_symbol_t symbol
);
BASL_API int basl_symbol_table_is_valid(
    const basl_symbol_table_t *table,
    basl_symbol_t symbol
);

#ifdef __cplusplus
}
#endif

#endif
