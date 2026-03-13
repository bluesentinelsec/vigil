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
    EXPECT_EQ(basl_vm_frame_depth(vm), 0U);

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
    EXPECT_EQ(basl_vm_frame_depth(vm), 0U);

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

TEST(BaslVmTest, RejectsArithmeticOverflow) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t constant;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    basl_value_init_int(&constant, INT64_MAX);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(5U, 0U, 1U), nullptr, &error),
        BASL_STATUS_OK
    );
    basl_value_init_int(&constant, 1);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(5U, 2U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_ADD, Span(5U, 4U, 5U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(5U, 6U, 7U), &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(
        basl_vm_execute(vm, &chunk, &result, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(
        std::strcmp(error.value, "integer arithmetic overflow or invalid operation"),
        0
    );

    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, RejectsNegateOverflow) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t constant;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    basl_value_init_int(&constant, INT64_MIN);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(6U, 0U, 1U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_NEGATE, Span(6U, 2U, 3U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(6U, 4U, 5U), &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(
        basl_vm_execute(vm, &chunk, &result, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(
        std::strcmp(error.value, "integer arithmetic overflow or invalid operation"),
        0
    );

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

TEST(BaslVmTest, ConcatenatesAndComparesStringsByValue) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_object_t *left_object = nullptr;
    basl_object_t *right_object = nullptr;
    basl_object_t *expected_object = nullptr;
    basl_value_t left;
    basl_value_t right;
    basl_value_t expected;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    ASSERT_EQ(basl_string_object_new_cstr(runtime, "ba", &left_object, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_string_object_new_cstr(runtime, "sl", &right_object, &error), BASL_STATUS_OK);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "basl", &expected_object, &error),
        BASL_STATUS_OK
    );

    basl_value_init_object(&left, &left_object);
    basl_value_init_object(&right, &right_object);
    basl_value_init_object(&expected, &expected_object);
    basl_value_init_nil(&result);

    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &left, Span(3U, 0U, 1U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &right, Span(3U, 2U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_ADD, Span(3U, 4U, 5U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &expected, Span(3U, 6U, 10U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_EQUAL, Span(3U, 11U, 13U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(3U, 14U, 15U), &error),
        BASL_STATUS_OK
    );

    basl_value_release(&left);
    basl_value_release(&right);
    basl_value_release(&expected);

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_BOOL);
    EXPECT_TRUE(basl_value_as_bool(&result));

    basl_value_release(&result);
    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, SupportsFloatArithmeticAndNegation) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t constant;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    basl_value_init_float(&constant, 1.5);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(7U, 0U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    basl_value_init_float(&constant, 2.0);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(7U, 4U, 7U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_ADD, Span(7U, 8U, 9U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_NEGATE, Span(7U, 10U, 11U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(7U, 12U, 13U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_FLOAT);
    EXPECT_DOUBLE_EQ(basl_value_as_float(&result), -3.5);

    basl_value_release(&result);
    basl_chunk_free(&chunk);
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
    EXPECT_EQ(
        basl_vm_execute_function(vm, nullptr, nullptr, &error),
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
    EXPECT_EQ(error.location.offset, 10U);

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

TEST(BaslVmTest, ExecutesFunctionObjectEntry) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_object_t *function = nullptr;
    basl_value_t constant;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_int(&constant, 99);
    basl_value_init_nil(&result);

    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(4U, 0U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(4U, 4U, 5U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_function_object_new_cstr(runtime, "main", 0U, 1U, &chunk, &function, &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(
        basl_vm_execute_function(vm, function, &result, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&result), 99);
    EXPECT_EQ(basl_vm_stack_depth(vm), 0U);
    EXPECT_EQ(basl_vm_frame_depth(vm), 0U);

    basl_value_release(&result);
    basl_object_release(&function);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, ExecuteFunctionRejectsNonFunctionObject) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_object_t *object = nullptr;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "hello", &object, &error),
        BASL_STATUS_OK
    );
    basl_value_init_nil(&result);

    EXPECT_EQ(
        basl_vm_execute_function(vm, object, &result, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "function must be a function object"), 0);

    basl_object_release(&object);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, ExecuteFunctionRejectsNonZeroArityEntrypoint) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_object_t *function = nullptr;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(7U, 0U, 1U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_function_object_new_cstr(runtime, "helper", 1U, 1U, &chunk, &function, &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(
        basl_vm_execute_function(vm, function, &result, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(
        std::strcmp(error.value, "top-level execute_function requires a zero-arity function"),
        0
    );

    basl_object_release(&function);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, SupportsConversionsAndBitwiseNotOpcodes) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t constant;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    basl_value_init_float(&constant, 5.9);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(8U, 0U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_TO_I32, Span(8U, 4U, 5U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_BITWISE_NOT, Span(8U, 6U, 7U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(8U, 8U, 9U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&result), -6);
    basl_value_release(&result);

    basl_chunk_clear(&chunk);
    basl_value_init_bool(&constant, true);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(9U, 0U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_TO_STRING, Span(9U, 4U, 5U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(9U, 6U, 7U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_value_kind(&result), BASL_VALUE_OBJECT);
    ASSERT_NE(basl_value_as_object(&result), nullptr);
    EXPECT_EQ(basl_object_type(basl_value_as_object(&result)), BASL_OBJECT_STRING);
    EXPECT_STREQ(basl_string_object_c_str(basl_value_as_object(&result)), "true");
    basl_value_release(&result);

    basl_chunk_clear(&chunk);
    basl_value_init_float(&constant, 255.9);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(10U, 0U, 5U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_TO_U8, Span(10U, 6U, 7U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(10U, 8U, 9U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&result), 255);
    basl_value_release(&result);

    basl_chunk_clear(&chunk);
    basl_value_init_int(&constant, 42);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(11U, 0U, 2U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_TO_I64, Span(11U, 3U, 4U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(11U, 5U, 6U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&result), 42);
    basl_value_release(&result);

    basl_chunk_clear(&chunk);
    basl_value_init_float(&constant, -1.0);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(12U, 0U, 4U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_TO_U32, Span(12U, 5U, 6U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(12U, 7U, 8U), &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(
        basl_vm_execute(vm, &chunk, &result, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "u32 conversion overflow or invalid value"), 0);

    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, SupportsErrorOpcodes) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_value_t constant;
    basl_value_t result;
    basl_object_t *object = nullptr;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    ASSERT_EQ(basl_string_object_new_cstr(runtime, "boom", &object, &error), BASL_STATUS_OK);
    basl_value_init_object(&constant, &object);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(10U, 0U, 4U), nullptr, &error),
        BASL_STATUS_OK
    );
    basl_value_release(&constant);
    basl_value_init_int(&constant, 9);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(10U, 5U, 6U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_NEW_ERROR, Span(10U, 7U, 8U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_GET_ERROR_KIND, Span(10U, 9U, 10U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(10U, 11U, 12U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&result), 9);
    basl_value_release(&result);

    basl_chunk_clear(&chunk);
    ASSERT_EQ(basl_string_object_new_cstr(runtime, "boom", &object, &error), BASL_STATUS_OK);
    basl_value_init_object(&constant, &object);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(11U, 0U, 4U), nullptr, &error),
        BASL_STATUS_OK
    );
    basl_value_release(&constant);
    basl_value_init_int(&constant, 9);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(11U, 5U, 6U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_NEW_ERROR, Span(11U, 7U, 8U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(
            &chunk,
            BASL_OPCODE_GET_ERROR_MESSAGE,
            Span(11U, 9U, 10U),
            &error
        ),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(11U, 11U, 12U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_value_kind(&result), BASL_VALUE_OBJECT);
    ASSERT_NE(basl_value_as_object(&result), nullptr);
    EXPECT_EQ(basl_object_type(basl_value_as_object(&result)), BASL_OBJECT_STRING);
    EXPECT_STREQ(basl_string_object_c_str(basl_value_as_object(&result)), "boom");

    basl_value_release(&result);
    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslVmTest, ExecutesArrayAndMapIndexOpcodes) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_chunk_t chunk;
    basl_object_t *key_object = nullptr;
    basl_value_t constant;
    basl_value_t result;
    basl_error_t error = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_nil(&result);

    basl_value_init_int(&constant, 4);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(12U, 0U, 1U), nullptr, &error),
        BASL_STATUS_OK
    );
    basl_value_init_int(&constant, 6);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(12U, 2U, 3U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_NEW_ARRAY, Span(12U, 4U, 5U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(basl_chunk_write_u32(&chunk, 0U, Span(12U, 6U, 7U), &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_chunk_write_u32(&chunk, 2U, Span(12U, 8U, 9U), &error), BASL_STATUS_OK);
    basl_value_init_int(&constant, 1);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(12U, 10U, 11U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_GET_INDEX, Span(12U, 12U, 13U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(12U, 14U, 15U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&result), 6);
    basl_value_release(&result);

    basl_chunk_clear(&chunk);
    ASSERT_EQ(basl_string_object_new_cstr(runtime, "answer", &key_object, &error), BASL_STATUS_OK);
    basl_value_init_object(&constant, &key_object);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(13U, 0U, 6U), nullptr, &error),
        BASL_STATUS_OK
    );
    basl_value_release(&constant);
    basl_value_init_int(&constant, 9);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(13U, 7U, 8U), nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_NEW_MAP, Span(13U, 9U, 10U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(basl_chunk_write_u32(&chunk, 0U, Span(13U, 11U, 12U), &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_chunk_write_u32(&chunk, 1U, Span(13U, 13U, 14U), &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_string_object_new_cstr(runtime, "answer", &key_object, &error), BASL_STATUS_OK);
    basl_value_init_object(&constant, &key_object);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &constant, Span(13U, 15U, 21U), nullptr, &error),
        BASL_STATUS_OK
    );
    basl_value_release(&constant);
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_GET_INDEX, Span(13U, 22U, 23U), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_chunk_write_opcode(&chunk, BASL_OPCODE_RETURN, Span(13U, 24U, 25U), &error),
        BASL_STATUS_OK
    );

    ASSERT_EQ(basl_vm_execute(vm, &chunk, &result, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&result), 9);

    basl_value_release(&result);
    basl_chunk_free(&chunk);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}
