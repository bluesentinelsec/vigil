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
    BASL_OPCODE_DUP_TWO = 7,
    BASL_OPCODE_GET_LOCAL = 8,
    BASL_OPCODE_SET_LOCAL = 9,
    BASL_OPCODE_GET_GLOBAL = 10,
    BASL_OPCODE_SET_GLOBAL = 11,
    BASL_OPCODE_GET_FUNCTION = 12,
    BASL_OPCODE_NEW_CLOSURE = 13,
    BASL_OPCODE_GET_CAPTURE = 14,
    BASL_OPCODE_SET_CAPTURE = 15,
    BASL_OPCODE_JUMP = 16,
    BASL_OPCODE_JUMP_IF_FALSE = 17,
    BASL_OPCODE_LOOP = 18,
    BASL_OPCODE_ADD = 19,
    BASL_OPCODE_SUBTRACT = 20,
    BASL_OPCODE_MULTIPLY = 21,
    BASL_OPCODE_DIVIDE = 22,
    BASL_OPCODE_MODULO = 23,
    BASL_OPCODE_BITWISE_AND = 24,
    BASL_OPCODE_BITWISE_OR = 25,
    BASL_OPCODE_BITWISE_XOR = 26,
    BASL_OPCODE_SHIFT_LEFT = 27,
    BASL_OPCODE_SHIFT_RIGHT = 28,
    BASL_OPCODE_NEGATE = 29,
    BASL_OPCODE_NOT = 30,
    BASL_OPCODE_BITWISE_NOT = 31,
    BASL_OPCODE_TO_I32 = 32,
    BASL_OPCODE_TO_I64 = 33,
    BASL_OPCODE_TO_U8 = 34,
    BASL_OPCODE_TO_U32 = 35,
    BASL_OPCODE_TO_U64 = 36,
    BASL_OPCODE_TO_F64 = 37,
    BASL_OPCODE_TO_STRING = 38,
    BASL_OPCODE_FORMAT_F64 = 39,
    BASL_OPCODE_NEW_ERROR = 40,
    BASL_OPCODE_GET_ERROR_KIND = 41,
    BASL_OPCODE_GET_ERROR_MESSAGE = 42,
    BASL_OPCODE_EQUAL = 43,
    BASL_OPCODE_GREATER = 44,
    BASL_OPCODE_LESS = 45,
    BASL_OPCODE_CALL = 46,
    BASL_OPCODE_CALL_VALUE = 47,
    BASL_OPCODE_NEW_INSTANCE = 48,
    BASL_OPCODE_GET_FIELD = 49,
    BASL_OPCODE_SET_FIELD = 50,
    BASL_OPCODE_NEW_ARRAY = 51,
    BASL_OPCODE_NEW_MAP = 52,
    BASL_OPCODE_GET_INDEX = 53,
    BASL_OPCODE_SET_INDEX = 54,
    BASL_OPCODE_GET_COLLECTION_SIZE = 55,
    BASL_OPCODE_GET_MAP_KEY_AT = 56,
    BASL_OPCODE_GET_MAP_VALUE_AT = 57,
    BASL_OPCODE_CALL_INTERFACE = 58,
    BASL_OPCODE_DEFER_CALL = 59,
    BASL_OPCODE_DEFER_CALL_VALUE = 60,
    BASL_OPCODE_DEFER_NEW_INSTANCE = 61,
    BASL_OPCODE_DEFER_CALL_INTERFACE = 62,
    BASL_OPCODE_GET_STRING_SIZE = 63,
    BASL_OPCODE_STRING_CONTAINS = 64,
    BASL_OPCODE_STRING_STARTS_WITH = 65,
    BASL_OPCODE_STRING_ENDS_WITH = 66,
    BASL_OPCODE_STRING_TRIM = 67,
    BASL_OPCODE_STRING_TO_UPPER = 68,
    BASL_OPCODE_STRING_TO_LOWER = 69,
    BASL_OPCODE_STRING_REPLACE = 70,
    BASL_OPCODE_STRING_SPLIT = 71,
    BASL_OPCODE_STRING_INDEX_OF = 72,
    BASL_OPCODE_STRING_SUBSTR = 73,
    BASL_OPCODE_STRING_BYTES = 74,
    BASL_OPCODE_STRING_CHAR_AT = 75,
    BASL_OPCODE_ARRAY_PUSH = 76,
    BASL_OPCODE_ARRAY_POP = 77,
    BASL_OPCODE_ARRAY_GET_SAFE = 78,
    BASL_OPCODE_ARRAY_SET_SAFE = 79,
    BASL_OPCODE_ARRAY_SLICE = 80,
    BASL_OPCODE_ARRAY_CONTAINS = 81,
    BASL_OPCODE_MAP_GET_SAFE = 82,
    BASL_OPCODE_MAP_SET_SAFE = 83,
    BASL_OPCODE_MAP_REMOVE_SAFE = 84,
    BASL_OPCODE_MAP_HAS = 85,
    BASL_OPCODE_MAP_KEYS = 86,
    BASL_OPCODE_MAP_VALUES = 87
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
