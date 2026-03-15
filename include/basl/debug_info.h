#ifndef BASL_DEBUG_INFO_H
#define BASL_DEBUG_INFO_H

#include <stddef.h>
#include <stdint.h>

#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/source.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Per-chunk local variable debug info ─────────────────────────── */

typedef struct basl_debug_local {
    const char *name;
    size_t name_length;
    size_t slot;
    size_t scope_start_ip;  /* IP when local comes into scope */
    size_t scope_end_ip;    /* IP when local goes out of scope */
} basl_debug_local_t;

typedef struct basl_debug_local_table {
    basl_runtime_t *runtime;
    basl_debug_local_t *locals;
    size_t count;
    size_t capacity;
} basl_debug_local_table_t;

BASL_API void basl_debug_local_table_init(
    basl_debug_local_table_t *table,
    basl_runtime_t *runtime
);
BASL_API void basl_debug_local_table_free(basl_debug_local_table_t *table);
BASL_API basl_status_t basl_debug_local_table_add(
    basl_debug_local_table_t *table,
    const char *name,
    size_t name_length,
    size_t slot,
    size_t scope_start_ip,
    basl_error_t *error
);
BASL_API void basl_debug_local_table_close_scope(
    basl_debug_local_table_t *table,
    size_t min_slot,
    size_t scope_end_ip
);
BASL_API const basl_debug_local_t *basl_debug_local_table_find(
    const basl_debug_local_table_t *table,
    size_t ip
);
BASL_API size_t basl_debug_local_table_count(
    const basl_debug_local_table_t *table
);
BASL_API const basl_debug_local_t *basl_debug_local_table_get(
    const basl_debug_local_table_t *table,
    size_t index
);

/* ── Program-level symbol table ──────────────────────────────────── */

typedef enum basl_debug_symbol_kind {
    BASL_DEBUG_SYMBOL_FUNCTION = 0,
    BASL_DEBUG_SYMBOL_CLASS = 1,
    BASL_DEBUG_SYMBOL_INTERFACE = 2,
    BASL_DEBUG_SYMBOL_ENUM = 3,
    BASL_DEBUG_SYMBOL_ENUM_MEMBER = 4,
    BASL_DEBUG_SYMBOL_FIELD = 5,
    BASL_DEBUG_SYMBOL_METHOD = 6,
    BASL_DEBUG_SYMBOL_GLOBAL_CONST = 7,
    BASL_DEBUG_SYMBOL_GLOBAL_VAR = 8
} basl_debug_symbol_kind_t;

typedef struct basl_debug_symbol {
    basl_debug_symbol_kind_t kind;
    const char *name;
    size_t name_length;
    basl_source_span_t span;
    int is_public;
    size_t parent_index;    /* index of owning class/interface/enum, or SIZE_MAX */
} basl_debug_symbol_t;

typedef struct basl_debug_symbol_table {
    basl_runtime_t *runtime;
    basl_debug_symbol_t *symbols;
    size_t count;
    size_t capacity;
} basl_debug_symbol_table_t;

BASL_API void basl_debug_symbol_table_init(
    basl_debug_symbol_table_t *table,
    basl_runtime_t *runtime
);
BASL_API void basl_debug_symbol_table_free(
    basl_debug_symbol_table_t *table
);
BASL_API basl_status_t basl_debug_symbol_table_add(
    basl_debug_symbol_table_t *table,
    basl_debug_symbol_kind_t kind,
    const char *name,
    size_t name_length,
    basl_source_span_t span,
    int is_public,
    size_t parent_index,
    basl_error_t *error
);
BASL_API size_t basl_debug_symbol_table_count(
    const basl_debug_symbol_table_t *table
);
BASL_API const basl_debug_symbol_t *basl_debug_symbol_table_get(
    const basl_debug_symbol_table_t *table,
    size_t index
);

#ifdef __cplusplus
}
#endif

#endif
