#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#include "vigil/vigil.h"

struct AllocatorStats
{
    int allocate_calls;
    int reallocate_calls;
    int deallocate_calls;
};

static void *CountedAllocate(void *user_data, size_t size)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->allocate_calls += 1;
    return calloc(1U, size);
}

static void *CountedReallocate(void *user_data, void *memory, size_t size)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->reallocate_calls += 1;
    return realloc(memory, size);
}

static void CountedDeallocate(void *user_data, void *memory)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->deallocate_calls += 1;
    free(memory);
}

static vigil_source_span_t Span(vigil_source_id_t source_id, size_t start, size_t end)
{
    vigil_source_span_t span = {0};

    span.source_id = source_id;
    span.start_offset = start;
    span.end_offset = end;
    return span;
}

TEST(VigilChunkTest, InitStartsEmpty)
{
    vigil_chunk_t chunk;

    vigil_chunk_init(&chunk, NULL);

    EXPECT_EQ(chunk.runtime, NULL);
    EXPECT_EQ(vigil_chunk_code_size(&chunk), 0U);
    EXPECT_EQ(vigil_chunk_constant_count(&chunk), 0U);
    EXPECT_EQ(vigil_chunk_code(&chunk), NULL);
    EXPECT_EQ(vigil_chunk_constant(&chunk, 0U), NULL);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 0U).source_id, 0U);
}

TEST(VigilChunkTest, WriteOpcodeAndBytesTrackSourceSpans)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    const uint8_t *code;
    vigil_source_span_t opcode_span = Span(1U, 10U, 11U);
    vigil_source_span_t operand_span = Span(1U, 12U, 16U);

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);

    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, opcode_span, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0x78563412U, operand_span, &error), VIGIL_STATUS_OK);

    code = vigil_chunk_code(&chunk);
    ASSERT_NE(code, NULL);
    ASSERT_EQ(vigil_chunk_code_size(&chunk), 5U);
    EXPECT_EQ(code[0], (uint8_t)VIGIL_OPCODE_RETURN);
    EXPECT_EQ(code[1], 0x12U);
    EXPECT_EQ(code[2], 0x34U);
    EXPECT_EQ(code[3], 0x56U);
    EXPECT_EQ(code[4], 0x78U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 0U).start_offset, 10U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 1U).start_offset, 12U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 4U).end_offset, 16U);

    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, AddConstantCopiesOwnedValueAndReleasesOnFree)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_object_t *object = NULL;
    vigil_value_t value;
    const vigil_value_t *stored;
    size_t index = SIZE_MAX;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "hello", &object, &error), VIGIL_STATUS_OK);

    vigil_value_init_object(&value, &object);
    ASSERT_EQ(object, NULL);

    ASSERT_EQ(vigil_chunk_add_constant(&chunk, &value, &index, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(index, 0U);
    EXPECT_EQ(vigil_chunk_constant_count(&chunk), 1U);
    stored = vigil_chunk_constant(&chunk, index);
    ASSERT_NE(stored, NULL);
    ASSERT_NE(vigil_value_as_object(stored), NULL);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(stored)), 2U);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(stored)), "hello");

    vigil_value_release(&value);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(stored)), 1U);

    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, WriteConstantEncodesInstructionAndConstantIndex)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_value_t value;
    size_t index = SIZE_MAX;
    const uint8_t *code;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&value, 42);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &value, Span(7U, 20U, 25U), &index, &error), VIGIL_STATUS_OK);

    ASSERT_EQ(index, 0U);
    ASSERT_EQ(vigil_chunk_code_size(&chunk), 5U);
    code = vigil_chunk_code(&chunk);
    ASSERT_NE(code, NULL);
    EXPECT_EQ(code[0], (uint8_t)VIGIL_OPCODE_CONSTANT);
    EXPECT_EQ(code[1], 0U);
    EXPECT_EQ(code[2], 0U);
    EXPECT_EQ(code[3], 0U);
    EXPECT_EQ(code[4], 0U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 0U).source_id, 7U);
    EXPECT_EQ(vigil_chunk_span_at(&chunk, 4U).end_offset, 25U);

    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, DisassembleFormatsOpcodesAndConstants)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_string_t output;
    vigil_value_t value;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_string_init(&output, runtime);
    vigil_value_init_int(&value, 123);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &value, Span(1U, 0U, 3U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(1U, 4U, 5U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_disassemble(&chunk, &output, &error), VIGIL_STATUS_OK);

    EXPECT_NE(strstr(vigil_string_c_str(&output), "0000 CONSTANT 0 123"), NULL);
    EXPECT_NE(strstr(vigil_string_c_str(&output), "0005 RETURN"), NULL);

    vigil_string_free(&output);
    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, OpcodeNameReturnsUnknownForOutOfRangeOpcode)
{
    EXPECT_STREQ(vigil_opcode_name(VIGIL_OPCODE_RETURN), "RETURN");
    EXPECT_STREQ(vigil_opcode_name((vigil_opcode_t)(VIGIL_OPCODE_CALL_EXTERN + 1)), "UNKNOWN");
}

TEST(VigilChunkTest, UsesRuntimeAllocatorHooks)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_value_t value;
    struct AllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t options = {0};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    vigil_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&value, 1);

    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &value, Span(1U, 0U, 1U), NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(1U, 2U, 3U), &error), VIGIL_STATUS_OK);

    EXPECT_GE(stats.allocate_calls, 3);

    vigil_chunk_free(&chunk);
    EXPECT_GE(stats.deallocate_calls, 2);
    vigil_runtime_close(&runtime);
}

TEST(VigilChunkTest, RejectsMissingRuntimeForMutation)
{
    vigil_chunk_t chunk;
    vigil_error_t error = {0};
    vigil_value_t value;

    vigil_chunk_init(&chunk, NULL);
    vigil_value_init_nil(&value);

    EXPECT_EQ(vigil_chunk_add_constant(&chunk, &value, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "chunk runtime must not be null"), 0);

    EXPECT_EQ(vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(0U, 0U, 0U), &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
}

TEST(VigilChunkTest, RejectsMissingArguments)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_string_t output;
    vigil_value_t value;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_string_init(&output, runtime);
    vigil_value_init_nil(&value);

    EXPECT_EQ(vigil_chunk_add_constant(&chunk, NULL, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "chunk constant requires value and out_index"), 0);

    vigil_string_free(&output);
    vigil_chunk_free(&chunk);
    vigil_runtime_close(&runtime);
}

void register_chunk_tests(void)
{
    REGISTER_TEST(VigilChunkTest, InitStartsEmpty);
    REGISTER_TEST(VigilChunkTest, WriteOpcodeAndBytesTrackSourceSpans);
    REGISTER_TEST(VigilChunkTest, AddConstantCopiesOwnedValueAndReleasesOnFree);
    REGISTER_TEST(VigilChunkTest, WriteConstantEncodesInstructionAndConstantIndex);
    REGISTER_TEST(VigilChunkTest, DisassembleFormatsOpcodesAndConstants);
    REGISTER_TEST(VigilChunkTest, OpcodeNameReturnsUnknownForOutOfRangeOpcode);
    REGISTER_TEST(VigilChunkTest, UsesRuntimeAllocatorHooks);
    REGISTER_TEST(VigilChunkTest, RejectsMissingRuntimeForMutation);
    REGISTER_TEST(VigilChunkTest, RejectsMissingArguments);
}
