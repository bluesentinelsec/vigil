#include <limits.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/string.h"
#include "basl/symbol.h"

static basl_string_t *basl_symbol_strings(basl_symbol_table_t *table) {
    return (basl_string_t *)table->strings;
}

static const basl_string_t *basl_symbol_const_strings(
    const basl_symbol_table_t *table
) {
    return (const basl_string_t *)table->strings;
}

static int basl_symbol_table_validate_mutable(
    const basl_symbol_table_t *table,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (table == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "symbol table must not be null"
        );
        return 0;
    }

    if (table->runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "symbol table runtime must not be null"
        );
        return 0;
    }

    return 1;
}

static size_t basl_symbol_next_capacity(size_t current_capacity) {
    if (current_capacity == 0U) {
        return 16U;
    }

    return current_capacity * 2U;
}

static basl_status_t basl_symbol_table_grow(
    basl_symbol_table_t *table,
    size_t minimum_capacity,
    basl_error_t *error
) {
    basl_status_t status;
    basl_string_t *strings;
    size_t new_capacity;
    size_t old_capacity;
    void *memory;

    if (table->capacity >= minimum_capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    new_capacity = basl_symbol_next_capacity(table->capacity);
    while (new_capacity < minimum_capacity) {
        if (new_capacity > SIZE_MAX / 2U) {
            basl_error_set_literal(
                error,
                BASL_STATUS_INVALID_ARGUMENT,
                "symbol table capacity would overflow"
            );
            return BASL_STATUS_INVALID_ARGUMENT;
        }

        new_capacity *= 2U;
    }

    if (new_capacity > SIZE_MAX / sizeof(*strings)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "symbol table capacity would overflow"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    old_capacity = table->capacity;
    memory = table->strings;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            table->runtime,
            new_capacity * sizeof(*strings),
            &memory,
            error
        );
    } else {
        status = basl_runtime_realloc(
            table->runtime,
            &memory,
            new_capacity * sizeof(*strings),
            error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_string_t *)memory + old_capacity,
                0,
                (new_capacity - old_capacity) * sizeof(*strings)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    table->strings = memory;
    table->capacity = new_capacity;
    return BASL_STATUS_OK;
}

void basl_symbol_table_init(
    basl_symbol_table_t *table,
    basl_runtime_t *runtime
) {
    if (table == NULL) {
        return;
    }

    memset(table, 0, sizeof(*table));
    table->runtime = runtime;
    basl_map_init(&table->by_name, runtime);
}

void basl_symbol_table_clear(basl_symbol_table_t *table) {
    size_t index;
    basl_string_t *strings;

    if (table == NULL) {
        return;
    }

    strings = basl_symbol_strings(table);
    for (index = 0U; index < table->count; index += 1U) {
        basl_string_free(&strings[index]);
        memset(&strings[index], 0, sizeof(strings[index]));
    }

    basl_map_clear(&table->by_name);
    table->count = 0U;
}

void basl_symbol_table_free(basl_symbol_table_t *table) {
    void *memory;

    if (table == NULL) {
        return;
    }

    basl_symbol_table_clear(table);
    memory = table->strings;
    if (table->runtime != NULL) {
        basl_runtime_free(table->runtime, &memory);
    }
    basl_map_free(&table->by_name);
    memset(table, 0, sizeof(*table));
}

size_t basl_symbol_table_count(const basl_symbol_table_t *table) {
    if (table == NULL) {
        return 0U;
    }

    return table->count;
}

basl_status_t basl_symbol_table_intern(
    basl_symbol_table_t *table,
    const char *text,
    size_t length,
    basl_symbol_t *out_symbol,
    basl_error_t *error
) {
    basl_status_t status;
    const basl_value_t *stored_value;
    basl_value_t symbol_value;
    basl_string_t *strings;
    basl_symbol_t symbol;

    if (out_symbol != NULL) {
        *out_symbol = BASL_SYMBOL_INVALID;
    }

    if (!basl_symbol_table_validate_mutable(table, error)) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (text == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "symbol text must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (out_symbol == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "out_symbol must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    stored_value = basl_map_get(&table->by_name, text, length);
    if (stored_value != NULL) {
        if (basl_value_kind(stored_value) != BASL_VALUE_INT) {
            basl_error_set_literal(
                error,
                BASL_STATUS_INTERNAL,
                "symbol table map entry must be an integer"
            );
            return BASL_STATUS_INTERNAL;
        }

        *out_symbol = (basl_symbol_t)basl_value_as_int(stored_value);
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    if (table->count == UINT32_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "symbol table is full"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_symbol_table_grow(table, table->count + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    strings = basl_symbol_strings(table);
    basl_string_init(&strings[table->count], table->runtime);
    status = basl_string_assign(&strings[table->count], text, length, error);
    if (status != BASL_STATUS_OK) {
        basl_string_free(&strings[table->count]);
        return status;
    }

    symbol = (basl_symbol_t)(table->count + 1U);
    basl_value_init_int(&symbol_value, (int64_t)symbol);
    status = basl_map_set(&table->by_name, text, length, &symbol_value, error);
    if (status != BASL_STATUS_OK) {
        basl_string_free(&strings[table->count]);
        memset(&strings[table->count], 0, sizeof(strings[table->count]));
        return status;
    }

    table->count += 1U;
    *out_symbol = symbol;
    basl_error_clear(error);
    return BASL_STATUS_OK;
}

basl_status_t basl_symbol_table_intern_cstr(
    basl_symbol_table_t *table,
    const char *text,
    basl_symbol_t *out_symbol,
    basl_error_t *error
) {
    if (text == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "symbol text must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return basl_symbol_table_intern(table, text, strlen(text), out_symbol, error);
}

const char *basl_symbol_table_c_str(
    const basl_symbol_table_t *table,
    basl_symbol_t symbol
) {
    const basl_string_t *strings;

    if (!basl_symbol_table_is_valid(table, symbol)) {
        return NULL;
    }

    strings = basl_symbol_const_strings(table);
    return basl_string_c_str(&strings[symbol - 1U]);
}

size_t basl_symbol_table_length(
    const basl_symbol_table_t *table,
    basl_symbol_t symbol
) {
    const basl_string_t *strings;

    if (!basl_symbol_table_is_valid(table, symbol)) {
        return 0U;
    }

    strings = basl_symbol_const_strings(table);
    return basl_string_length(&strings[symbol - 1U]);
}

int basl_symbol_table_is_valid(
    const basl_symbol_table_t *table,
    basl_symbol_t symbol
) {
    if (table == NULL || symbol == BASL_SYMBOL_INVALID) {
        return 0;
    }

    return (size_t)symbol <= table->count;
}
