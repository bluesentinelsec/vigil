#include "basl_test.h"

#include <stdlib.h>
#include <string.h>


#include "basl/basl.h"

struct AllocatorStats {
    int allocate_calls;
    int reallocate_calls;
    int deallocate_calls;
};

static void *CountedAllocate(void *user_data, size_t size) {
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->allocate_calls += 1;
    return calloc(1U, size);
}

static void *CountedReallocate(void *user_data, void *memory, size_t size) {
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->reallocate_calls += 1;
    return realloc(memory, size);
}

static void CountedDeallocate(void *user_data, void *memory) {
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->deallocate_calls += 1;
    free(memory);
}

static basl_source_span_t Span(basl_source_id_t source_id, size_t start, size_t end) {
    basl_source_span_t span = {0};

    span.source_id = source_id;
    span.start_offset = start;
    span.end_offset = end;
    return span;
}


TEST(BaslChunkTest, InitStartsEmpty) {
    basl_chunk_t chunk;

    basl_chunk_init(&chunk, NULL);

    EXPECT_EQ(chunk.runtime, NULL);
    EXPECT_EQ(basl_chunk_code_size(&chunk), 0U);
    EXPECT_EQ(basl_chunk_constant_count(&chunk), 0U);
    EXPECT_EQ(basl_chunk_code(&chunk), NULL);
    EXPECT_EQ(basl_chunk_constant(&chunk, 0U), NULL);
    EXPECT_EQ(basl_chunk_span_at(&chunk, 0U).source_id, 0U);
}

TEST(BaslChunkTest, WriteOpcodeAndBytesTrackSourceSpans) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_chunk_t chunk;
    const uint8_t *code;
    basl_source_span_t opcode_span = Span(1U, 10U, 11U);
    basl_source_span_t operand_span = Span(1U, 12U, 16U);

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
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
    ASSERT_NE(code, NULL);
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
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_chunk_t chunk;
    basl_object_t *object = NULL;
    basl_value_t value;
    const basl_value_t *stored;
    size_t index = SIZE_MAX;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "hello", &object, &error),
        BASL_STATUS_OK
    );

    basl_value_init_object(&value, &object);
    ASSERT_EQ(object, NULL);

    ASSERT_EQ(
        basl_chunk_add_constant(&chunk, &value, &index, &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(index, 0U);
    EXPECT_EQ(basl_chunk_constant_count(&chunk), 1U);
    stored = basl_chunk_constant(&chunk, index);
    ASSERT_NE(stored, NULL);
    ASSERT_NE(basl_value_as_object(stored), NULL);
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
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_chunk_t chunk;
    basl_value_t value;
    size_t index = SIZE_MAX;
    const uint8_t *code;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_int(&value, 42);

    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &value, Span(7U, 20U, 25U), &index, &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(index, 0U);
    ASSERT_EQ(basl_chunk_code_size(&chunk), 5U);
    code = basl_chunk_code(&chunk);
    ASSERT_NE(code, NULL);
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
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_chunk_t chunk;
    basl_string_t output;
    basl_value_t value;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_string_init(&output, runtime);
    basl_value_init_int(&value, 123);

    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &value, Span(1U, 0U, 3U), NULL, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(1U, 4U, 5U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(basl_chunk_disassemble(&chunk, &output, &error), BASL_STATUS_OK);

    EXPECT_NE(strstr(basl_string_c_str(&output), "0000 CONSTANT 0 123"), NULL);
    EXPECT_NE(strstr(basl_string_c_str(&output), "0005 RETURN"), NULL);

    basl_string_free(&output);
    basl_chunk_free(&chunk);
    basl_runtime_close(&runtime);
}

TEST(BaslChunkTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_chunk_t chunk;
    basl_value_t value;
    struct AllocatorStats stats = {0};
    basl_allocator_t allocator = {0};
    basl_runtime_options_t options = {0};

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
        basl_chunk_write_constant(&chunk, &value, Span(1U, 0U, 1U), NULL, &error),
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
    basl_error_t error = {0};
    basl_value_t value;

    basl_chunk_init(&chunk, NULL);
    basl_value_init_nil(&value);

    EXPECT_EQ(
        basl_chunk_add_constant(&chunk, &value, NULL, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "chunk runtime must not be null"), 0);

    EXPECT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(0U, 0U, 0U), &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
}

TEST(BaslChunkTest, RejectsMissingArguments) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_chunk_t chunk;
    basl_string_t output;
    basl_value_t value;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_string_init(&output, runtime);
    basl_value_init_nil(&value);

    EXPECT_EQ(
        basl_chunk_add_constant(&chunk, NULL, NULL, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(
        strcmp(error.value, "chunk constant requires value and out_index"),
        0
    );

    basl_string_free(&output);
    basl_chunk_free(&chunk);
    basl_runtime_close(&runtime);
}

void register_chunk_tests(void) {
    REGISTER_TEST(BaslChunkTest, InitStartsEmpty);
    REGISTER_TEST(BaslChunkTest, WriteOpcodeAndBytesTrackSourceSpans);
    REGISTER_TEST(BaslChunkTest, AddConstantCopiesOwnedValueAndReleasesOnFree);
    REGISTER_TEST(BaslChunkTest, WriteConstantEncodesInstructionAndConstantIndex);
    REGISTER_TEST(BaslChunkTest, DisassembleFormatsOpcodesAndConstants);
    REGISTER_TEST(BaslChunkTest, UsesRuntimeAllocatorHooks);
    REGISTER_TEST(BaslChunkTest, RejectsMissingRuntimeForMutation);
    REGISTER_TEST(BaslChunkTest, RejectsMissingArguments);
}
