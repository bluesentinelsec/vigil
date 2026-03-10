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
                if (basl_chunk_append_text(output, "\"", error) != BASL_STATUS_OK) {
                    return error == NULL ? BASL_STATUS_INTERNAL : error->type;
                }
                if (basl_string_append_cstr(
                        output,
                        basl_string_object_c_str(object),
                        error
                    ) != BASL_STATUS_OK) {
                    return error == NULL ? BASL_STATUS_INTERNAL : error->type;
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
    uint32_t constant_index;
    int written;
    basl_status_t status;

    status = basl_chunk_validate_output(chunk, output, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_string_clear(output);
    for (offset = 0U; offset < chunk->code.length; ) {
        written = snprintf(
            line,
            sizeof(line),
            "%04zu %s",
            offset,
            basl_opcode_name((basl_opcode_t)chunk->code.data[offset])
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

        if ((basl_opcode_t)chunk->code.data[offset] == BASL_OPCODE_CONSTANT) {
            if (offset + 4U >= chunk->code.length) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "truncated constant instruction"
                );
                return BASL_STATUS_INTERNAL;
            }

            constant_index = (uint32_t)chunk->code.data[offset + 1U];
            constant_index |= (uint32_t)chunk->code.data[offset + 2U] << 8U;
            constant_index |= (uint32_t)chunk->code.data[offset + 3U] << 16U;
            constant_index |= (uint32_t)chunk->code.data[offset + 4U] << 24U;

            written = snprintf(line, sizeof(line), " %u ", constant_index);
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

            status = basl_chunk_append_value(
                output,
                basl_chunk_constant(chunk, (size_t)constant_index),
                error
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            offset += 5U;
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
