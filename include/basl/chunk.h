#ifndef BASL_CHUNK_H
#define BASL_CHUNK_H

#include <stddef.h>
#include <stdint.h>

#include "basl/array.h"
#include "basl/export.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/string.h"
#include "basl/value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum basl_opcode {
    BASL_OPCODE_CONSTANT = 0,
    BASL_OPCODE_NIL = 1,
    BASL_OPCODE_TRUE = 2,
    BASL_OPCODE_FALSE = 3,
    BASL_OPCODE_RETURN = 4,
    BASL_OPCODE_POP = 5,
    BASL_OPCODE_DUP = 6,
    BASL_OPCODE_GET_LOCAL = 7,
    BASL_OPCODE_SET_LOCAL = 8,
    BASL_OPCODE_GET_GLOBAL = 9,
    BASL_OPCODE_SET_GLOBAL = 10,
    BASL_OPCODE_JUMP = 11,
    BASL_OPCODE_JUMP_IF_FALSE = 12,
    BASL_OPCODE_LOOP = 13,
    BASL_OPCODE_ADD = 14,
    BASL_OPCODE_SUBTRACT = 15,
    BASL_OPCODE_MULTIPLY = 16,
    BASL_OPCODE_DIVIDE = 17,
    BASL_OPCODE_MODULO = 18,
    BASL_OPCODE_BITWISE_AND = 19,
    BASL_OPCODE_BITWISE_OR = 20,
    BASL_OPCODE_BITWISE_XOR = 21,
    BASL_OPCODE_SHIFT_LEFT = 22,
    BASL_OPCODE_SHIFT_RIGHT = 23,
    BASL_OPCODE_NEGATE = 24,
    BASL_OPCODE_NOT = 25,
    BASL_OPCODE_BITWISE_NOT = 26,
    BASL_OPCODE_TO_I32 = 27,
    BASL_OPCODE_TO_F64 = 28,
    BASL_OPCODE_TO_STRING = 29,
    BASL_OPCODE_NEW_ERROR = 30,
    BASL_OPCODE_GET_ERROR_KIND = 31,
    BASL_OPCODE_GET_ERROR_MESSAGE = 32,
    BASL_OPCODE_EQUAL = 33,
    BASL_OPCODE_GREATER = 34,
    BASL_OPCODE_LESS = 35,
    BASL_OPCODE_CALL = 36,
    BASL_OPCODE_NEW_INSTANCE = 37,
    BASL_OPCODE_GET_FIELD = 38,
    BASL_OPCODE_SET_FIELD = 39,
    BASL_OPCODE_CALL_INTERFACE = 40,
    BASL_OPCODE_DEFER_CALL = 41,
    BASL_OPCODE_DEFER_NEW_INSTANCE = 42,
    BASL_OPCODE_DEFER_CALL_INTERFACE = 43
} basl_opcode_t;

typedef struct basl_chunk {
    basl_runtime_t *runtime;
    basl_byte_buffer_t code;
    basl_source_span_t *spans;
    size_t span_count;
    size_t span_capacity;
    basl_value_t *constants;
    size_t constant_count;
    size_t constant_capacity;
} basl_chunk_t;

BASL_API void basl_chunk_init(basl_chunk_t *chunk, basl_runtime_t *runtime);
BASL_API void basl_chunk_clear(basl_chunk_t *chunk);
BASL_API void basl_chunk_free(basl_chunk_t *chunk);
BASL_API size_t basl_chunk_code_size(const basl_chunk_t *chunk);
BASL_API const uint8_t *basl_chunk_code(const basl_chunk_t *chunk);
BASL_API size_t basl_chunk_constant_count(const basl_chunk_t *chunk);
/*
 * The returned pointer is invalidated by any subsequent chunk mutation or
 * lifetime operation that can reallocate or clear the constant pool.
 */
BASL_API const basl_value_t *basl_chunk_constant(
    const basl_chunk_t *chunk,
    size_t index
);
BASL_API basl_source_span_t basl_chunk_span_at(
    const basl_chunk_t *chunk,
    size_t offset
);
BASL_API const char *basl_opcode_name(basl_opcode_t opcode);
BASL_API basl_status_t basl_chunk_write_byte(
    basl_chunk_t *chunk,
    uint8_t value,
    basl_source_span_t span,
    basl_error_t *error
);
BASL_API basl_status_t basl_chunk_write_opcode(
    basl_chunk_t *chunk,
    basl_opcode_t opcode,
    basl_source_span_t span,
    basl_error_t *error
);
BASL_API basl_status_t basl_chunk_write_u32(
    basl_chunk_t *chunk,
    uint32_t value,
    basl_source_span_t span,
    basl_error_t *error
);
BASL_API basl_status_t basl_chunk_add_constant(
    basl_chunk_t *chunk,
    const basl_value_t *value,
    size_t *out_index,
    basl_error_t *error
);
BASL_API basl_status_t basl_chunk_write_constant(
    basl_chunk_t *chunk,
    const basl_value_t *value,
    basl_source_span_t span,
    size_t *out_index,
    basl_error_t *error
);
BASL_API basl_status_t basl_chunk_disassemble(
    const basl_chunk_t *chunk,
    basl_string_t *output,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
