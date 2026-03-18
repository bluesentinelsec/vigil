#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/chunk.h"

static basl_status_t basl_chunk_validate_mutable(
    const basl_chunk_t *chunk,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (chunk == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "chunk must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk->runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "chunk runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_chunk_validate_output(
    const basl_chunk_t *chunk,
    const basl_string_t *output,
    basl_error_t *error
) {
    basl_status_t status;

    status = basl_chunk_validate_mutable(chunk, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (output == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "output string must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (output->bytes.runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "output string runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_chunk_grow_spans(
    basl_chunk_t *chunk,
    size_t minimum_capacity,
    basl_error_t *error
) {
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= chunk->span_capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    old_capacity = chunk->span_capacity;
    next_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > (SIZE_MAX / 2U)) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*chunk->spans))) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "chunk span allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (chunk->spans == NULL) {
        memory = NULL;
        status = basl_runtime_alloc(
            chunk->runtime,
            next_capacity * sizeof(*chunk->spans),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        memory = chunk->spans;
        status = basl_runtime_realloc(
            chunk->runtime,
            &memory,
            next_capacity * sizeof(*chunk->spans),
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        memset(
            (basl_source_span_t *)memory + old_capacity,
            0,
            (next_capacity - old_capacity) * sizeof(*chunk->spans)
        );
    }

    chunk->spans = (basl_source_span_t *)memory;
    chunk->span_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_chunk_grow_constants(
    basl_chunk_t *chunk,
    size_t minimum_capacity,
    basl_error_t *error
) {
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= chunk->constant_capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    old_capacity = chunk->constant_capacity;
    next_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > (SIZE_MAX / 2U)) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*chunk->constants))) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "chunk constant allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (chunk->constants == NULL) {
        memory = NULL;
        status = basl_runtime_alloc(
            chunk->runtime,
            next_capacity * sizeof(*chunk->constants),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        memory = chunk->constants;
        status = basl_runtime_realloc(
            chunk->runtime,
            &memory,
            next_capacity * sizeof(*chunk->constants),
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        memset(
            (basl_value_t *)memory + old_capacity,
            0,
            (next_capacity - old_capacity) * sizeof(*chunk->constants)
        );
    }

    chunk->constants = (basl_value_t *)memory;
    chunk->constant_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_chunk_append_bytes(
    basl_chunk_t *chunk,
    const uint8_t *bytes,
    size_t byte_count,
    basl_source_span_t span,
    basl_error_t *error
) {
    basl_status_t status;
    size_t old_code_length;
    size_t old_span_count;
    size_t i;

    if (byte_count == 0U) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    old_code_length = chunk->code.length;
    old_span_count = chunk->span_count;
    if (byte_count > SIZE_MAX - old_code_length) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "chunk code size overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (byte_count > SIZE_MAX - old_span_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "chunk span table overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_byte_buffer_reserve(&chunk->code, old_code_length + byte_count, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_chunk_grow_spans(chunk, old_span_count + byte_count, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    memcpy(chunk->code.data + old_code_length, bytes, byte_count);
    chunk->code.length = old_code_length + byte_count;
    for (i = 0U; i < byte_count; ++i) {
        chunk->spans[old_span_count + i] = span;
    }

    chunk->span_count = old_span_count + byte_count;
    return BASL_STATUS_OK;
}

static basl_status_t basl_chunk_append_text(
    basl_string_t *output,
    const char *text,
    basl_error_t *error
) {
    return basl_string_append_cstr(output, text, error);
}

static basl_status_t basl_chunk_append_value(
    basl_string_t *output,
    const basl_value_t *value,
    basl_error_t *error
) {
    char buffer[64];
    int written;
    basl_object_t *object;
    basl_status_t status;

    if (value == NULL) {
        return basl_chunk_append_text(output, "<null>", error);
    }

    switch (basl_value_kind(value)) {
        case BASL_VALUE_NIL:
            return basl_chunk_append_text(output, "nil", error);
        case BASL_VALUE_BOOL:
            return basl_chunk_append_text(
                output,
                basl_value_as_bool(value) ? "true" : "false",
                error
            );
        case BASL_VALUE_INT:
            written = snprintf(
                buffer,
                sizeof(buffer),
                "%lld",
                (long long)basl_value_as_int(value)
            );
            break;
        case BASL_VALUE_UINT:
            written = snprintf(
                buffer,
                sizeof(buffer),
                "%llu",
                (unsigned long long)basl_value_as_uint(value)
            );
            break;
        case BASL_VALUE_FLOAT:
            written = snprintf(
                buffer,
                sizeof(buffer),
                "%.17g",
                basl_value_as_float(value)
            );
            break;
        case BASL_VALUE_OBJECT:
            object = basl_value_as_object(value);
            if (basl_object_type(object) == BASL_OBJECT_STRING) {
                status = basl_chunk_append_text(output, "\"", error);
                if (status != BASL_STATUS_OK) {
                    return status;
                }

                status = basl_string_append_cstr(
                    output,
                    basl_string_object_c_str(object),
                    error
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }

                return basl_chunk_append_text(output, "\"", error);
            }

            return basl_chunk_append_text(output, "<object>", error);
        default:
            return basl_chunk_append_text(output, "<unknown>", error);
    }

    if (written < 0) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INTERNAL,
            "failed to format chunk value"
        );
        return BASL_STATUS_INTERNAL;
    }

    return basl_string_append(output, buffer, (size_t)written, error);
}

void basl_chunk_init(basl_chunk_t *chunk, basl_runtime_t *runtime) {
    if (chunk == NULL) {
        return;
    }

    memset(chunk, 0, sizeof(*chunk));
    chunk->runtime = runtime;
    basl_byte_buffer_init(&chunk->code, runtime);
    basl_debug_local_table_init(&chunk->debug_locals, runtime);
}

void basl_chunk_clear(basl_chunk_t *chunk) {
    size_t i;

    if (chunk == NULL) {
        return;
    }

    basl_byte_buffer_clear(&chunk->code);
    chunk->span_count = 0U;
    for (i = 0U; i < chunk->constant_count; ++i) {
        basl_value_release(&chunk->constants[i]);
    }

    chunk->constant_count = 0U;
}

void basl_chunk_free(basl_chunk_t *chunk) {
    void *memory;

    if (chunk == NULL) {
        return;
    }

    basl_chunk_clear(chunk);
    basl_byte_buffer_free(&chunk->code);

    memory = chunk->spans;
    if (chunk->runtime != NULL) {
        basl_runtime_free(chunk->runtime, &memory);
    }

    memory = chunk->constants;
    if (chunk->runtime != NULL) {
        basl_runtime_free(chunk->runtime, &memory);
    }

    basl_debug_local_table_free(&chunk->debug_locals);

    memset(chunk, 0, sizeof(*chunk));
}

size_t basl_chunk_code_size(const basl_chunk_t *chunk) {
    if (chunk == NULL) {
        return 0U;
    }

    return chunk->code.length;
}

const uint8_t *basl_chunk_code(const basl_chunk_t *chunk) {
    if (chunk == NULL) {
        return NULL;
    }

    return chunk->code.data;
}

size_t basl_chunk_constant_count(const basl_chunk_t *chunk) {
    if (chunk == NULL) {
        return 0U;
    }

    return chunk->constant_count;
}

const basl_value_t *basl_chunk_constant(
    const basl_chunk_t *chunk,
    size_t index
) {
    if (chunk == NULL || index >= chunk->constant_count) {
        return NULL;
    }

    return &chunk->constants[index];
}

basl_source_span_t basl_chunk_span_at(
    const basl_chunk_t *chunk,
    size_t offset
) {
    basl_source_span_t span;

    basl_source_span_clear(&span);
    if (chunk == NULL || offset >= chunk->span_count) {
        return span;
    }

    return chunk->spans[offset];
}

const char *basl_opcode_name(basl_opcode_t opcode) {
    switch (opcode) {
        case BASL_OPCODE_CONSTANT:
            return "CONSTANT";
        case BASL_OPCODE_NIL:
            return "NIL";
        case BASL_OPCODE_TRUE:
            return "TRUE";
        case BASL_OPCODE_FALSE:
            return "FALSE";
        case BASL_OPCODE_RETURN:
            return "RETURN";
        case BASL_OPCODE_POP:
            return "POP";
        case BASL_OPCODE_DUP:
            return "DUP";
        case BASL_OPCODE_DUP_TWO:
            return "DUP_TWO";
        case BASL_OPCODE_GET_LOCAL:
            return "GET_LOCAL";
        case BASL_OPCODE_SET_LOCAL:
            return "SET_LOCAL";
        case BASL_OPCODE_GET_GLOBAL:
            return "GET_GLOBAL";
        case BASL_OPCODE_SET_GLOBAL:
            return "SET_GLOBAL";
        case BASL_OPCODE_GET_FUNCTION:
            return "GET_FUNCTION";
        case BASL_OPCODE_NEW_CLOSURE:
            return "NEW_CLOSURE";
        case BASL_OPCODE_GET_CAPTURE:
            return "GET_CAPTURE";
        case BASL_OPCODE_SET_CAPTURE:
            return "SET_CAPTURE";
        case BASL_OPCODE_JUMP:
            return "JUMP";
        case BASL_OPCODE_JUMP_IF_FALSE:
            return "JUMP_IF_FALSE";
        case BASL_OPCODE_LOOP:
            return "LOOP";
        case BASL_OPCODE_ADD:
            return "ADD";
        case BASL_OPCODE_SUBTRACT:
            return "SUBTRACT";
        case BASL_OPCODE_MULTIPLY:
            return "MULTIPLY";
        case BASL_OPCODE_DIVIDE:
            return "DIVIDE";
        case BASL_OPCODE_MODULO:
            return "MODULO";
        case BASL_OPCODE_BITWISE_AND:
            return "BITWISE_AND";
        case BASL_OPCODE_BITWISE_OR:
            return "BITWISE_OR";
        case BASL_OPCODE_BITWISE_XOR:
            return "BITWISE_XOR";
        case BASL_OPCODE_SHIFT_LEFT:
            return "SHIFT_LEFT";
        case BASL_OPCODE_SHIFT_RIGHT:
            return "SHIFT_RIGHT";
        case BASL_OPCODE_NEGATE:
            return "NEGATE";
        case BASL_OPCODE_NOT:
            return "NOT";
        case BASL_OPCODE_BITWISE_NOT:
            return "BITWISE_NOT";
        case BASL_OPCODE_TO_I32:
            return "TO_I32";
        case BASL_OPCODE_TO_I64:
            return "TO_I64";
        case BASL_OPCODE_TO_U8:
            return "TO_U8";
        case BASL_OPCODE_TO_U32:
            return "TO_U32";
        case BASL_OPCODE_TO_U64:
            return "TO_U64";
        case BASL_OPCODE_TO_F64:
            return "TO_F64";
        case BASL_OPCODE_TO_STRING:
            return "TO_STRING";
        case BASL_OPCODE_FORMAT_F64:
            return "FORMAT_F64";
        case BASL_OPCODE_NEW_ERROR:
            return "NEW_ERROR";
        case BASL_OPCODE_GET_ERROR_KIND:
            return "GET_ERROR_KIND";
        case BASL_OPCODE_GET_ERROR_MESSAGE:
            return "GET_ERROR_MESSAGE";
        case BASL_OPCODE_EQUAL:
            return "EQUAL";
        case BASL_OPCODE_GREATER:
            return "GREATER";
        case BASL_OPCODE_LESS:
            return "LESS";
        case BASL_OPCODE_CALL:
            return "CALL";
        case BASL_OPCODE_CALL_VALUE:
            return "CALL_VALUE";
        case BASL_OPCODE_NEW_INSTANCE:
            return "NEW_INSTANCE";
        case BASL_OPCODE_GET_FIELD:
            return "GET_FIELD";
        case BASL_OPCODE_SET_FIELD:
            return "SET_FIELD";
        case BASL_OPCODE_NEW_ARRAY:
            return "NEW_ARRAY";
        case BASL_OPCODE_NEW_MAP:
            return "NEW_MAP";
        case BASL_OPCODE_GET_INDEX:
            return "GET_INDEX";
        case BASL_OPCODE_SET_INDEX:
            return "SET_INDEX";
        case BASL_OPCODE_GET_COLLECTION_SIZE:
            return "GET_COLLECTION_SIZE";
        case BASL_OPCODE_GET_MAP_KEY_AT:
            return "GET_MAP_KEY_AT";
        case BASL_OPCODE_GET_MAP_VALUE_AT:
            return "GET_MAP_VALUE_AT";
        case BASL_OPCODE_CALL_INTERFACE:
            return "CALL_INTERFACE";
        case BASL_OPCODE_DEFER_CALL:
            return "DEFER_CALL";
        case BASL_OPCODE_DEFER_CALL_VALUE:
            return "DEFER_CALL_VALUE";
        case BASL_OPCODE_DEFER_NEW_INSTANCE:
            return "DEFER_NEW_INSTANCE";
        case BASL_OPCODE_DEFER_CALL_INTERFACE:
            return "DEFER_CALL_INTERFACE";
        case BASL_OPCODE_GET_STRING_SIZE:
            return "GET_STRING_SIZE";
        case BASL_OPCODE_STRING_CONTAINS:
            return "STRING_CONTAINS";
        case BASL_OPCODE_STRING_STARTS_WITH:
            return "STRING_STARTS_WITH";
        case BASL_OPCODE_STRING_ENDS_WITH:
            return "STRING_ENDS_WITH";
        case BASL_OPCODE_STRING_TRIM:
            return "STRING_TRIM";
        case BASL_OPCODE_STRING_TO_UPPER:
            return "STRING_TO_UPPER";
        case BASL_OPCODE_STRING_TO_LOWER:
            return "STRING_TO_LOWER";
        case BASL_OPCODE_STRING_REPLACE:
            return "STRING_REPLACE";
        case BASL_OPCODE_STRING_SPLIT:
            return "STRING_SPLIT";
        case BASL_OPCODE_STRING_INDEX_OF:
            return "STRING_INDEX_OF";
        case BASL_OPCODE_STRING_SUBSTR:
            return "STRING_SUBSTR";
        case BASL_OPCODE_STRING_BYTES:
            return "STRING_BYTES";
        case BASL_OPCODE_STRING_CHAR_AT:
            return "STRING_CHAR_AT";
        case BASL_OPCODE_ARRAY_PUSH:
            return "ARRAY_PUSH";
        case BASL_OPCODE_ARRAY_POP:
            return "ARRAY_POP";
        case BASL_OPCODE_ARRAY_GET_SAFE:
            return "ARRAY_GET_SAFE";
        case BASL_OPCODE_ARRAY_SET_SAFE:
            return "ARRAY_SET_SAFE";
        case BASL_OPCODE_ARRAY_SLICE:
            return "ARRAY_SLICE";
        case BASL_OPCODE_ARRAY_CONTAINS:
            return "ARRAY_CONTAINS";
        case BASL_OPCODE_MAP_GET_SAFE:
            return "MAP_GET_SAFE";
        case BASL_OPCODE_MAP_SET_SAFE:
            return "MAP_SET_SAFE";
        case BASL_OPCODE_MAP_REMOVE_SAFE:
            return "MAP_REMOVE_SAFE";
        case BASL_OPCODE_MAP_HAS:
            return "MAP_HAS";
        case BASL_OPCODE_MAP_KEYS:
            return "MAP_KEYS";
        case BASL_OPCODE_MAP_VALUES:
            return "MAP_VALUES";
        case BASL_OPCODE_ADD_I64:           return "ADD_I64";
        case BASL_OPCODE_SUBTRACT_I64:      return "SUBTRACT_I64";
        case BASL_OPCODE_LESS_I64:          return "LESS_I64";
        case BASL_OPCODE_LESS_EQUAL_I64:    return "LESS_EQUAL_I64";
        case BASL_OPCODE_GREATER_I64:       return "GREATER_I64";
        case BASL_OPCODE_GREATER_EQUAL_I64: return "GREATER_EQUAL_I64";
        case BASL_OPCODE_MULTIPLY_I64:      return "MULTIPLY_I64";
        case BASL_OPCODE_DIVIDE_I64:        return "DIVIDE_I64";
        case BASL_OPCODE_MODULO_I64:        return "MODULO_I64";
        case BASL_OPCODE_EQUAL_I64:         return "EQUAL_I64";
        case BASL_OPCODE_NOT_EQUAL_I64:     return "NOT_EQUAL_I64";
        case BASL_OPCODE_LOCALS_ADD_I64:    return "LOCALS_ADD_I64";
        case BASL_OPCODE_LOCALS_SUBTRACT_I64: return "LOCALS_SUBTRACT_I64";
        case BASL_OPCODE_LOCALS_MULTIPLY_I64: return "LOCALS_MULTIPLY_I64";
        case BASL_OPCODE_LOCALS_MODULO_I64: return "LOCALS_MODULO_I64";
        case BASL_OPCODE_LOCALS_LESS_I64:   return "LOCALS_LESS_I64";
        case BASL_OPCODE_LOCALS_LESS_EQUAL_I64: return "LOCALS_LESS_EQUAL_I64";
        case BASL_OPCODE_LOCALS_GREATER_I64: return "LOCALS_GREATER_I64";
        case BASL_OPCODE_LOCALS_GREATER_EQUAL_I64: return "LOCALS_GREATER_EQUAL_I64";
        case BASL_OPCODE_LOCALS_EQUAL_I64:  return "LOCALS_EQUAL_I64";
        case BASL_OPCODE_LOCALS_NOT_EQUAL_I64: return "LOCALS_NOT_EQUAL_I64";
        case BASL_OPCODE_ADD_I32:           return "ADD_I32";
        case BASL_OPCODE_SUBTRACT_I32:      return "SUBTRACT_I32";
        case BASL_OPCODE_MULTIPLY_I32:      return "MULTIPLY_I32";
        case BASL_OPCODE_LESS_I32:          return "LESS_I32";
        case BASL_OPCODE_LESS_EQUAL_I32:    return "LESS_EQUAL_I32";
        case BASL_OPCODE_GREATER_I32:       return "GREATER_I32";
        case BASL_OPCODE_GREATER_EQUAL_I32: return "GREATER_EQUAL_I32";
        case BASL_OPCODE_EQUAL_I32:         return "EQUAL_I32";
        case BASL_OPCODE_NOT_EQUAL_I32:     return "NOT_EQUAL_I32";
        case BASL_OPCODE_MODULO_I32:        return "MODULO_I32";
        case BASL_OPCODE_DIVIDE_I32:        return "DIVIDE_I32";
        case BASL_OPCODE_LOCALS_ADD_I32_STORE: return "LOCALS_ADD_I32_STORE";
        case BASL_OPCODE_LOCALS_SUBTRACT_I32_STORE: return "LOCALS_SUBTRACT_I32_STORE";
        case BASL_OPCODE_LOCALS_MULTIPLY_I32_STORE: return "LOCALS_MULTIPLY_I32_STORE";
        case BASL_OPCODE_LOCALS_LESS_I32_STORE: return "LOCALS_LESS_I32_STORE";
        case BASL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE: return "LOCALS_LESS_EQUAL_I32_STORE";
        case BASL_OPCODE_LOCALS_GREATER_I32_STORE: return "LOCALS_GREATER_I32_STORE";
        case BASL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE: return "LOCALS_GREATER_EQUAL_I32_STORE";
        case BASL_OPCODE_LOCALS_EQUAL_I32_STORE: return "LOCALS_EQUAL_I32_STORE";
        case BASL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE: return "LOCALS_NOT_EQUAL_I32_STORE";
        case BASL_OPCODE_LOCALS_MODULO_I32_STORE: return "LOCALS_MODULO_I32_STORE";
        case BASL_OPCODE_INCREMENT_LOCAL_I32: return "INCREMENT_LOCAL_I32";
        case BASL_OPCODE_TAIL_CALL: return "TAIL_CALL";
        case BASL_OPCODE_FORLOOP_I32: return "FORLOOP_I32";
        case BASL_OPCODE_CALL_NATIVE: return "CALL_NATIVE";
        case BASL_OPCODE_DEFER_CALL_NATIVE: return "DEFER_CALL_NATIVE";
        case BASL_OPCODE_STRING_TRIM_LEFT: return "STRING_TRIM_LEFT";
        case BASL_OPCODE_STRING_TRIM_RIGHT: return "STRING_TRIM_RIGHT";
        case BASL_OPCODE_STRING_REPEAT: return "STRING_REPEAT";
        case BASL_OPCODE_STRING_REVERSE: return "STRING_REVERSE";
        case BASL_OPCODE_STRING_IS_EMPTY: return "STRING_IS_EMPTY";
        case BASL_OPCODE_STRING_COUNT: return "STRING_COUNT";
        case BASL_OPCODE_STRING_LAST_INDEX_OF: return "STRING_LAST_INDEX_OF";
        case BASL_OPCODE_STRING_TRIM_PREFIX: return "STRING_TRIM_PREFIX";
        case BASL_OPCODE_STRING_TRIM_SUFFIX: return "STRING_TRIM_SUFFIX";
        case BASL_OPCODE_CHAR_FROM_INT: return "CHAR_FROM_INT";
        case BASL_OPCODE_STRING_TO_C: return "STRING_TO_C";
        case BASL_OPCODE_STRING_JOIN: return "STRING_JOIN";
        case BASL_OPCODE_STRING_CUT: return "STRING_CUT";
        case BASL_OPCODE_STRING_FIELDS: return "STRING_FIELDS";
        case BASL_OPCODE_STRING_EQUAL_FOLD: return "STRING_EQUAL_FOLD";
        case BASL_OPCODE_STRING_CHAR_COUNT: return "STRING_CHAR_COUNT";
        case BASL_OPCODE_FORMAT_SPEC: return "FORMAT_SPEC";
        default:
            return "UNKNOWN";
    }
}

basl_status_t basl_chunk_write_byte(
    basl_chunk_t *chunk,
    uint8_t value,
    basl_source_span_t span,
    basl_error_t *error
) {
    basl_status_t status;

    status = basl_chunk_validate_mutable(chunk, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_chunk_append_bytes(chunk, &value, 1U, span, error);
}

basl_status_t basl_chunk_write_opcode(
    basl_chunk_t *chunk,
    basl_opcode_t opcode,
    basl_source_span_t span,
    basl_error_t *error
) {
    return basl_chunk_write_byte(chunk, (uint8_t)opcode, span, error);
}

basl_status_t basl_chunk_write_u32(
    basl_chunk_t *chunk,
    uint32_t value,
    basl_source_span_t span,
    basl_error_t *error
) {
    uint8_t encoded[4];
    basl_status_t status;

    status = basl_chunk_validate_mutable(chunk, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    encoded[0] = (uint8_t)(value & 0xffU);
    encoded[1] = (uint8_t)((value >> 8U) & 0xffU);
    encoded[2] = (uint8_t)((value >> 16U) & 0xffU);
    encoded[3] = (uint8_t)((value >> 24U) & 0xffU);
    return basl_chunk_append_bytes(chunk, encoded, sizeof(encoded), span, error);
}

basl_status_t basl_chunk_add_constant(
    basl_chunk_t *chunk,
    const basl_value_t *value,
    size_t *out_index,
    basl_error_t *error
) {
    basl_status_t status;

    status = basl_chunk_validate_mutable(chunk, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (value == NULL || out_index == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "chunk constant requires value and out_index"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk->constant_count == SIZE_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "chunk constant table overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_chunk_grow_constants(chunk, chunk->constant_count + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    chunk->constants[chunk->constant_count] = basl_value_copy(value);
    *out_index = chunk->constant_count;
    chunk->constant_count += 1U;
    return BASL_STATUS_OK;
}

basl_status_t basl_chunk_write_constant(
    basl_chunk_t *chunk,
    const basl_value_t *value,
    basl_source_span_t span,
    size_t *out_index,
    basl_error_t *error
) {
    basl_status_t status;
    size_t index;
    uint8_t encoded[5];

    status = basl_chunk_add_constant(chunk, value, &index, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (index > UINT32_MAX) {
        basl_value_release(&chunk->constants[chunk->constant_count - 1U]);
        chunk->constant_count -= 1U;
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "chunk constant index overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    encoded[0] = (uint8_t)BASL_OPCODE_CONSTANT;
    encoded[1] = (uint8_t)((uint32_t)index & 0xffU);
    encoded[2] = (uint8_t)(((uint32_t)index >> 8U) & 0xffU);
    encoded[3] = (uint8_t)(((uint32_t)index >> 16U) & 0xffU);
    encoded[4] = (uint8_t)(((uint32_t)index >> 24U) & 0xffU);
    status = basl_chunk_append_bytes(chunk, encoded, sizeof(encoded), span, error);
    if (status != BASL_STATUS_OK) {
        basl_value_release(&chunk->constants[chunk->constant_count - 1U]);
        chunk->constant_count -= 1U;
        return status;
    }

    if (out_index != NULL) {
        *out_index = index;
    }

    return BASL_STATUS_OK;
}

basl_status_t basl_chunk_disassemble(
    const basl_chunk_t *chunk,
    basl_string_t *output,
    basl_error_t *error
) {
    size_t offset;
    char line[64];
    uint32_t operand;
    int written;
    basl_status_t status;
    basl_opcode_t opcode;

    status = basl_chunk_validate_output(chunk, output, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_string_clear(output);
    for (offset = 0U; offset < chunk->code.length; ) {
        opcode = (basl_opcode_t)chunk->code.data[offset];
        written = snprintf(
            line,
            sizeof(line),
            "%04zu %s",
            offset,
            basl_opcode_name(opcode)
        );
        if (written < 0) {
            basl_error_set_literal(
                error,
                BASL_STATUS_INTERNAL,
                "failed to format chunk disassembly"
            );
            return BASL_STATUS_INTERNAL;
        }

        status = basl_string_append(output, line, (size_t)written, error);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        if (
            opcode == BASL_OPCODE_CALL ||
            opcode == BASL_OPCODE_DEFER_CALL
        ) {
            uint32_t function_index;
            uint32_t arg_count;

            if (offset + 8U >= chunk->code.length) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "truncated call instruction"
                );
                return BASL_STATUS_INTERNAL;
            }

            function_index = (uint32_t)chunk->code.data[offset + 1U];
            function_index |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            function_index |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            function_index |= (uint32_t)chunk->code.data[offset + 4U] << 24U;
            arg_count = (uint32_t)chunk->code.data[offset + 5U];
            arg_count |= (uint32_t)chunk->code.data[offset + 6U] << 8U;
            arg_count |= (uint32_t)chunk->code.data[offset + 7U] << 16U;
            arg_count |= (uint32_t)chunk->code.data[offset + 8U] << 24U;

            written = snprintf(
                line,
                sizeof(line),
                " %u %u",
                function_index,
                arg_count
            );
            if (written < 0) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "failed to format chunk call operand"
                );
                return BASL_STATUS_INTERNAL;
            }

            status = basl_string_append(output, line, (size_t)written, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }

            offset += 9U;
        } else if (
            opcode == BASL_OPCODE_CALL_VALUE ||
            opcode == BASL_OPCODE_DEFER_CALL_VALUE
        ) {
            if (offset + 4U >= chunk->code.length) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "truncated indirect call instruction"
                );
                return BASL_STATUS_INTERNAL;
            }

            operand = (uint32_t)chunk->code.data[offset + 1U];
            operand |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            operand |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            operand |= (uint32_t)chunk->code.data[offset + 4U] << 24U;

            written = snprintf(line, sizeof(line), " %u", operand);
            if (written < 0) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "failed to format chunk indirect call operand"
                );
                return BASL_STATUS_INTERNAL;
            }

            status = basl_string_append(output, line, (size_t)written, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }

            offset += 5U;
        } else if (opcode == BASL_OPCODE_NEW_CLOSURE) {
            uint32_t function_index;
            uint32_t capture_count;

            if (offset + 8U >= chunk->code.length) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "truncated closure instruction"
                );
                return BASL_STATUS_INTERNAL;
            }

            function_index = (uint32_t)chunk->code.data[offset + 1U];
            function_index |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            function_index |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            function_index |= (uint32_t)chunk->code.data[offset + 4U] << 24U;
            capture_count = (uint32_t)chunk->code.data[offset + 5U];
            capture_count |= (uint32_t)chunk->code.data[offset + 6U] << 8U;
            capture_count |= (uint32_t)chunk->code.data[offset + 7U] << 16U;
            capture_count |= (uint32_t)chunk->code.data[offset + 8U] << 24U;

            written = snprintf(line, sizeof(line), " %u %u", function_index, capture_count);
            if (written < 0) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "failed to format chunk closure operand"
                );
                return BASL_STATUS_INTERNAL;
            }

            status = basl_string_append(output, line, (size_t)written, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }

            offset += 9U;
        } else if (
            opcode == BASL_OPCODE_CALL_INTERFACE ||
            opcode == BASL_OPCODE_DEFER_CALL_INTERFACE
        ) {
            uint32_t interface_index;
            uint32_t method_index;
            uint32_t arg_count;

            if (offset + 12U >= chunk->code.length) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "truncated interface call instruction"
                );
                return BASL_STATUS_INTERNAL;
            }

            interface_index = (uint32_t)chunk->code.data[offset + 1U];
            interface_index |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            interface_index |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            interface_index |= (uint32_t)chunk->code.data[offset + 4U] << 24U;
            method_index = (uint32_t)chunk->code.data[offset + 5U];
            method_index |= (uint32_t)chunk->code.data[offset + 6U] << 8U;
            method_index |= (uint32_t)chunk->code.data[offset + 7U] << 16U;
            method_index |= (uint32_t)chunk->code.data[offset + 8U] << 24U;
            arg_count = (uint32_t)chunk->code.data[offset + 9U];
            arg_count |= (uint32_t)chunk->code.data[offset + 10U] << 8U;
            arg_count |= (uint32_t)chunk->code.data[offset + 11U] << 16U;
            arg_count |= (uint32_t)chunk->code.data[offset + 12U] << 24U;

            written = snprintf(
                line,
                sizeof(line),
                " %u %u %u",
                interface_index,
                method_index,
                arg_count
            );
            if (written < 0) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "failed to format chunk interface call operand"
                );
                return BASL_STATUS_INTERNAL;
            }

            status = basl_string_append(output, line, (size_t)written, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }

            offset += 13U;
        } else if (
            opcode == BASL_OPCODE_NEW_INSTANCE ||
            opcode == BASL_OPCODE_NEW_ARRAY ||
            opcode == BASL_OPCODE_NEW_MAP ||
            opcode == BASL_OPCODE_DEFER_NEW_INSTANCE ||
            opcode == BASL_OPCODE_FORMAT_SPEC
        ) {
            uint32_t first_operand;
            uint32_t second_operand;

            if (offset + 8U >= chunk->code.length) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    opcode == BASL_OPCODE_NEW_INSTANCE ||
                            opcode == BASL_OPCODE_DEFER_NEW_INSTANCE
                        ? "truncated constructor instruction"
                        : "truncated collection instruction"
                );
                return BASL_STATUS_INTERNAL;
            }

            first_operand = (uint32_t)chunk->code.data[offset + 1U];
            first_operand |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            first_operand |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            first_operand |= (uint32_t)chunk->code.data[offset + 4U] << 24U;
            second_operand = (uint32_t)chunk->code.data[offset + 5U];
            second_operand |= (uint32_t)chunk->code.data[offset + 6U] << 8U;
            second_operand |= (uint32_t)chunk->code.data[offset + 7U] << 16U;
            second_operand |= (uint32_t)chunk->code.data[offset + 8U] << 24U;

            if (opcode == BASL_OPCODE_NEW_ARRAY || opcode == BASL_OPCODE_NEW_MAP) {
                written = snprintf(line, sizeof(line), " %u %u", first_operand, second_operand);
            } else {
                written = snprintf(line, sizeof(line), " %u %u", first_operand, second_operand);
            }
            if (written < 0) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    opcode == BASL_OPCODE_NEW_INSTANCE ||
                            opcode == BASL_OPCODE_DEFER_NEW_INSTANCE
                        ? "failed to format chunk constructor operand"
                        : "failed to format chunk collection operand"
                );
                return BASL_STATUS_INTERNAL;
            }

            status = basl_string_append(output, line, (size_t)written, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }

            offset += 9U;
        } else if (opcode == BASL_OPCODE_RETURN) {
            if (offset + 4U < chunk->code.length) {
                operand = (uint32_t)chunk->code.data[offset + 1U];
                operand |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
                operand |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
                operand |= (uint32_t)chunk->code.data[offset + 4U] << 24U;

                written = snprintf(line, sizeof(line), " %u", operand);
                if (written < 0) {
                    basl_error_set_literal(
                        error,
                        BASL_STATUS_INTERNAL,
                        "failed to format chunk return count"
                    );
                    return BASL_STATUS_INTERNAL;
                }

                status = basl_string_append(output, line, (size_t)written, error);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                offset += 5U;
            } else {
                offset += 1U;
            }
        } else if (
            opcode == BASL_OPCODE_CONSTANT ||
            opcode == BASL_OPCODE_GET_LOCAL ||
            opcode == BASL_OPCODE_SET_LOCAL ||
            opcode == BASL_OPCODE_GET_GLOBAL ||
            opcode == BASL_OPCODE_SET_GLOBAL ||
            opcode == BASL_OPCODE_GET_FUNCTION ||
            opcode == BASL_OPCODE_GET_CAPTURE ||
            opcode == BASL_OPCODE_SET_CAPTURE ||
            opcode == BASL_OPCODE_JUMP ||
            opcode == BASL_OPCODE_JUMP_IF_FALSE ||
            opcode == BASL_OPCODE_LOOP ||
            opcode == BASL_OPCODE_FORMAT_F64 ||
            opcode == BASL_OPCODE_GET_FIELD ||
            opcode == BASL_OPCODE_SET_FIELD
        ) {
            if (offset + 4U >= chunk->code.length) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "truncated constant instruction"
                );
                return BASL_STATUS_INTERNAL;
            }

            operand = (uint32_t)chunk->code.data[offset + 1U];
            operand |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            operand |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            operand |= (uint32_t)chunk->code.data[offset + 4U] << 24U;

            written = snprintf(line, sizeof(line), " %u", operand);
            if (written < 0) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "failed to format chunk constant index"
                );
                return BASL_STATUS_INTERNAL;
            }

            status = basl_string_append(output, line, (size_t)written, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }

            if (opcode == BASL_OPCODE_CONSTANT) {
                status = basl_chunk_append_text(output, " ", error);
                if (status != BASL_STATUS_OK) {
                    return status;
                }

                status = basl_chunk_append_value(
                    output,
                    basl_chunk_constant(chunk, (size_t)operand),
                    error
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
            }

            offset += 5U;
        } else if (
            opcode == BASL_OPCODE_GET_INDEX ||
            opcode == BASL_OPCODE_SET_INDEX ||
            opcode == BASL_OPCODE_GET_COLLECTION_SIZE ||
            opcode == BASL_OPCODE_GET_MAP_KEY_AT ||
            opcode == BASL_OPCODE_GET_MAP_VALUE_AT ||
            opcode == BASL_OPCODE_GET_STRING_SIZE ||
            opcode == BASL_OPCODE_STRING_CONTAINS ||
            opcode == BASL_OPCODE_STRING_STARTS_WITH ||
            opcode == BASL_OPCODE_STRING_ENDS_WITH ||
            opcode == BASL_OPCODE_STRING_TRIM ||
            opcode == BASL_OPCODE_STRING_TO_UPPER ||
            opcode == BASL_OPCODE_STRING_TO_LOWER ||
            opcode == BASL_OPCODE_STRING_REPLACE ||
            opcode == BASL_OPCODE_STRING_SPLIT ||
            opcode == BASL_OPCODE_STRING_INDEX_OF ||
            opcode == BASL_OPCODE_STRING_SUBSTR ||
            opcode == BASL_OPCODE_STRING_BYTES ||
            opcode == BASL_OPCODE_STRING_CHAR_AT ||
            opcode == BASL_OPCODE_ARRAY_PUSH ||
            opcode == BASL_OPCODE_ARRAY_POP ||
            opcode == BASL_OPCODE_ARRAY_GET_SAFE ||
            opcode == BASL_OPCODE_ARRAY_SET_SAFE ||
            opcode == BASL_OPCODE_ARRAY_SLICE ||
            opcode == BASL_OPCODE_ARRAY_CONTAINS ||
            opcode == BASL_OPCODE_MAP_GET_SAFE ||
            opcode == BASL_OPCODE_MAP_SET_SAFE ||
            opcode == BASL_OPCODE_MAP_REMOVE_SAFE ||
            opcode == BASL_OPCODE_MAP_HAS ||
            opcode == BASL_OPCODE_MAP_KEYS ||
            opcode == BASL_OPCODE_MAP_VALUES ||
            opcode == BASL_OPCODE_STRING_CHAR_COUNT
        ) {
            offset += 1U;
        } else {
            offset += 1U;
        }

        status = basl_chunk_append_text(output, "\n", error);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}
