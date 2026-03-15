/* BASL debug info: local variable tables and program symbol tables.
 *
 * Used by debuggers (variable inspection) and LSP (go-to-definition,
 * find-references, hover).
 */
#include <string.h>

#include "basl/debug_info.h"
#include "basl/status.h"

/* ── Local variable table ────────────────────────────────────────── */

void basl_debug_local_table_init(
    basl_debug_local_table_t *table,
    basl_runtime_t *runtime
) {
    if (table == NULL) return;
    memset(table, 0, sizeof(*table));
    table->runtime = runtime;
}

void basl_debug_local_table_free(basl_debug_local_table_t *table) {
    void *memory;
    if (table == NULL) return;
    if (table->locals != NULL) {
        memory = table->locals;
        basl_runtime_free(table->runtime, &memory);
        table->locals = NULL;
    }
    table->count = 0U;
    table->capacity = 0U;
}

basl_status_t basl_debug_local_table_add(
    basl_debug_local_table_t *table,
    const char *name,
    size_t name_length,
    size_t slot,
    size_t scope_start_ip,
    basl_error_t *error
) {
    basl_debug_local_t *entry;

    if (table == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (table->count >= table->capacity) {
        size_t new_cap = table->capacity == 0U ? 8U : table->capacity * 2U;
        void *memory = NULL;
        basl_status_t status = basl_runtime_alloc(
            table->runtime,
            new_cap * sizeof(basl_debug_local_t),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) return status;
        if (table->locals != NULL) {
            memcpy(memory, table->locals,
                   table->count * sizeof(basl_debug_local_t));
            void *old = table->locals;
            basl_runtime_free(table->runtime, &old);
        }
        table->locals = (basl_debug_local_t *)memory;
        table->capacity = new_cap;
    }

    entry = &table->locals[table->count];
    entry->name = name;
    entry->name_length = name_length;
    entry->slot = slot;
    entry->scope_start_ip = scope_start_ip;
    entry->scope_end_ip = SIZE_MAX;  /* open until closed */
    table->count += 1U;
    return BASL_STATUS_OK;
}

void basl_debug_local_table_close_scope(
    basl_debug_local_table_t *table,
    size_t min_slot,
    size_t scope_end_ip
) {
    size_t i;
    if (table == NULL) return;
    for (i = table->count; i > 0U; i -= 1U) {
        basl_debug_local_t *entry = &table->locals[i - 1U];
        if (entry->scope_end_ip != SIZE_MAX) continue;
        if (entry->slot < min_slot) break;
        entry->scope_end_ip = scope_end_ip;
    }
}

const basl_debug_local_t *basl_debug_local_table_find(
    const basl_debug_local_table_t *table,
    size_t ip
) {
    /* Returns the first local whose scope contains the given IP.
     * For a full list, callers should iterate with _get/_count. */
    size_t i;
    if (table == NULL) return NULL;
    for (i = 0U; i < table->count; i += 1U) {
        const basl_debug_local_t *entry = &table->locals[i];
        if (ip >= entry->scope_start_ip && ip < entry->scope_end_ip) {
            return entry;
        }
    }
    return NULL;
}

size_t basl_debug_local_table_count(
    const basl_debug_local_table_t *table
) {
    return table != NULL ? table->count : 0U;
}

const basl_debug_local_t *basl_debug_local_table_get(
    const basl_debug_local_table_t *table,
    size_t index
) {
    if (table == NULL || index >= table->count) return NULL;
    return &table->locals[index];
}

/* ── Symbol table ────────────────────────────────────────────────── */

void basl_debug_symbol_table_init(
    basl_debug_symbol_table_t *table,
    basl_runtime_t *runtime
) {
    if (table == NULL) return;
    memset(table, 0, sizeof(*table));
    table->runtime = runtime;
}

void basl_debug_symbol_table_free(
    basl_debug_symbol_table_t *table
) {
    void *memory;
    if (table == NULL) return;
    if (table->symbols != NULL) {
        memory = table->symbols;
        basl_runtime_free(table->runtime, &memory);
        table->symbols = NULL;
    }
    table->count = 0U;
    table->capacity = 0U;
}

basl_status_t basl_debug_symbol_table_add(
    basl_debug_symbol_table_t *table,
    basl_debug_symbol_kind_t kind,
    const char *name,
    size_t name_length,
    basl_source_span_t span,
    int is_public,
    size_t parent_index,
    basl_error_t *error
) {
    basl_debug_symbol_t *entry;

    if (table == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (table->count >= table->capacity) {
        size_t new_cap = table->capacity == 0U ? 16U : table->capacity * 2U;
        void *memory = NULL;
        basl_status_t status = basl_runtime_alloc(
            table->runtime,
            new_cap * sizeof(basl_debug_symbol_t),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) return status;
        if (table->symbols != NULL) {
            memcpy(memory, table->symbols,
                   table->count * sizeof(basl_debug_symbol_t));
            void *old = table->symbols;
            basl_runtime_free(table->runtime, &old);
        }
        table->symbols = (basl_debug_symbol_t *)memory;
        table->capacity = new_cap;
    }

    entry = &table->symbols[table->count];
    entry->kind = kind;
    entry->name = name;
    entry->name_length = name_length;
    entry->span = span;
    entry->is_public = is_public;
    entry->parent_index = parent_index;
    table->count += 1U;
    return BASL_STATUS_OK;
}

size_t basl_debug_symbol_table_count(
    const basl_debug_symbol_table_t *table
) {
    return table != NULL ? table->count : 0U;
}

const basl_debug_symbol_t *basl_debug_symbol_table_get(
    const basl_debug_symbol_table_t *table,
    size_t index
) {
    if (table == NULL || index >= table->count) return NULL;
    return &table->symbols[index];
}
