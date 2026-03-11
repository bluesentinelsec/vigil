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

TEST(BaslVmTest, OptionsInitClearsFields) {
    basl_vm_options_t options = {};

    options.initial_stack_capacity = 99U;
    basl_vm_options_init(&options);
    EXPECT_EQ(options.initial_stack_capacity, 0U);
}

TEST(BaslVmTest, OpenAndCloseVm) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_NE(vm, nullptr);
    EXPECT_EQ(basl_vm_runtime(vm), runtime);
    EXPECT_EQ(basl_vm_stack_depth(vm), 0U);

    basl_vm_close(&vm);
    EXPECT_EQ(vm, nullptr);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, ExecutesConstantAndReturn) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t constant;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_int(&constant, 42);
    basl_value_init_nil(&result);

    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(1U, 0U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(1U, 4U, 5U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&result), 42);
    EXPECT_EQ(basl_vm_stack_depth(vm), 0U);

    basl_value_release(&result);
    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, ExecutesLiteralOpcodes) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_TRUE, Span(2U, 0U, 1U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(2U, 2U, 3U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_BOOL);
    EXPECT_TRUE(basl_value_as_bool(&result));
    basl_value_release(&result);

    basl_chunk_clear(&chunk);
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_NIL, Span(2U, 4U, 5U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(2U, 6U, 7U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_NIL);

    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, ReturnedObjectSurvivesChunkLifetime) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_object_t *object = nullptr;
    basl_value_t constant;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "hello", &object, &error),
        BASL_STATUS_OK
    );

    basl_value_init_object(&constant, &object);
    basl_value_init_nil(&result);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(3U, 0U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    basl_value_release(&constant);
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(3U, 4U, 5U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    ASSERT_NE(basl_value_as_object(&result), nullptr);
    EXPECT_EQ(basl_object_ref_count(basl_value_as_object(&result)), 2U);

    basl_chunk_free(&chunk);
    EXPECT_EQ(basl_object_ref_count(basl_value_as_object(&result)), 1U);
    EXPECT_STREQ(
        basl_string_object_c_str(basl_value_as_object(&result)),
        "hello"
    );

    basl_value_release(&result);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, RejectsMissingArguments) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    EXPECT_EQ(
        basl_vm_open(nullptr, runtime, nullptr, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "out_vm must not be null"), 0);

    EXPECT_EQ(
        basl_vm_open(&vm, nullptr, nullptr, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "runtime must not be null"), 0);

    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    EXPECT_EQ(
        basl_vm_execute(vm, nullptr, &result, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        basl_vm_execute(vm, &chunk, nullptr, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );

    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, ReportsUnsupportedOpcodeAndSourceId) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    ASSERT_EQ(
        basl_chunk_write_byte(&chunk, 255U, Span(9U, 10U, 11U), &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(
        basl_vm_execute(vm, &chunk, &result, &error),
        BASL_STATUS_UNSUPPORTED
    );
    EXPECT_EQ(error.type, BASL_STATUS_UNSUPPORTED);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "unsupported opcode"), 0);
    EXPECT_EQ(error.location.source_id, 9U);

    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t constant;
    basl_value_t result;
    basl_error_t error = {};
    AllocatorStats stats = {};
    basl_allocator_t allocator = {};
    basl_runtime_options_t runtime_options = {};
    basl_vm_options_t vm_options = {};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    basl_runtime_options_init(&runtime_options);
    runtime_options.allocator = &allocator;
    basl_vm_options_init(&vm_options);
    vm_options.initial_stack_capacity = 2U;

    ASSERT_EQ(basl_runtime_open(&runtime, &runtime_options, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, &vm_options, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_int(&constant, 7);
    basl_value_init_nil(&result);

    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(1U, 0U, 1U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(1U, 2U, 3U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_as_int(&result), 7);
    EXPECT_GE(stats.allocate_calls, 4);

    basl_value_release(&result);
    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    EXPECT_GE(stats.deallocate_calls, 4);
}
