#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#include "internal/vigil_nanbox.h"
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

TEST(VigilValueTest, ImmediateValuesRoundTrip)
{
    vigil_value_t value;

    vigil_value_init_nil(&value);
    EXPECT_EQ(vigil_value_kind(&value), VIGIL_VALUE_NIL);
    EXPECT_EQ(vigil_value_as_object(&value), NULL);

    vigil_value_init_bool(&value, true);
    EXPECT_EQ(vigil_value_kind(&value), VIGIL_VALUE_BOOL);
    EXPECT_TRUE(vigil_value_as_bool(&value));

    vigil_value_init_int(&value, 42);
    EXPECT_EQ(vigil_value_kind(&value), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&value), 42);

    vigil_value_init_uint(&value, UINT64_C(9223372036854775808));
    EXPECT_EQ(vigil_value_kind(&value), VIGIL_VALUE_UINT);
    EXPECT_EQ(vigil_value_as_uint(&value), UINT64_C(9223372036854775808));
    vigil_value_release(&value);

    vigil_value_init_float(&value, 3.5);
    EXPECT_EQ(vigil_value_kind(&value), VIGIL_VALUE_FLOAT);
    EXPECT_DOUBLE_EQ(vigil_value_as_float(&value), 3.5);
}

TEST(VigilValueTest, StringObjectStartsWithOneReferenceAndExposesText)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_object_t *object = NULL;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "hello", &object, &error), VIGIL_STATUS_OK);

    ASSERT_NE(object, NULL);
    EXPECT_EQ(vigil_object_type(object), VIGIL_OBJECT_STRING);
    EXPECT_EQ(vigil_object_ref_count(object), 1U);
    EXPECT_EQ(vigil_string_object_length(object), 5U);
    EXPECT_STREQ(vigil_string_object_c_str(object), "hello");

    vigil_object_release(&object);
    EXPECT_EQ(object, NULL);
    vigil_runtime_close(&runtime);
}

TEST(VigilValueTest, ObjectRetainAndReleaseUpdateReferenceCount)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_object_t *left = NULL;
    vigil_object_t *right = NULL;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "hello", &left, &error), VIGIL_STATUS_OK);

    right = left;
    vigil_object_retain(right);
    EXPECT_EQ(vigil_object_ref_count(left), 2U);

    vigil_object_release(&left);
    EXPECT_EQ(left, NULL);
    ASSERT_NE(right, NULL);
    EXPECT_EQ(vigil_object_ref_count(right), 1U);

    vigil_object_release(&right);
    EXPECT_EQ(right, NULL);
    vigil_runtime_close(&runtime);
}

TEST(VigilValueTest, ValueInitObjectTransfersOwnershipAndCopyRetains)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_object_t *object = NULL;
    vigil_value_t value;
    vigil_value_t copy;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "value", &object, &error), VIGIL_STATUS_OK);

    vigil_value_init_object(&value, &object);
    EXPECT_EQ(object, NULL);
    ASSERT_NE(vigil_value_as_object(&value), NULL);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(&value)), 1U);

    copy = vigil_value_copy(&value);
    ASSERT_NE(vigil_value_as_object(&copy), NULL);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(&value)), 2U);

    vigil_value_release(&value);
    EXPECT_EQ(vigil_value_kind(&value), VIGIL_VALUE_NIL);
    EXPECT_EQ(vigil_object_ref_count(vigil_value_as_object(&copy)), 1U);

    vigil_value_release(&copy);
    EXPECT_EQ(vigil_value_kind(&copy), VIGIL_VALUE_NIL);
    vigil_runtime_close(&runtime);
}

TEST(VigilValueTest, ValueReleaseOnImmediateResetsToNil)
{
    vigil_value_t value;

    vigil_value_init_int(&value, 7);
    vigil_value_release(&value);

    EXPECT_EQ(vigil_value_kind(&value), VIGIL_VALUE_NIL);
    EXPECT_EQ(vigil_value_as_object(&value), NULL);
}

TEST(VigilValueTest, StringObjectUsesRuntimeAllocatorHooks)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_object_t *object = NULL;
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
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "allocator-backed", &object, &error), VIGIL_STATUS_OK);

    EXPECT_GE(stats.allocate_calls, 3);

    vigil_object_release(&object);
    EXPECT_EQ(object, NULL);
    EXPECT_GE(stats.deallocate_calls, 1);

    vigil_runtime_close(&runtime);
    EXPECT_GE(stats.deallocate_calls, 2);
}

TEST(VigilValueTest, StringObjectValidatesArguments)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_object_t *object = NULL;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_string_object_new(NULL, "hello", 5U, &object, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "runtime must not be null"), 0);

    EXPECT_EQ(vigil_string_object_new(runtime, NULL, 0U, &object, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "string object value must not be null"), 0);

    EXPECT_EQ(vigil_string_object_new(runtime, "hello", 5U, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "out_object must not be null"), 0);

    vigil_runtime_close(&runtime);
}

TEST(VigilValueTest, FunctionObjectTakesOwnershipOfChunkAndExposesMetadata)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_object_t *function = NULL;
    vigil_value_t value;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, runtime);
    vigil_value_init_int(&value, 42);
    ASSERT_EQ(vigil_chunk_write_constant(&chunk, &value, (vigil_source_span_t){0}, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_function_object_new_cstr(runtime, "main", 0U, 1U, &chunk, &function, &error), VIGIL_STATUS_OK);

    ASSERT_NE(function, NULL);
    EXPECT_EQ(vigil_object_type(function), VIGIL_OBJECT_FUNCTION);
    EXPECT_EQ(vigil_object_ref_count(function), 1U);
    EXPECT_STREQ(vigil_function_object_name(function), "main");
    EXPECT_EQ(vigil_function_object_arity(function), 0U);
    ASSERT_NE(vigil_function_object_chunk(function), NULL);
    EXPECT_EQ(vigil_chunk_constant_count(vigil_function_object_chunk(function)), 1U);

    EXPECT_EQ(chunk.runtime, NULL);
    EXPECT_EQ(vigil_chunk_code_size(&chunk), 0U);
    EXPECT_EQ(vigil_chunk_constant_count(&chunk), 0U);

    vigil_object_release(&function);
    vigil_runtime_close(&runtime);
}

TEST(VigilValueTest, FunctionObjectValidatesArguments)
{
    vigil_runtime_t *runtime = NULL;
    vigil_runtime_t *other_runtime = NULL;
    vigil_error_t error = {0};
    vigil_chunk_t chunk;
    vigil_object_t *function = NULL;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_runtime_open(&other_runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_chunk_init(&chunk, other_runtime);

    EXPECT_EQ(vigil_function_object_new(NULL, "main", 4U, 0U, 1U, &chunk, &function, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_function_object_new(runtime, NULL, 0U, 0U, 1U, &chunk, &function, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_function_object_new(runtime, "main", 4U, 0U, 1U, NULL, &function, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_function_object_new(runtime, "main", 4U, 0U, 1U, &chunk, NULL, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_function_object_new(runtime, "main", 4U, 0U, 1U, &chunk, &function, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "function object chunk runtime must match runtime"), 0);

    vigil_chunk_free(&chunk);
    vigil_runtime_close(&other_runtime);
    vigil_runtime_close(&runtime);
}

TEST(VigilValueTest, InstanceObjectStoresAndUpdatesFields)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_object_t *instance = NULL;
    vigil_value_t fields[2];
    vigil_value_t field_value;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&fields[0], 3);
    vigil_value_init_bool(&fields[1], true);

    ASSERT_EQ(vigil_instance_object_new(runtime, 3U, fields, 2U, &instance, &error), VIGIL_STATUS_OK);
    ASSERT_NE(instance, NULL);
    EXPECT_EQ(vigil_object_type(instance), VIGIL_OBJECT_INSTANCE);
    EXPECT_EQ(vigil_instance_object_class_index(instance), 3U);
    EXPECT_EQ(vigil_instance_object_field_count(instance), 2U);

    vigil_value_init_nil(&field_value);
    ASSERT_TRUE(vigil_instance_object_get_field(instance, 0U, &field_value));
    EXPECT_EQ(vigil_value_kind(&field_value), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&field_value), 3);
    vigil_value_release(&field_value);

    vigil_value_init_int(&field_value, 9);
    ASSERT_EQ(vigil_instance_object_set_field(instance, 0U, &field_value, &error), VIGIL_STATUS_OK);
    vigil_value_release(&field_value);

    vigil_value_init_nil(&field_value);
    ASSERT_TRUE(vigil_instance_object_get_field(instance, 0U, &field_value));
    EXPECT_EQ(vigil_value_as_int(&field_value), 9);
    vigil_value_release(&field_value);

    vigil_value_release(&fields[0]);
    vigil_value_release(&fields[1]);
    vigil_object_release(&instance);
    vigil_runtime_close(&runtime);
}

TEST(VigilValueTest, ArrayAndMapObjectsStoreAndExposeIndexedValues)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_object_t *array_object = NULL;
    vigil_object_t *map_object = NULL;
    vigil_value_t items[2];
    vigil_value_t value;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&items[0], 3);
    vigil_value_init_int(&items[1], 7);

    ASSERT_EQ(vigil_array_object_new(runtime, items, 2U, &array_object, &error), VIGIL_STATUS_OK);
    ASSERT_NE(array_object, NULL);
    EXPECT_EQ(vigil_object_type(array_object), VIGIL_OBJECT_ARRAY);
    EXPECT_EQ(vigil_array_object_length(array_object), 2U);

    vigil_value_init_nil(&value);
    ASSERT_TRUE(vigil_array_object_get(array_object, 1U, &value));
    EXPECT_EQ(vigil_value_kind(&value), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&value), 7);
    vigil_value_release(&value);

    vigil_value_init_int(&value, 9);
    ASSERT_EQ(vigil_array_object_set(array_object, 0U, &value, &error), VIGIL_STATUS_OK);
    vigil_value_release(&value);

    vigil_value_init_nil(&value);
    ASSERT_TRUE(vigil_array_object_get(array_object, 0U, &value));
    EXPECT_EQ(vigil_value_as_int(&value), 9);
    vigil_value_release(&value);

    ASSERT_EQ(vigil_map_object_new(runtime, &map_object, &error), VIGIL_STATUS_OK);
    ASSERT_NE(map_object, NULL);
    EXPECT_EQ(vigil_object_type(map_object), VIGIL_OBJECT_MAP);
    EXPECT_EQ(vigil_map_object_count(map_object), 0U);

    vigil_value_init_int(&items[0], 1);
    vigil_value_init_int(&value, 11);
    ASSERT_EQ(vigil_map_object_set(map_object, &items[0], &value, &error), VIGIL_STATUS_OK);
    vigil_value_release(&value);
    EXPECT_EQ(vigil_map_object_count(map_object), 1U);

    vigil_value_init_nil(&value);
    ASSERT_TRUE(vigil_map_object_get(map_object, &items[0], &value));
    EXPECT_EQ(vigil_value_kind(&value), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(&value), 11);
    vigil_value_release(&value);

    vigil_value_release(&items[0]);
    vigil_value_release(&items[1]);
    vigil_object_release(&array_object);
    vigil_object_release(&map_object);
    vigil_runtime_close(&runtime);
}

TEST(VigilValueTest, NanboxI32RoundTripPreservesSign)
{
    /* Negative i32 values must survive encode → decode and
       encode → value_as_int round-trips with correct sign. */
    static const int32_t cases[] = {0, 1, -1, -2, INT32_MIN, INT32_MAX, -42, 42};
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        int32_t input = cases[i];
        uint64_t encoded = vigil_nanbox_encode_i32(input);
        int32_t decoded_i32 = vigil_nanbox_decode_i32(encoded);
        EXPECT_EQ(decoded_i32, input);

        /* Also verify the 48-bit decode path used by stringify. */
        vigil_value_t v = encoded;
        EXPECT_EQ(vigil_value_kind(&v), VIGIL_VALUE_INT);
        EXPECT_EQ((int32_t)vigil_value_as_int(&v), input);
    }
}

void register_value_tests(void)
{
    REGISTER_TEST(VigilValueTest, ImmediateValuesRoundTrip);
    REGISTER_TEST(VigilValueTest, StringObjectStartsWithOneReferenceAndExposesText);
    REGISTER_TEST(VigilValueTest, ObjectRetainAndReleaseUpdateReferenceCount);
    REGISTER_TEST(VigilValueTest, ValueInitObjectTransfersOwnershipAndCopyRetains);
    REGISTER_TEST(VigilValueTest, ValueReleaseOnImmediateResetsToNil);
    REGISTER_TEST(VigilValueTest, StringObjectUsesRuntimeAllocatorHooks);
    REGISTER_TEST(VigilValueTest, StringObjectValidatesArguments);
    REGISTER_TEST(VigilValueTest, FunctionObjectTakesOwnershipOfChunkAndExposesMetadata);
    REGISTER_TEST(VigilValueTest, FunctionObjectValidatesArguments);
    REGISTER_TEST(VigilValueTest, InstanceObjectStoresAndUpdatesFields);
    REGISTER_TEST(VigilValueTest, ArrayAndMapObjectsStoreAndExposeIndexedValues);
    REGISTER_TEST(VigilValueTest, NanboxI32RoundTripPreservesSign);
}
