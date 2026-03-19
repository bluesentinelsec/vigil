#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/chunk.h"

static vigil_status_t vigil_chunk_validate_mutable(const vigil_chunk_t *chunk, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (chunk == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "chunk must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "chunk runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_validate_output(const vigil_chunk_t *chunk, const vigil_string_t *output,
                                                  vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_chunk_validate_mutable(chunk, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (output == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "output string must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (output->bytes.runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "output string runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_grow_spans(vigil_chunk_t *chunk, size_t minimum_capacity, vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= chunk->span_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = chunk->span_capacity;
    next_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*chunk->spans)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk span allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (chunk->spans == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(chunk->runtime, next_capacity * sizeof(*chunk->spans), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else
    {
        memory = chunk->spans;
        status = vigil_runtime_realloc(chunk->runtime, &memory, next_capacity * sizeof(*chunk->spans), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        memset((vigil_source_span_t *)memory + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*chunk->spans));
    }

    chunk->spans = (vigil_source_span_t *)memory;
    chunk->span_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_grow_constants(vigil_chunk_t *chunk, size_t minimum_capacity, vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= chunk->constant_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = chunk->constant_capacity;
    next_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*chunk->constants)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk constant allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (chunk->constants == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(chunk->runtime, next_capacity * sizeof(*chunk->constants), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else
    {
        memory = chunk->constants;
        status = vigil_runtime_realloc(chunk->runtime, &memory, next_capacity * sizeof(*chunk->constants), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        memset((vigil_value_t *)memory + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*chunk->constants));
    }

    chunk->constants = (vigil_value_t *)memory;
    chunk->constant_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_append_bytes(vigil_chunk_t *chunk, const uint8_t *bytes, size_t byte_count,
                                               vigil_source_span_t span, vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_code_length;
    size_t old_span_count;
    size_t i;

    if (byte_count == 0U)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_code_length = chunk->code.length;
    old_span_count = chunk->span_count;
    if (byte_count > SIZE_MAX - old_code_length)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk code size overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (byte_count > SIZE_MAX - old_span_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk span table overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_byte_buffer_reserve(&chunk->code, old_code_length + byte_count, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    status = vigil_chunk_grow_spans(chunk, old_span_count + byte_count, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    memcpy(chunk->code.data + old_code_length, bytes, byte_count);
    chunk->code.length = old_code_length + byte_count;
    for (i = 0U; i < byte_count; ++i)
    {
        chunk->spans[old_span_count + i] = span;
    }

    chunk->span_count = old_span_count + byte_count;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_chunk_append_text(vigil_string_t *output, const char *text, vigil_error_t *error)
{
    return vigil_string_append_cstr(output, text, error);
}

static vigil_status_t vigil_chunk_append_value(vigil_string_t *output, const vigil_value_t *value, vigil_error_t *error)
{
    char buffer[64];
    int written;
    vigil_object_t *object;
    vigil_status_t status;

    if (value == NULL)
    {
        return vigil_chunk_append_text(output, "<null>", error);
    }

    switch (vigil_value_kind(value))
    {
    case VIGIL_VALUE_NIL:
        return vigil_chunk_append_text(output, "nil", error);
    case VIGIL_VALUE_BOOL:
        return vigil_chunk_append_text(output, vigil_value_as_bool(value) ? "true" : "false", error);
    case VIGIL_VALUE_INT:
        written = snprintf(buffer, sizeof(buffer), "%lld", (long long)vigil_value_as_int(value));
        break;
    case VIGIL_VALUE_UINT:
        written = snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)vigil_value_as_uint(value));
        break;
    case VIGIL_VALUE_FLOAT:
        written = snprintf(buffer, sizeof(buffer), "%.17g", vigil_value_as_float(value));
        break;
    case VIGIL_VALUE_OBJECT:
        object = vigil_value_as_object(value);
        if (vigil_object_type(object) == VIGIL_OBJECT_STRING)
        {
            status = vigil_chunk_append_text(output, "\"", error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            status = vigil_string_append_cstr(output, vigil_string_object_c_str(object), error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            return vigil_chunk_append_text(output, "\"", error);
        }

        return vigil_chunk_append_text(output, "<object>", error);
    default:
        return vigil_chunk_append_text(output, "<unknown>", error);
    }

    if (written < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format chunk value");
        return VIGIL_STATUS_INTERNAL;
    }

    return vigil_string_append(output, buffer, (size_t)written, error);
}

void vigil_chunk_init(vigil_chunk_t *chunk, vigil_runtime_t *runtime)
{
    if (chunk == NULL)
    {
        return;
    }

    memset(chunk, 0, sizeof(*chunk));
    chunk->runtime = runtime;
    vigil_byte_buffer_init(&chunk->code, runtime);
    vigil_debug_local_table_init(&chunk->debug_locals, runtime);
}

void vigil_chunk_clear(vigil_chunk_t *chunk)
{
    size_t i;

    if (chunk == NULL)
    {
        return;
    }

    vigil_byte_buffer_clear(&chunk->code);
    chunk->span_count = 0U;
    for (i = 0U; i < chunk->constant_count; ++i)
    {
        vigil_value_release(&chunk->constants[i]);
    }

    chunk->constant_count = 0U;
}

void vigil_chunk_free(vigil_chunk_t *chunk)
{
    void *memory;

    if (chunk == NULL)
    {
        return;
    }

    vigil_chunk_clear(chunk);
    vigil_byte_buffer_free(&chunk->code);

    memory = chunk->spans;
    if (chunk->runtime != NULL)
    {
        vigil_runtime_free(chunk->runtime, &memory);
    }

    memory = chunk->constants;
    if (chunk->runtime != NULL)
    {
        vigil_runtime_free(chunk->runtime, &memory);
    }

    vigil_debug_local_table_free(&chunk->debug_locals);

    memset(chunk, 0, sizeof(*chunk));
}

size_t vigil_chunk_code_size(const vigil_chunk_t *chunk)
{
    if (chunk == NULL)
    {
        return 0U;
    }

    return chunk->code.length;
}

const uint8_t *vigil_chunk_code(const vigil_chunk_t *chunk)
{
    if (chunk == NULL)
    {
        return NULL;
    }

    return chunk->code.data;
}

size_t vigil_chunk_constant_count(const vigil_chunk_t *chunk)
{
    if (chunk == NULL)
    {
        return 0U;
    }

    return chunk->constant_count;
}

const vigil_value_t *vigil_chunk_constant(const vigil_chunk_t *chunk, size_t index)
{
    if (chunk == NULL || index >= chunk->constant_count)
    {
        return NULL;
    }

    return &chunk->constants[index];
}

vigil_source_span_t vigil_chunk_span_at(const vigil_chunk_t *chunk, size_t offset)
{
    vigil_source_span_t span;

    vigil_source_span_clear(&span);
    if (chunk == NULL || offset >= chunk->span_count)
    {
        return span;
    }

    return chunk->spans[offset];
}

const char *vigil_opcode_name(vigil_opcode_t opcode)
{
    switch (opcode)
    {
    case VIGIL_OPCODE_CONSTANT:
        return "CONSTANT";
    case VIGIL_OPCODE_NIL:
        return "NIL";
    case VIGIL_OPCODE_TRUE:
        return "TRUE";
    case VIGIL_OPCODE_FALSE:
        return "FALSE";
    case VIGIL_OPCODE_RETURN:
        return "RETURN";
    case VIGIL_OPCODE_POP:
        return "POP";
    case VIGIL_OPCODE_DUP:
        return "DUP";
    case VIGIL_OPCODE_DUP_TWO:
        return "DUP_TWO";
    case VIGIL_OPCODE_GET_LOCAL:
        return "GET_LOCAL";
    case VIGIL_OPCODE_SET_LOCAL:
        return "SET_LOCAL";
    case VIGIL_OPCODE_GET_GLOBAL:
        return "GET_GLOBAL";
    case VIGIL_OPCODE_SET_GLOBAL:
        return "SET_GLOBAL";
    case VIGIL_OPCODE_GET_FUNCTION:
        return "GET_FUNCTION";
    case VIGIL_OPCODE_NEW_CLOSURE:
        return "NEW_CLOSURE";
    case VIGIL_OPCODE_GET_CAPTURE:
        return "GET_CAPTURE";
    case VIGIL_OPCODE_SET_CAPTURE:
        return "SET_CAPTURE";
    case VIGIL_OPCODE_JUMP:
        return "JUMP";
    case VIGIL_OPCODE_JUMP_IF_FALSE:
        return "JUMP_IF_FALSE";
    case VIGIL_OPCODE_LOOP:
        return "LOOP";
    case VIGIL_OPCODE_ADD:
        return "ADD";
    case VIGIL_OPCODE_SUBTRACT:
        return "SUBTRACT";
    case VIGIL_OPCODE_MULTIPLY:
        return "MULTIPLY";
    case VIGIL_OPCODE_DIVIDE:
        return "DIVIDE";
    case VIGIL_OPCODE_MODULO:
        return "MODULO";
    case VIGIL_OPCODE_BITWISE_AND:
        return "BITWISE_AND";
    case VIGIL_OPCODE_BITWISE_OR:
        return "BITWISE_OR";
    case VIGIL_OPCODE_BITWISE_XOR:
        return "BITWISE_XOR";
    case VIGIL_OPCODE_SHIFT_LEFT:
        return "SHIFT_LEFT";
    case VIGIL_OPCODE_SHIFT_RIGHT:
        return "SHIFT_RIGHT";
    case VIGIL_OPCODE_NEGATE:
        return "NEGATE";
    case VIGIL_OPCODE_NOT:
        return "NOT";
    case VIGIL_OPCODE_BITWISE_NOT:
        return "BITWISE_NOT";
    case VIGIL_OPCODE_TO_I32:
        return "TO_I32";
    case VIGIL_OPCODE_TO_I64:
        return "TO_I64";
    case VIGIL_OPCODE_TO_U8:
        return "TO_U8";
    case VIGIL_OPCODE_TO_U32:
        return "TO_U32";
    case VIGIL_OPCODE_TO_U64:
        return "TO_U64";
    case VIGIL_OPCODE_TO_F64:
        return "TO_F64";
    case VIGIL_OPCODE_TO_STRING:
        return "TO_STRING";
    case VIGIL_OPCODE_FORMAT_F64:
        return "FORMAT_F64";
    case VIGIL_OPCODE_NEW_ERROR:
        return "NEW_ERROR";
    case VIGIL_OPCODE_GET_ERROR_KIND:
        return "GET_ERROR_KIND";
    case VIGIL_OPCODE_GET_ERROR_MESSAGE:
        return "GET_ERROR_MESSAGE";
    case VIGIL_OPCODE_EQUAL:
        return "EQUAL";
    case VIGIL_OPCODE_GREATER:
        return "GREATER";
    case VIGIL_OPCODE_LESS:
        return "LESS";
    case VIGIL_OPCODE_CALL:
        return "CALL";
    case VIGIL_OPCODE_CALL_VALUE:
        return "CALL_VALUE";
    case VIGIL_OPCODE_NEW_INSTANCE:
        return "NEW_INSTANCE";
    case VIGIL_OPCODE_GET_FIELD:
        return "GET_FIELD";
    case VIGIL_OPCODE_SET_FIELD:
        return "SET_FIELD";
    case VIGIL_OPCODE_NEW_ARRAY:
        return "NEW_ARRAY";
    case VIGIL_OPCODE_NEW_MAP:
        return "NEW_MAP";
    case VIGIL_OPCODE_GET_INDEX:
        return "GET_INDEX";
    case VIGIL_OPCODE_SET_INDEX:
        return "SET_INDEX";
    case VIGIL_OPCODE_GET_COLLECTION_SIZE:
        return "GET_COLLECTION_SIZE";
    case VIGIL_OPCODE_GET_MAP_KEY_AT:
        return "GET_MAP_KEY_AT";
    case VIGIL_OPCODE_GET_MAP_VALUE_AT:
        return "GET_MAP_VALUE_AT";
    case VIGIL_OPCODE_CALL_INTERFACE:
        return "CALL_INTERFACE";
    case VIGIL_OPCODE_DEFER_CALL:
        return "DEFER_CALL";
    case VIGIL_OPCODE_DEFER_CALL_VALUE:
        return "DEFER_CALL_VALUE";
    case VIGIL_OPCODE_DEFER_NEW_INSTANCE:
        return "DEFER_NEW_INSTANCE";
    case VIGIL_OPCODE_DEFER_CALL_INTERFACE:
        return "DEFER_CALL_INTERFACE";
    case VIGIL_OPCODE_GET_STRING_SIZE:
        return "GET_STRING_SIZE";
    case VIGIL_OPCODE_STRING_CONTAINS:
        return "STRING_CONTAINS";
    case VIGIL_OPCODE_STRING_STARTS_WITH:
        return "STRING_STARTS_WITH";
    case VIGIL_OPCODE_STRING_ENDS_WITH:
        return "STRING_ENDS_WITH";
    case VIGIL_OPCODE_STRING_TRIM:
        return "STRING_TRIM";
    case VIGIL_OPCODE_STRING_TO_UPPER:
        return "STRING_TO_UPPER";
    case VIGIL_OPCODE_STRING_TO_LOWER:
        return "STRING_TO_LOWER";
    case VIGIL_OPCODE_STRING_REPLACE:
        return "STRING_REPLACE";
    case VIGIL_OPCODE_STRING_SPLIT:
        return "STRING_SPLIT";
    case VIGIL_OPCODE_STRING_INDEX_OF:
        return "STRING_INDEX_OF";
    case VIGIL_OPCODE_STRING_SUBSTR:
        return "STRING_SUBSTR";
    case VIGIL_OPCODE_STRING_BYTES:
        return "STRING_BYTES";
    case VIGIL_OPCODE_STRING_CHAR_AT:
        return "STRING_CHAR_AT";
    case VIGIL_OPCODE_ARRAY_PUSH:
        return "ARRAY_PUSH";
    case VIGIL_OPCODE_ARRAY_POP:
        return "ARRAY_POP";
    case VIGIL_OPCODE_ARRAY_GET_SAFE:
        return "ARRAY_GET_SAFE";
    case VIGIL_OPCODE_ARRAY_SET_SAFE:
        return "ARRAY_SET_SAFE";
    case VIGIL_OPCODE_ARRAY_SLICE:
        return "ARRAY_SLICE";
    case VIGIL_OPCODE_ARRAY_CONTAINS:
        return "ARRAY_CONTAINS";
    case VIGIL_OPCODE_MAP_GET_SAFE:
        return "MAP_GET_SAFE";
    case VIGIL_OPCODE_MAP_SET_SAFE:
        return "MAP_SET_SAFE";
    case VIGIL_OPCODE_MAP_REMOVE_SAFE:
        return "MAP_REMOVE_SAFE";
    case VIGIL_OPCODE_MAP_HAS:
        return "MAP_HAS";
    case VIGIL_OPCODE_MAP_KEYS:
        return "MAP_KEYS";
    case VIGIL_OPCODE_MAP_VALUES:
        return "MAP_VALUES";
    case VIGIL_OPCODE_ADD_I64:
        return "ADD_I64";
    case VIGIL_OPCODE_SUBTRACT_I64:
        return "SUBTRACT_I64";
    case VIGIL_OPCODE_LESS_I64:
        return "LESS_I64";
    case VIGIL_OPCODE_LESS_EQUAL_I64:
        return "LESS_EQUAL_I64";
    case VIGIL_OPCODE_GREATER_I64:
        return "GREATER_I64";
    case VIGIL_OPCODE_GREATER_EQUAL_I64:
        return "GREATER_EQUAL_I64";
    case VIGIL_OPCODE_MULTIPLY_I64:
        return "MULTIPLY_I64";
    case VIGIL_OPCODE_DIVIDE_I64:
        return "DIVIDE_I64";
    case VIGIL_OPCODE_MODULO_I64:
        return "MODULO_I64";
    case VIGIL_OPCODE_EQUAL_I64:
        return "EQUAL_I64";
    case VIGIL_OPCODE_NOT_EQUAL_I64:
        return "NOT_EQUAL_I64";
    case VIGIL_OPCODE_LOCALS_ADD_I64:
        return "LOCALS_ADD_I64";
    case VIGIL_OPCODE_LOCALS_SUBTRACT_I64:
        return "LOCALS_SUBTRACT_I64";
    case VIGIL_OPCODE_LOCALS_MULTIPLY_I64:
        return "LOCALS_MULTIPLY_I64";
    case VIGIL_OPCODE_LOCALS_MODULO_I64:
        return "LOCALS_MODULO_I64";
    case VIGIL_OPCODE_LOCALS_LESS_I64:
        return "LOCALS_LESS_I64";
    case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64:
        return "LOCALS_LESS_EQUAL_I64";
    case VIGIL_OPCODE_LOCALS_GREATER_I64:
        return "LOCALS_GREATER_I64";
    case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64:
        return "LOCALS_GREATER_EQUAL_I64";
    case VIGIL_OPCODE_LOCALS_EQUAL_I64:
        return "LOCALS_EQUAL_I64";
    case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64:
        return "LOCALS_NOT_EQUAL_I64";
    case VIGIL_OPCODE_ADD_I32:
        return "ADD_I32";
    case VIGIL_OPCODE_SUBTRACT_I32:
        return "SUBTRACT_I32";
    case VIGIL_OPCODE_MULTIPLY_I32:
        return "MULTIPLY_I32";
    case VIGIL_OPCODE_LESS_I32:
        return "LESS_I32";
    case VIGIL_OPCODE_LESS_EQUAL_I32:
        return "LESS_EQUAL_I32";
    case VIGIL_OPCODE_GREATER_I32:
        return "GREATER_I32";
    case VIGIL_OPCODE_GREATER_EQUAL_I32:
        return "GREATER_EQUAL_I32";
    case VIGIL_OPCODE_EQUAL_I32:
        return "EQUAL_I32";
    case VIGIL_OPCODE_NOT_EQUAL_I32:
        return "NOT_EQUAL_I32";
    case VIGIL_OPCODE_MODULO_I32:
        return "MODULO_I32";
    case VIGIL_OPCODE_DIVIDE_I32:
        return "DIVIDE_I32";
    case VIGIL_OPCODE_LOCALS_ADD_I32_STORE:
        return "LOCALS_ADD_I32_STORE";
    case VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE:
        return "LOCALS_SUBTRACT_I32_STORE";
    case VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE:
        return "LOCALS_MULTIPLY_I32_STORE";
    case VIGIL_OPCODE_LOCALS_LESS_I32_STORE:
        return "LOCALS_LESS_I32_STORE";
    case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE:
        return "LOCALS_LESS_EQUAL_I32_STORE";
    case VIGIL_OPCODE_LOCALS_GREATER_I32_STORE:
        return "LOCALS_GREATER_I32_STORE";
    case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE:
        return "LOCALS_GREATER_EQUAL_I32_STORE";
    case VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE:
        return "LOCALS_EQUAL_I32_STORE";
    case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE:
        return "LOCALS_NOT_EQUAL_I32_STORE";
    case VIGIL_OPCODE_LOCALS_MODULO_I32_STORE:
        return "LOCALS_MODULO_I32_STORE";
    case VIGIL_OPCODE_INCREMENT_LOCAL_I32:
        return "INCREMENT_LOCAL_I32";
    case VIGIL_OPCODE_TAIL_CALL:
        return "TAIL_CALL";
    case VIGIL_OPCODE_FORLOOP_I32:
        return "FORLOOP_I32";
    case VIGIL_OPCODE_CALL_NATIVE:
        return "CALL_NATIVE";
    case VIGIL_OPCODE_DEFER_CALL_NATIVE:
        return "DEFER_CALL_NATIVE";
    case VIGIL_OPCODE_STRING_TRIM_LEFT:
        return "STRING_TRIM_LEFT";
    case VIGIL_OPCODE_STRING_TRIM_RIGHT:
        return "STRING_TRIM_RIGHT";
    case VIGIL_OPCODE_STRING_REPEAT:
        return "STRING_REPEAT";
    case VIGIL_OPCODE_STRING_REVERSE:
        return "STRING_REVERSE";
    case VIGIL_OPCODE_STRING_IS_EMPTY:
        return "STRING_IS_EMPTY";
    case VIGIL_OPCODE_STRING_COUNT:
        return "STRING_COUNT";
    case VIGIL_OPCODE_STRING_LAST_INDEX_OF:
        return "STRING_LAST_INDEX_OF";
    case VIGIL_OPCODE_STRING_TRIM_PREFIX:
        return "STRING_TRIM_PREFIX";
    case VIGIL_OPCODE_STRING_TRIM_SUFFIX:
        return "STRING_TRIM_SUFFIX";
    case VIGIL_OPCODE_CHAR_FROM_INT:
        return "CHAR_FROM_INT";
    case VIGIL_OPCODE_STRING_TO_C:
        return "STRING_TO_C";
    case VIGIL_OPCODE_STRING_JOIN:
        return "STRING_JOIN";
    case VIGIL_OPCODE_STRING_CUT:
        return "STRING_CUT";
    case VIGIL_OPCODE_STRING_FIELDS:
        return "STRING_FIELDS";
    case VIGIL_OPCODE_STRING_EQUAL_FOLD:
        return "STRING_EQUAL_FOLD";
    case VIGIL_OPCODE_STRING_CHAR_COUNT:
        return "STRING_CHAR_COUNT";
    case VIGIL_OPCODE_FORMAT_SPEC:
        return "FORMAT_SPEC";
    default:
        return "UNKNOWN";
    }
}

vigil_status_t vigil_chunk_write_byte(vigil_chunk_t *chunk, uint8_t value, vigil_source_span_t span,
                                      vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_chunk_validate_mutable(chunk, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return vigil_chunk_append_bytes(chunk, &value, 1U, span, error);
}

vigil_status_t vigil_chunk_write_opcode(vigil_chunk_t *chunk, vigil_opcode_t opcode, vigil_source_span_t span,
                                        vigil_error_t *error)
{
    return vigil_chunk_write_byte(chunk, (uint8_t)opcode, span, error);
}

vigil_status_t vigil_chunk_write_u32(vigil_chunk_t *chunk, uint32_t value, vigil_source_span_t span,
                                     vigil_error_t *error)
{
    uint8_t encoded[4];
    vigil_status_t status;

    status = vigil_chunk_validate_mutable(chunk, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    encoded[0] = (uint8_t)(value & 0xffU);
    encoded[1] = (uint8_t)((value >> 8U) & 0xffU);
    encoded[2] = (uint8_t)((value >> 16U) & 0xffU);
    encoded[3] = (uint8_t)((value >> 24U) & 0xffU);
    return vigil_chunk_append_bytes(chunk, encoded, sizeof(encoded), span, error);
}

vigil_status_t vigil_chunk_add_constant(vigil_chunk_t *chunk, const vigil_value_t *value, size_t *out_index,
                                        vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_chunk_validate_mutable(chunk, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (value == NULL || out_index == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "chunk constant requires value and out_index");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk->constant_count == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk constant table overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_chunk_grow_constants(chunk, chunk->constant_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    chunk->constants[chunk->constant_count] = vigil_value_copy(value);
    *out_index = chunk->constant_count;
    chunk->constant_count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_chunk_write_constant(vigil_chunk_t *chunk, const vigil_value_t *value, vigil_source_span_t span,
                                          size_t *out_index, vigil_error_t *error)
{
    vigil_status_t status;
    size_t index;
    uint8_t encoded[5];

    status = vigil_chunk_add_constant(chunk, value, &index, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (index > UINT32_MAX)
    {
        vigil_value_release(&chunk->constants[chunk->constant_count - 1U]);
        chunk->constant_count -= 1U;
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "chunk constant index overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    encoded[0] = (uint8_t)VIGIL_OPCODE_CONSTANT;
    encoded[1] = (uint8_t)((uint32_t)index & 0xffU);
    encoded[2] = (uint8_t)(((uint32_t)index >> 8U) & 0xffU);
    encoded[3] = (uint8_t)(((uint32_t)index >> 16U) & 0xffU);
    encoded[4] = (uint8_t)(((uint32_t)index >> 24U) & 0xffU);
    status = vigil_chunk_append_bytes(chunk, encoded, sizeof(encoded), span, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_value_release(&chunk->constants[chunk->constant_count - 1U]);
        chunk->constant_count -= 1U;
        return status;
    }

    if (out_index != NULL)
    {
        *out_index = index;
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_chunk_disassemble(const vigil_chunk_t *chunk, vigil_string_t *output, vigil_error_t *error)
{
    size_t offset;
    char line[64];
    uint32_t operand;
    int written;
    vigil_status_t status;
    vigil_opcode_t opcode;

    status = vigil_chunk_validate_output(chunk, output, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_string_clear(output);
    for (offset = 0U; offset < chunk->code.length;)
    {
        opcode = (vigil_opcode_t)chunk->code.data[offset];
        written = snprintf(line, sizeof(line), "%04zu %s", offset, vigil_opcode_name(opcode));
        if (written < 0)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format chunk disassembly");
            return VIGIL_STATUS_INTERNAL;
        }

        status = vigil_string_append(output, line, (size_t)written, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        if (opcode == VIGIL_OPCODE_CALL || opcode == VIGIL_OPCODE_DEFER_CALL)
        {
            uint32_t function_index;
            uint32_t arg_count;

            if (offset + 8U >= chunk->code.length)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "truncated call instruction");
                return VIGIL_STATUS_INTERNAL;
            }

            function_index = (uint32_t)chunk->code.data[offset + 1U];
            function_index |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            function_index |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            function_index |= (uint32_t)chunk->code.data[offset + 4U] << 24U;
            arg_count = (uint32_t)chunk->code.data[offset + 5U];
            arg_count |= (uint32_t)chunk->code.data[offset + 6U] << 8U;
            arg_count |= (uint32_t)chunk->code.data[offset + 7U] << 16U;
            arg_count |= (uint32_t)chunk->code.data[offset + 8U] << 24U;

            written = snprintf(line, sizeof(line), " %u %u", function_index, arg_count);
            if (written < 0)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format chunk call operand");
                return VIGIL_STATUS_INTERNAL;
            }

            status = vigil_string_append(output, line, (size_t)written, error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            offset += 9U;
        }
        else if (opcode == VIGIL_OPCODE_CALL_VALUE || opcode == VIGIL_OPCODE_DEFER_CALL_VALUE)
        {
            if (offset + 4U >= chunk->code.length)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "truncated indirect call instruction");
                return VIGIL_STATUS_INTERNAL;
            }

            operand = (uint32_t)chunk->code.data[offset + 1U];
            operand |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            operand |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            operand |= (uint32_t)chunk->code.data[offset + 4U] << 24U;

            written = snprintf(line, sizeof(line), " %u", operand);
            if (written < 0)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format chunk indirect call operand");
                return VIGIL_STATUS_INTERNAL;
            }

            status = vigil_string_append(output, line, (size_t)written, error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            offset += 5U;
        }
        else if (opcode == VIGIL_OPCODE_NEW_CLOSURE)
        {
            uint32_t function_index;
            uint32_t capture_count;

            if (offset + 8U >= chunk->code.length)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "truncated closure instruction");
                return VIGIL_STATUS_INTERNAL;
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
            if (written < 0)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format chunk closure operand");
                return VIGIL_STATUS_INTERNAL;
            }

            status = vigil_string_append(output, line, (size_t)written, error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            offset += 9U;
        }
        else if (opcode == VIGIL_OPCODE_CALL_INTERFACE || opcode == VIGIL_OPCODE_DEFER_CALL_INTERFACE)
        {
            uint32_t interface_index;
            uint32_t method_index;
            uint32_t arg_count;

            if (offset + 12U >= chunk->code.length)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "truncated interface call instruction");
                return VIGIL_STATUS_INTERNAL;
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

            written = snprintf(line, sizeof(line), " %u %u %u", interface_index, method_index, arg_count);
            if (written < 0)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format chunk interface call operand");
                return VIGIL_STATUS_INTERNAL;
            }

            status = vigil_string_append(output, line, (size_t)written, error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            offset += 13U;
        }
        else if (opcode == VIGIL_OPCODE_NEW_INSTANCE || opcode == VIGIL_OPCODE_NEW_ARRAY ||
                 opcode == VIGIL_OPCODE_NEW_MAP || opcode == VIGIL_OPCODE_DEFER_NEW_INSTANCE ||
                 opcode == VIGIL_OPCODE_FORMAT_SPEC)
        {
            uint32_t first_operand;
            uint32_t second_operand;

            if (offset + 8U >= chunk->code.length)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL,
                                        opcode == VIGIL_OPCODE_NEW_INSTANCE || opcode == VIGIL_OPCODE_DEFER_NEW_INSTANCE
                                            ? "truncated constructor instruction"
                                            : "truncated collection instruction");
                return VIGIL_STATUS_INTERNAL;
            }

            first_operand = (uint32_t)chunk->code.data[offset + 1U];
            first_operand |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            first_operand |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            first_operand |= (uint32_t)chunk->code.data[offset + 4U] << 24U;
            second_operand = (uint32_t)chunk->code.data[offset + 5U];
            second_operand |= (uint32_t)chunk->code.data[offset + 6U] << 8U;
            second_operand |= (uint32_t)chunk->code.data[offset + 7U] << 16U;
            second_operand |= (uint32_t)chunk->code.data[offset + 8U] << 24U;

            if (opcode == VIGIL_OPCODE_NEW_ARRAY || opcode == VIGIL_OPCODE_NEW_MAP)
            {
                written = snprintf(line, sizeof(line), " %u %u", first_operand, second_operand);
            }
            else
            {
                written = snprintf(line, sizeof(line), " %u %u", first_operand, second_operand);
            }
            if (written < 0)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL,
                                        opcode == VIGIL_OPCODE_NEW_INSTANCE || opcode == VIGIL_OPCODE_DEFER_NEW_INSTANCE
                                            ? "failed to format chunk constructor operand"
                                            : "failed to format chunk collection operand");
                return VIGIL_STATUS_INTERNAL;
            }

            status = vigil_string_append(output, line, (size_t)written, error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            offset += 9U;
        }
        else if (opcode == VIGIL_OPCODE_RETURN)
        {
            if (offset + 4U < chunk->code.length)
            {
                operand = (uint32_t)chunk->code.data[offset + 1U];
                operand |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
                operand |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
                operand |= (uint32_t)chunk->code.data[offset + 4U] << 24U;

                written = snprintf(line, sizeof(line), " %u", operand);
                if (written < 0)
                {
                    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format chunk return count");
                    return VIGIL_STATUS_INTERNAL;
                }

                status = vigil_string_append(output, line, (size_t)written, error);
                if (status != VIGIL_STATUS_OK)
                {
                    return status;
                }
                offset += 5U;
            }
            else
            {
                offset += 1U;
            }
        }
        else if (opcode == VIGIL_OPCODE_CONSTANT || opcode == VIGIL_OPCODE_GET_LOCAL ||
                 opcode == VIGIL_OPCODE_SET_LOCAL || opcode == VIGIL_OPCODE_GET_GLOBAL ||
                 opcode == VIGIL_OPCODE_SET_GLOBAL || opcode == VIGIL_OPCODE_GET_FUNCTION ||
                 opcode == VIGIL_OPCODE_GET_CAPTURE || opcode == VIGIL_OPCODE_SET_CAPTURE ||
                 opcode == VIGIL_OPCODE_JUMP || opcode == VIGIL_OPCODE_JUMP_IF_FALSE || opcode == VIGIL_OPCODE_LOOP ||
                 opcode == VIGIL_OPCODE_FORMAT_F64 || opcode == VIGIL_OPCODE_GET_FIELD ||
                 opcode == VIGIL_OPCODE_SET_FIELD)
        {
            if (offset + 4U >= chunk->code.length)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "truncated constant instruction");
                return VIGIL_STATUS_INTERNAL;
            }

            operand = (uint32_t)chunk->code.data[offset + 1U];
            operand |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            operand |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            operand |= (uint32_t)chunk->code.data[offset + 4U] << 24U;

            written = snprintf(line, sizeof(line), " %u", operand);
            if (written < 0)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format chunk constant index");
                return VIGIL_STATUS_INTERNAL;
            }

            status = vigil_string_append(output, line, (size_t)written, error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }

            if (opcode == VIGIL_OPCODE_CONSTANT)
            {
                status = vigil_chunk_append_text(output, " ", error);
                if (status != VIGIL_STATUS_OK)
                {
                    return status;
                }

                status = vigil_chunk_append_value(output, vigil_chunk_constant(chunk, (size_t)operand), error);
                if (status != VIGIL_STATUS_OK)
                {
                    return status;
                }
            }

            offset += 5U;
        }
        else if (opcode == VIGIL_OPCODE_GET_INDEX || opcode == VIGIL_OPCODE_SET_INDEX ||
                 opcode == VIGIL_OPCODE_GET_COLLECTION_SIZE || opcode == VIGIL_OPCODE_GET_MAP_KEY_AT ||
                 opcode == VIGIL_OPCODE_GET_MAP_VALUE_AT || opcode == VIGIL_OPCODE_GET_STRING_SIZE ||
                 opcode == VIGIL_OPCODE_STRING_CONTAINS || opcode == VIGIL_OPCODE_STRING_STARTS_WITH ||
                 opcode == VIGIL_OPCODE_STRING_ENDS_WITH || opcode == VIGIL_OPCODE_STRING_TRIM ||
                 opcode == VIGIL_OPCODE_STRING_TO_UPPER || opcode == VIGIL_OPCODE_STRING_TO_LOWER ||
                 opcode == VIGIL_OPCODE_STRING_REPLACE || opcode == VIGIL_OPCODE_STRING_SPLIT ||
                 opcode == VIGIL_OPCODE_STRING_INDEX_OF || opcode == VIGIL_OPCODE_STRING_SUBSTR ||
                 opcode == VIGIL_OPCODE_STRING_BYTES || opcode == VIGIL_OPCODE_STRING_CHAR_AT ||
                 opcode == VIGIL_OPCODE_ARRAY_PUSH || opcode == VIGIL_OPCODE_ARRAY_POP ||
                 opcode == VIGIL_OPCODE_ARRAY_GET_SAFE || opcode == VIGIL_OPCODE_ARRAY_SET_SAFE ||
                 opcode == VIGIL_OPCODE_ARRAY_SLICE || opcode == VIGIL_OPCODE_ARRAY_CONTAINS ||
                 opcode == VIGIL_OPCODE_MAP_GET_SAFE || opcode == VIGIL_OPCODE_MAP_SET_SAFE ||
                 opcode == VIGIL_OPCODE_MAP_REMOVE_SAFE || opcode == VIGIL_OPCODE_MAP_HAS ||
                 opcode == VIGIL_OPCODE_MAP_KEYS || opcode == VIGIL_OPCODE_MAP_VALUES ||
                 opcode == VIGIL_OPCODE_STRING_CHAR_COUNT)
        {
            offset += 1U;
        }
        else
        {
            offset += 1U;
        }

        status = vigil_chunk_append_text(output, "\n", error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}
