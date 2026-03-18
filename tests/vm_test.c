#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>


#include "vigil/vigil.h"

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

static vigil_source_span_t Span(vigil_source_id_t source_id, size_t start, size_t end) {
    vigil_source_span_t span = {0};

    span.source_id = source_id;
    span.start_offset = start;
    span.end_offset = end;
    return span;
}


TEST(VigilVmTest, OptionsInitClearsFields) {
    vigil_vm_options_t options = {0};

    options.initial_stack_capacity = 99U;
    vigil_vm_options_init(&options);
    EXPECT_EQ(options.initial_stack_capacity, 0U);
}

TEST(VigilVmTest, OpenAndCloseVm) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_NE(vm, NULL);
    EXPECT_EQ(vigil_vm_runtime(vm), runtime);
    EXPECT_EQ(vigil_vm_stack_depth(vm), 0U);
    EXPECT_EQ(vigil_vm_frame_depth(vm), 0U);

    vigil_vm_close(&vm);
    EXPECT_EQ(vm, NULL);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecutesConstantAndReturn) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&constant, 42);
    vigil_value_init_nil(&result);

    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(1U, 0U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(1U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 42);
    EXPECT_EQ(vigil_vm_stack_depth(vm), 0U);
    EXPECT_EQ(vigil_vm_frame_depth(vm), 0U);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecutesLiteralOpcodes) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TRUE, Span(2U, 0U, 1U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(2U, 2U, 3U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_BOOL);
    EXPECT_TRUE(vigil_value_as_bool(&result));
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NIL, Span(2U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(2U, 6U, 7U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_NIL);

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, RejectsArithmeticOverflow) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_int(&constant, INT64_MAX);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(5U, 0U, 1U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 1);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(5U, 2U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_ADD, Span(5U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(5U, 6U, 7U), &error),
        VIGIL_STATUS_OK
    );

    EXPECT_EQ(
        vigil_vm_execute(vm, &chunk, &result, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(
        strcmp(error.value, "integer arithmetic overflow or invalid operation"),
        0
    );

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, RejectsNegateOverflow) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_int(&constant, INT64_MIN);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(6U, 0U, 1U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_release(&constant);
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEGATE, Span(6U, 2U, 3U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(6U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );

    EXPECT_EQ(
        vigil_vm_execute(vm, &chunk, &result, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(
        strcmp(error.value, "integer arithmetic overflow or invalid operation"),
        0
    );

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ReturnedObjectSurvivesChunkLifetime) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *object = NULL;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    ASSERT_EQ(
        vigil_string_object_new_cstr(runtime, "hello", &object, &error),
        VIGIL_STATUS_OK
    );

    vigil_value_init_object(&constant, &object);
    vigil_value_init_nil(&result);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(3U, 0U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_release(&constant);
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(3U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(&result)), 2U);

    vigil_chunk_free(&chunk);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(&result)), 1U);
    EXPECT_STREQ(
        vigil_string_object_c_str(vigil_value_as_object(&result)),
        "hello"
    );

    vigil_value_release(&result);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ConcatenatesAndComparesStringsByValue) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *left_object = NULL;
    vigil_object_t *right_object = NULL;
    vigil_object_t *expected_object = NULL;
    vigil_value_t left;
    vigil_value_t right;
    vigil_value_t expected;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "vi", &left_object, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "gil", &right_object, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(
        vigil_string_object_new_cstr(runtime, "vigil", &expected_object, &error),
        VIGIL_STATUS_OK
    );

    vigil_value_init_object(&left, &left_object);
    vigil_value_init_object(&right, &right_object);
    vigil_value_init_object(&expected, &expected_object);
    vigil_value_init_nil(&result);

    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &left, Span(3U, 0U, 1U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &right, Span(3U, 2U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_ADD, Span(3U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &expected, Span(3U, 6U, 10U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_EQUAL, Span(3U, 11U, 13U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(3U, 14U, 15U), &error),
        VIGIL_STATUS_OK
    );

    vigil_value_release(&left);
    vigil_value_release(&right);
    vigil_value_release(&expected);

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_BOOL);
    EXPECT_TRUE(vigil_value_as_bool(&result));

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, SupportsFloatArithmeticAndNegation) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_float(&constant, 1.5);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(7U, 0U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_init_float(&constant, 2.0);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(7U, 4U, 7U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_ADD, Span(7U, 8U, 9U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEGATE, Span(7U, 10U, 11U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(7U, 12U, 13U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_FLOAT);
    EXPECT_DOUBLE_EQ(vigil_value_as_float(&result), -3.5);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, RejectsMissingArguments) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    EXPECT_EQ(
        vigil_vm_open(NULL, runtime, NULL, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "out_vm must not be null"), 0);

    EXPECT_EQ(
        vigil_vm_open(&vm, NULL, NULL, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "runtime must not be null"), 0);

    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(
        vigil_vm_execute(vm, NULL, &result, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        vigil_vm_execute(vm, &chunk, NULL, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        vigil_vm_execute_function(vm, NULL, NULL, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ReportsUnsupportedOpcodeAndSourceId) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    ASSERT_EQ(
        vigil_chunk_write_byte(&chunk, 255U, Span(9U, 10U, 11U), &error),
        VIGIL_STATUS_OK
    );

    EXPECT_EQ(
        vigil_vm_execute(vm, &chunk, &result, &error),
        VIGIL_STATUS_UNSUPPORTED
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_UNSUPPORTED);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "unsupported opcode"), 0);
    EXPECT_EQ(error.location.source_id, 9U);
    EXPECT_EQ(error.location.offset, 10U);

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, UsesRuntimeAllocatorHooks) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};
    struct AllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t runtime_options = {0};
    vigil_vm_options_t vm_options = {0};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    vigil_runtime_options_init(&runtime_options);
    runtime_options.allocator = &allocator;
    vigil_vm_options_init(&vm_options);
    vm_options.initial_stack_capacity = 2U;

    ASSERT_EQ(vigil_runtime_open(&runtime, &runtime_options, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, &vm_options, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&constant, 7);
    vigil_value_init_nil(&result);

    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(1U, 0U, 1U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(1U, 2U, 3U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 7);
    EXPECT_GE(stats.allocate_calls, 4);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    EXPECT_GE(stats.deallocate_calls, 4);
}

TEST(VigilVmTest, ExecutesFunctionObjectEntry) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *function = NULL;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&constant, 99);
    vigil_value_init_nil(&result);

    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(4U, 0U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(4U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_function_object_new_cstr(runtime, "main", 0U, 1U, &chunk, &function, &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(
        vigil_vm_execute_function(vm, function, &result, &error),
        VIGIL_STATUS_OK
    );
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 99);
    EXPECT_EQ(vigil_vm_stack_depth(vm), 0U);
    EXPECT_EQ(vigil_vm_frame_depth(vm), 0U);

    vigil_value_release(&result);
    vigil_object_release(&function);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecuteFunctionRejectsNonFunctionObject) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_object_t *object = NULL;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(
        vigil_string_object_new_cstr(runtime, "hello", &object, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_init_nil(&result);

    EXPECT_EQ(
        vigil_vm_execute_function(vm, object, &result, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "function must be a function or closure object"), 0);

    vigil_object_release(&object);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecuteFunctionRejectsNonZeroArityEntrypoint) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *function = NULL;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(7U, 0U, 1U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_function_object_new_cstr(runtime, "helper", 1U, 1U, &chunk, &function, &error),
        VIGIL_STATUS_OK
    );

    EXPECT_EQ(
        vigil_vm_execute_function(vm, function, &result, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(
        strcmp(error.value, "not enough arguments on stack for function arity"),
        0
    );

    vigil_object_release(&function);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, SupportsConversionsAndBitwiseNotOpcodes) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_float(&constant, 5.9);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(8U, 0U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_I32, Span(8U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_BITWISE_NOT, Span(8U, 6U, 7U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(8U, 8U, 9U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), -6);
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    vigil_value_init_bool(&constant, true);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(9U, 0U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_STRING, Span(9U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(9U, 6U, 7U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "true");
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    vigil_value_init_float(&constant, 255.9);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(10U, 0U, 5U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_U8, Span(10U, 6U, 7U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(10U, 8U, 9U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_UINT);
    EXPECT_EQ(vigil_value_as_uint(&result), UINT64_C(255));
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    vigil_value_init_int(&constant, 42);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(11U, 0U, 2U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_I64, Span(11U, 3U, 4U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(11U, 5U, 6U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 42);
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    vigil_value_init_float(&constant, -1.0);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(12U, 0U, 4U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_U32, Span(12U, 5U, 6U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(12U, 7U, 8U), &error),
        VIGIL_STATUS_OK
    );

    EXPECT_EQ(
        vigil_vm_execute(vm, &chunk, &result, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "u32 conversion overflow or invalid value"), 0);

    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecutesUnsignedIntegerArithmetic) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_uint(&constant, UINT64_C(9223372036854775808));
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(13U, 0U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_release(&constant);
    vigil_value_init_uint(&constant, UINT64_C(2));
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(13U, 4U, 5U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_ADD, Span(13U, 6U, 7U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(13U, 8U, 9U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_UINT);
    EXPECT_EQ(vigil_value_as_uint(&result), UINT64_C(9223372036854775810));
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    vigil_value_init_uint(&constant, UINT64_C(9223372036854775808));
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(14U, 0U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_release(&constant);
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_TO_F64, Span(14U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(14U, 6U, 7U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_FLOAT);
    EXPECT_DOUBLE_EQ(vigil_value_as_float(&result), 9223372036854775808.0);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, SupportsErrorOpcodes) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_object_t *object = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "boom", &object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &object);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(10U, 0U, 4U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 9);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(10U, 5U, 6U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEW_ERROR, Span(10U, 7U, 8U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_GET_ERROR_KIND, Span(10U, 9U, 10U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(10U, 11U, 12U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 9);
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "boom", &object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &object);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(11U, 0U, 4U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 9);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(11U, 5U, 6U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEW_ERROR, Span(11U, 7U, 8U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(
            &chunk,
            VIGIL_OPCODE_GET_ERROR_MESSAGE,
            Span(11U, 9U, 10U),
            &error
        ),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(11U, 11U, 12U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_value_kind(&result), VIGIL_VALUE_OBJECT);
    ASSERT_NE(vigil_value_as_object(&result), NULL);
    EXPECT_EQ(vigil_object_type(vigil_value_as_object(&result)), VIGIL_OBJECT_STRING);
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(&result)), "boom");

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

TEST(VigilVmTest, ExecutesArrayAndMapIndexOpcodes) {
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    vigil_chunk_t chunk;
    vigil_object_t *key_object = NULL;
    vigil_value_t constant;
    vigil_value_t result;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_nil(&result);

    vigil_value_init_int(&constant, 4);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(12U, 0U, 1U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_init_int(&constant, 6);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(12U, 2U, 3U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEW_ARRAY, Span(12U, 4U, 5U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(12U, 6U, 7U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 2U, Span(12U, 8U, 9U), &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&constant, 1);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(12U, 10U, 11U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_GET_INDEX, Span(12U, 12U, 13U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(12U, 14U, 15U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 6);
    vigil_value_release(&result);

    vigil_chunk_clear(&chunk);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "answer", &key_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &key_object);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(13U, 0U, 6U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_release(&constant);
    vigil_value_init_int(&constant, 9);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(13U, 7U, 8U), NULL, &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_NEW_MAP, Span(13U, 9U, 10U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 0U, Span(13U, 11U, 12U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_chunk_write_u32(&chunk, 1U, Span(13U, 13U, 14U), &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "answer", &key_object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&constant, &key_object);
    ASSERT_EQ(
        vigil_chunk_write_constant(&chunk, &constant, Span(13U, 15U, 21U), NULL, &error),
        VIGIL_STATUS_OK
    );
    vigil_value_release(&constant);
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_GET_INDEX, Span(13U, 22U, 23U), &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_chunk_write_opcode(&chunk, VIGIL_OPCODE_RETURN, Span(13U, 24U, 25U), &error),
        VIGIL_STATUS_OK
    );

    ASSERT_EQ(vigil_vm_execute(vm, &chunk, &result, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_kind(&result), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&result), 9);

    vigil_value_release(&result);
    vigil_chunk_free(&chunk);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
}

void register_vm_tests(void) {
    REGISTER_TEST(VigilVmTest, OptionsInitClearsFields);
    REGISTER_TEST(VigilVmTest, OpenAndCloseVm);
    REGISTER_TEST(VigilVmTest, ExecutesConstantAndReturn);
    REGISTER_TEST(VigilVmTest, ExecutesLiteralOpcodes);
    REGISTER_TEST(VigilVmTest, RejectsArithmeticOverflow);
    REGISTER_TEST(VigilVmTest, RejectsNegateOverflow);
    REGISTER_TEST(VigilVmTest, ReturnedObjectSurvivesChunkLifetime);
    REGISTER_TEST(VigilVmTest, ConcatenatesAndComparesStringsByValue);
    REGISTER_TEST(VigilVmTest, SupportsFloatArithmeticAndNegation);
    REGISTER_TEST(VigilVmTest, RejectsMissingArguments);
    REGISTER_TEST(VigilVmTest, ReportsUnsupportedOpcodeAndSourceId);
    REGISTER_TEST(VigilVmTest, UsesRuntimeAllocatorHooks);
    REGISTER_TEST(VigilVmTest, ExecutesFunctionObjectEntry);
    REGISTER_TEST(VigilVmTest, ExecuteFunctionRejectsNonFunctionObject);
    REGISTER_TEST(VigilVmTest, ExecuteFunctionRejectsNonZeroArityEntrypoint);
    REGISTER_TEST(VigilVmTest, SupportsConversionsAndBitwiseNotOpcodes);
    REGISTER_TEST(VigilVmTest, ExecutesUnsignedIntegerArithmetic);
    REGISTER_TEST(VigilVmTest, SupportsErrorOpcodes);
    REGISTER_TEST(VigilVmTest, ExecutesArrayAndMapIndexOpcodes);
}
