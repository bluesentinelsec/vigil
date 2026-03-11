#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

extern "C" {
#include "basl/basl.h"
}

namespace {

struct AllocatorStats {
    int allocate_calls;
    int reallocate_calls;
    int deallocate_calls;
};

void *CountedAllocate(void *user_data, size_t size) {
    AllocatorStats *stats = static_cast<AllocatorStats *>(user_data);

    stats->allocate_calls += 1;
    return std::calloc(1U, size);
}

void *CountedReallocate(void *user_data, void *memory, size_t size) {
    AllocatorStats *stats = static_cast<AllocatorStats *>(user_data);

    stats->reallocate_calls += 1;
    return std::realloc(memory, size);
}

void CountedDeallocate(void *user_data, void *memory) {
    AllocatorStats *stats = static_cast<AllocatorStats *>(user_data);

    stats->deallocate_calls += 1;
    std::free(memory);
}

basl_source_span_t Span(basl_source_id_t source_id, size_t start, size_t end) {
    basl_source_span_t span = {};

    span.source_id = source_id;
    span.start_offset = start;
    span.end_offset = end;
    return span;
}

}  // namespace

TEST(BaslChunkTest, InitStartsEmpty) {
    basl_chunk_t chunk;

    basl_chunk_init(&chunk, nullptr);

    EXPECT_EQ(chunk.runtime, nullptr);
    EXPECT_EQ(basl_chunk_code_size(&chunk), 0U);
    EXPECT_EQ(basl_chunk_constant_count(&chunk), 0U);
    EXPECT_EQ(basl_chunk_code(&chunk), nullptr);
    EXPECT_EQ(basl_chunk_constant(&chunk, 0U), nullptr);
    EXPECT_EQ(basl_chunk_span_at(&chunk, 0U).source_id, 0U);
}

TEST(BaslChunkTest, WriteOpcodeAndBytesTrackSourceSpans) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_chunk_t chunk;
    const uint8_t *code;
    basl_source_span_t opcode_span = Span(1U, 10U, 11U);
    basl_source_span_t operand_span = Span(1U, 12U, 16U);

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);

    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, opcode_span, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_u32(&chunk, 0x78563412U, operand_span, &error),
        BASL_STATUS_OK
    );

    code = basl_chunk_code(&chunk);
    ASSERT_NE(code, nullptr);
    ASSERT_EQ(basl_chunk_code_size(&chunk), 5U);
    EXPECT_EQ(code[0], (uint8_t)BASL_OPCODE_RETURN);
    EXPECT_EQ(code[1], 0x12U);
    EXPECT_EQ(code[2], 0x34U);
    EXPECT_EQ(code[3], 0x56U);
    EXPECT_EQ(code[4], 0x78U);
    EXPECT_EQ(basl_chunk_span_at(&chunk, 0U).start_offset, 10U);
    EXPECT_EQ(basl_chunk_span_at(&chunk, 1U).start_offset, 12U);
    EXPECT_EQ(basl_chunk_span_at(&chunk, 4U).end_offset, 16U);

    basl_chunk_free(&chunk);
    basl_runtime_close(&runtime);
}

TEST(BaslChunkTest, AddConstantCopiesOwnedValueAndReleasesOnFree) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_chunk_t chunk;
    basl_object_t *object = nullptr;
    basl_value_t value;
    const basl_value_t *stored;
    size_t index = SIZE_MAX;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "hello", &object, &error),
        BASL_STATUS_OK
    );

    basl_value_init_object(&value, &object);
    ASSERT_EQ(object, nullptr);

    ASSERT_EQ(
        basl_chunk_add_constant(&chunk, &value, &index, &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(index, 0U);
    EXPECT_EQ(basl_chunk_constant_count(&chunk), 1U);
    stored = basl_chunk_constant(&chunk, index);
    ASSERT_NE(stored, nullptr);
    ASSERT_NE(basl_value_as_object(stored), nullptr);
    EXPECT_EQ(basl_object_ref_count(basl_value_as_object(stored)), 2U);
    EXPECT_STREQ(
        basl_string_object_c_str(basl_value_as_object(stored)),
        "hello"
    );

    basl_value_release(&value);
    EXPECT_EQ(basl_object_ref_count(basl_value_as_object(stored)), 1U);

    basl_chunk_free(&chunk);
    basl_runtime_close(&runtime);
}

TEST(BaslChunkTest, WriteConstantEncodesInstructionAndConstantIndex) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_chunk_t chunk;
    basl_value_t value;
    size_t index = SIZE_MAX;
    const uint8_t *code;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_int(&value, 42);

    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &value, Span(7U, 20U, 25U), &index, &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(index, 0U);
    ASSERT_EQ(basl_chunk_code_size(&chunk), 5U);
    code = basl_chunk_code(&chunk);
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code[0], (uint8_t)BASL_OPCODE_CONSTANT);
    EXPECT_EQ(code[1], 0U);
    EXPECT_EQ(code[2], 0U);
    EXPECT_EQ(code[3], 0U);
    EXPECT_EQ(code[4], 0U);
    EXPECT_EQ(basl_chunk_span_at(&chunk, 0U).source_id, 7U);
    EXPECT_EQ(basl_chunk_span_at(&chunk, 4U).end_offset, 25U);

    basl_chunk_free(&chunk);
    basl_runtime_close(&runtime);
}

TEST(BaslChunkTest, DisassembleFormatsOpcodesAndConstants) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_chunk_t chunk;
    basl_string_t output;
    basl_value_t value;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_string_init(&output, runtime);
    basl_value_init_int(&value, 123);

    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &value, Span(1U, 0U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(1U, 4U, 5U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(basl_chunk_disassemble(&chunk, &output, &error), BASL_STATUS_OK);

    EXPECT_NE(std::strstr(basl_string_c_str(&output), "0000 CONSTANT 0 123"), nullptr);
    EXPECT_NE(std::strstr(basl_string_c_str(&output), "0005 RETURN"), nullptr);

    basl_string_free(&output);
    basl_chunk_free(&chunk);
    basl_runtime_close(&runtime);
}

TEST(BaslChunkTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_chunk_t chunk;
    basl_value_t value;
    AllocatorStats stats = {};
    basl_allocator_t allocator = {};
    basl_runtime_options_t options = {};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_int(&value, 1);

    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &value, Span(1U, 0U, 1U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(1U, 2U, 3U), &error),
        BASL_STATUS_OK
    );

    EXPECT_GE(stats.allocate_calls, 3);

    basl_chunk_free(&chunk);
    EXPECT_GE(stats.deallocate_calls, 2);
    basl_runtime_close(&runtime);
}

TEST(BaslChunkTest, RejectsMissingRuntimeForMutation) {
    basl_chunk_t chunk;
    basl_error_t error = {};
    basl_value_t value;

    basl_chunk_init(&chunk, nullptr);
    basl_value_init_nil(&value);

    EXPECT_EQ(
        basl_chunk_add_constant(&chunk, &value, nullptr, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "chunk runtime must not be null"), 0);

    EXPECT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(0U, 0U, 0U), &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
}

TEST(BaslChunkTest, RejectsMissingArguments) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_chunk_t chunk;
    basl_string_t output;
    basl_value_t value;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_string_init(&output, runtime);
    basl_value_init_nil(&value);

    EXPECT_EQ(
        basl_chunk_add_constant(&chunk, nullptr, nullptr, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(
        std::strcmp(error.value, "chunk constant requires value and out_index"),
        0
    );

    basl_string_free(&output);
    basl_chunk_free(&chunk);
    basl_runtime_close(&runtime);
}
