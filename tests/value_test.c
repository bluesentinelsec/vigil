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


TEST(BaslValueTest, ImmediateValuesRoundTrip) {
    basl_value_t value;

    basl_value_init_nil(&value);
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_NIL);
    EXPECT_EQ(basl_value_as_object(&value), NULL);

    basl_value_init_bool(&value, true);
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_BOOL);
    EXPECT_TRUE(basl_value_as_bool(&value));

    basl_value_init_int(&value, 42);
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&value), 42);

    basl_value_init_uint(&value, UINT64_C(9223372036854775808));
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_UINT);
    EXPECT_EQ(basl_value_as_uint(&value), UINT64_C(9223372036854775808));
    basl_value_release(&value);

    basl_value_init_float(&value, 3.5);
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_FLOAT);
    EXPECT_DOUBLE_EQ(basl_value_as_float(&value), 3.5);
}

TEST(BaslValueTest, StringObjectStartsWithOneReferenceAndExposesText) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_object_t *object = NULL;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "hello", &object, &error),
        BASL_STATUS_OK
    );

    ASSERT_NE(object, NULL);
    EXPECT_EQ(basl_object_type(object), BASL_OBJECT_STRING);
    EXPECT_EQ(basl_object_ref_count(object), 1U);
    EXPECT_EQ(basl_string_object_length(object), 5U);
    EXPECT_STREQ(basl_string_object_c_str(object), "hello");

    basl_object_release(&object);
    EXPECT_EQ(object, NULL);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, ObjectRetainAndReleaseUpdateReferenceCount) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_object_t *left = NULL;
    basl_object_t *right = NULL;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "hello", &left, &error),
        BASL_STATUS_OK
    );

    right = left;
    basl_object_retain(right);
    EXPECT_EQ(basl_object_ref_count(left), 2U);

    basl_object_release(&left);
    EXPECT_EQ(left, NULL);
    ASSERT_NE(right, NULL);
    EXPECT_EQ(basl_object_ref_count(right), 1U);

    basl_object_release(&right);
    EXPECT_EQ(right, NULL);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, ValueInitObjectTransfersOwnershipAndCopyRetains) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_object_t *object = NULL;
    basl_value_t value;
    basl_value_t copy;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "value", &object, &error),
        BASL_STATUS_OK
    );

    basl_value_init_object(&value, &object);
    EXPECT_EQ(object, NULL);
    ASSERT_NE(basl_value_as_object(&value), NULL);
    EXPECT_EQ(basl_object_ref_count(basl_value_as_object(&value)), 1U);

    copy = basl_value_copy(&value);
    ASSERT_NE(basl_value_as_object(&copy), NULL);
    EXPECT_EQ(basl_object_ref_count(basl_value_as_object(&value)), 2U);

    basl_value_release(&value);
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_NIL);
    EXPECT_EQ(basl_object_ref_count(basl_value_as_object(&copy)), 1U);

    basl_value_release(&copy);
    EXPECT_EQ(basl_value_kind(&copy), BASL_VALUE_NIL);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, ValueReleaseOnImmediateResetsToNil) {
    basl_value_t value;

    basl_value_init_int(&value, 7);
    basl_value_release(&value);

    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_NIL);
    EXPECT_EQ(basl_value_as_object(&value), NULL);
}

TEST(BaslValueTest, StringObjectUsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_object_t *object = NULL;
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
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "allocator-backed", &object, &error),
        BASL_STATUS_OK
    );

    EXPECT_GE(stats.allocate_calls, 3);

    basl_object_release(&object);
    EXPECT_EQ(object, NULL);
    EXPECT_GE(stats.deallocate_calls, 2);

    basl_runtime_close(&runtime);
    EXPECT_GE(stats.deallocate_calls, 3);
}

TEST(BaslValueTest, StringObjectValidatesArguments) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_object_t *object = NULL;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);

    EXPECT_EQ(
        basl_string_object_new(NULL, "hello", 5U, &object, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "runtime must not be null"), 0);

    EXPECT_EQ(
        basl_string_object_new(runtime, NULL, 0U, &object, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "string object value must not be null"), 0);

    EXPECT_EQ(
        basl_string_object_new(runtime, "hello", 5U, NULL, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "out_object must not be null"), 0);

    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, FunctionObjectTakesOwnershipOfChunkAndExposesMetadata) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_chunk_t chunk;
    basl_object_t *function = NULL;
    basl_value_t value;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_int(&value, 42);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &value, (basl_source_span_t){0}, NULL, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_function_object_new_cstr(runtime, "main", 0U, 1U, &chunk, &function, &error),
        BASL_STATUS_OK
    );

    ASSERT_NE(function, NULL);
    EXPECT_EQ(basl_object_type(function), BASL_OBJECT_FUNCTION);
    EXPECT_EQ(basl_object_ref_count(function), 1U);
    EXPECT_STREQ(basl_function_object_name(function), "main");
    EXPECT_EQ(basl_function_object_arity(function), 0U);
    ASSERT_NE(basl_function_object_chunk(function), NULL);
    EXPECT_EQ(basl_chunk_constant_count(basl_function_object_chunk(function)), 1U);

    EXPECT_EQ(chunk.runtime, NULL);
    EXPECT_EQ(basl_chunk_code_size(&chunk), 0U);
    EXPECT_EQ(basl_chunk_constant_count(&chunk), 0U);

    basl_object_release(&function);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, FunctionObjectValidatesArguments) {
    basl_runtime_t *runtime = NULL;
    basl_runtime_t *other_runtime = NULL;
    basl_error_t error = {0};
    basl_chunk_t chunk;
    basl_object_t *function = NULL;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_runtime_open(&other_runtime, NULL, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, other_runtime);

    EXPECT_EQ(
        basl_function_object_new(NULL, "main", 4U, 0U, 1U, &chunk, &function, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        basl_function_object_new(runtime, NULL, 0U, 0U, 1U, &chunk, &function, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        basl_function_object_new(runtime, "main", 4U, 0U, 1U, NULL, &function, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        basl_function_object_new(runtime, "main", 4U, 0U, 1U, &chunk, NULL, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        basl_function_object_new(runtime, "main", 4U, 0U, 1U, &chunk, &function, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(
        strcmp(error.value, "function object chunk runtime must match runtime"),
        0
    );

    basl_chunk_free(&chunk);
    basl_runtime_close(&other_runtime);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, InstanceObjectStoresAndUpdatesFields) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_object_t *instance = NULL;
    basl_value_t fields[2];
    basl_value_t field_value;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_value_init_int(&fields[0], 3);
    basl_value_init_bool(&fields[1], true);

    ASSERT_EQ(
        basl_instance_object_new(runtime, 3U, fields, 2U, &instance, &error),
        BASL_STATUS_OK
    );
    ASSERT_NE(instance, NULL);
    EXPECT_EQ(basl_object_type(instance), BASL_OBJECT_INSTANCE);
    EXPECT_EQ(basl_instance_object_class_index(instance), 3U);
    EXPECT_EQ(basl_instance_object_field_count(instance), 2U);

    basl_value_init_nil(&field_value);
    ASSERT_TRUE(basl_instance_object_get_field(instance, 0U, &field_value));
    EXPECT_EQ(basl_value_kind(&field_value), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&field_value), 3);
    basl_value_release(&field_value);

    basl_value_init_int(&field_value, 9);
    ASSERT_EQ(
        basl_instance_object_set_field(instance, 0U, &field_value, &error),
        BASL_STATUS_OK
    );
    basl_value_release(&field_value);

    basl_value_init_nil(&field_value);
    ASSERT_TRUE(basl_instance_object_get_field(instance, 0U, &field_value));
    EXPECT_EQ(basl_value_as_int(&field_value), 9);
    basl_value_release(&field_value);

    basl_value_release(&fields[0]);
    basl_value_release(&fields[1]);
    basl_object_release(&instance);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, ArrayAndMapObjectsStoreAndExposeIndexedValues) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_object_t *array_object = NULL;
    basl_object_t *map_object = NULL;
    basl_value_t items[2];
    basl_value_t value;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_value_init_int(&items[0], 3);
    basl_value_init_int(&items[1], 7);

    ASSERT_EQ(
        basl_array_object_new(runtime, items, 2U, &array_object, &error),
        BASL_STATUS_OK
    );
    ASSERT_NE(array_object, NULL);
    EXPECT_EQ(basl_object_type(array_object), BASL_OBJECT_ARRAY);
    EXPECT_EQ(basl_array_object_length(array_object), 2U);

    basl_value_init_nil(&value);
    ASSERT_TRUE(basl_array_object_get(array_object, 1U, &value));
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&value), 7);
    basl_value_release(&value);

    basl_value_init_int(&value, 9);
    ASSERT_EQ(
        basl_array_object_set(array_object, 0U, &value, &error),
        BASL_STATUS_OK
    );
    basl_value_release(&value);

    basl_value_init_nil(&value);
    ASSERT_TRUE(basl_array_object_get(array_object, 0U, &value));
    EXPECT_EQ(basl_value_as_int(&value), 9);
    basl_value_release(&value);

    ASSERT_EQ(basl_map_object_new(runtime, &map_object, &error), BASL_STATUS_OK);
    ASSERT_NE(map_object, NULL);
    EXPECT_EQ(basl_object_type(map_object), BASL_OBJECT_MAP);
    EXPECT_EQ(basl_map_object_count(map_object), 0U);

    basl_value_init_int(&items[0], 1);
    basl_value_init_int(&value, 11);
    ASSERT_EQ(
        basl_map_object_set(map_object, &items[0], &value, &error),
        BASL_STATUS_OK
    );
    basl_value_release(&value);
    EXPECT_EQ(basl_map_object_count(map_object), 1U);

    basl_value_init_nil(&value);
    ASSERT_TRUE(basl_map_object_get(map_object, &items[0], &value));
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&value), 11);
    basl_value_release(&value);

    basl_value_release(&items[0]);
    basl_value_release(&items[1]);
    basl_object_release(&array_object);
    basl_object_release(&map_object);
    basl_runtime_close(&runtime);
}

void register_value_tests(void) {
    REGISTER_TEST(BaslValueTest, ImmediateValuesRoundTrip);
    REGISTER_TEST(BaslValueTest, StringObjectStartsWithOneReferenceAndExposesText);
    REGISTER_TEST(BaslValueTest, ObjectRetainAndReleaseUpdateReferenceCount);
    REGISTER_TEST(BaslValueTest, ValueInitObjectTransfersOwnershipAndCopyRetains);
    REGISTER_TEST(BaslValueTest, ValueReleaseOnImmediateResetsToNil);
    REGISTER_TEST(BaslValueTest, StringObjectUsesRuntimeAllocatorHooks);
    REGISTER_TEST(BaslValueTest, StringObjectValidatesArguments);
    REGISTER_TEST(BaslValueTest, FunctionObjectTakesOwnershipOfChunkAndExposesMetadata);
    REGISTER_TEST(BaslValueTest, FunctionObjectValidatesArguments);
    REGISTER_TEST(BaslValueTest, InstanceObjectStoresAndUpdatesFields);
    REGISTER_TEST(BaslValueTest, ArrayAndMapObjectsStoreAndExposeIndexedValues);
}
