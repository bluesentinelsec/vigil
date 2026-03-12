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

}  // namespace

TEST(BaslValueTest, ImmediateValuesRoundTrip) {
    basl_value_t value;

    basl_value_init_nil(&value);
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_NIL);
    EXPECT_EQ(basl_value_as_object(&value), nullptr);

    basl_value_init_bool(&value, true);
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_BOOL);
    EXPECT_TRUE(basl_value_as_bool(&value));

    basl_value_init_int(&value, 42);
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&value), 42);

    basl_value_init_float(&value, 3.5);
    EXPECT_EQ(basl_value_kind(&value), BASL_VALUE_FLOAT);
    EXPECT_DOUBLE_EQ(basl_value_as_float(&value), 3.5);
}

TEST(BaslValueTest, StringObjectStartsWithOneReferenceAndExposesText) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_object_t *object = nullptr;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "hello", &object, &error),
        BASL_STATUS_OK
    );

    ASSERT_NE(object, nullptr);
    EXPECT_EQ(basl_object_type(object), BASL_OBJECT_STRING);
    EXPECT_EQ(basl_object_ref_count(object), 1U);
    EXPECT_EQ(basl_string_object_length(object), 5U);
    EXPECT_STREQ(basl_string_object_c_str(object), "hello");

    basl_object_release(&object);
    EXPECT_EQ(object, nullptr);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, ObjectRetainAndReleaseUpdateReferenceCount) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_object_t *left = nullptr;
    basl_object_t *right = nullptr;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "hello", &left, &error),
        BASL_STATUS_OK
    );

    right = left;
    basl_object_retain(right);
    EXPECT_EQ(basl_object_ref_count(left), 2U);

    basl_object_release(&left);
    EXPECT_EQ(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(basl_object_ref_count(right), 1U);

    basl_object_release(&right);
    EXPECT_EQ(right, nullptr);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, ValueInitObjectTransfersOwnershipAndCopyRetains) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_object_t *object = nullptr;
    basl_value_t value;
    basl_value_t copy;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "value", &object, &error),
        BASL_STATUS_OK
    );

    basl_value_init_object(&value, &object);
    EXPECT_EQ(object, nullptr);
    ASSERT_NE(basl_value_as_object(&value), nullptr);
    EXPECT_EQ(basl_object_ref_count(basl_value_as_object(&value)), 1U);

    copy = basl_value_copy(&value);
    ASSERT_NE(basl_value_as_object(&copy), nullptr);
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
    EXPECT_EQ(basl_value_as_object(&value), nullptr);
}

TEST(BaslValueTest, StringObjectUsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_object_t *object = nullptr;
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
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "allocator-backed", &object, &error),
        BASL_STATUS_OK
    );

    EXPECT_GE(stats.allocate_calls, 3);

    basl_object_release(&object);
    EXPECT_EQ(object, nullptr);
    EXPECT_GE(stats.deallocate_calls, 2);

    basl_runtime_close(&runtime);
    EXPECT_GE(stats.deallocate_calls, 3);
}

TEST(BaslValueTest, StringObjectValidatesArguments) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_object_t *object = nullptr;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);

    EXPECT_EQ(
        basl_string_object_new(nullptr, "hello", 5U, &object, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "runtime must not be null"), 0);

    EXPECT_EQ(
        basl_string_object_new(runtime, nullptr, 0U, &object, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "string object value must not be null"), 0);

    EXPECT_EQ(
        basl_string_object_new(runtime, "hello", 5U, nullptr, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "out_object must not be null"), 0);

    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, FunctionObjectTakesOwnershipOfChunkAndExposesMetadata) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_chunk_t chunk;
    basl_object_t *function = nullptr;
    basl_value_t value;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, runtime);
    basl_value_init_int(&value, 42);
    ASSERT_EQ(
        basl_chunk_write_constant(&chunk, &value, {}, nullptr, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_function_object_new_cstr(runtime, "main", 0U, &chunk, &function, &error),
        BASL_STATUS_OK
    );

    ASSERT_NE(function, nullptr);
    EXPECT_EQ(basl_object_type(function), BASL_OBJECT_FUNCTION);
    EXPECT_EQ(basl_object_ref_count(function), 1U);
    EXPECT_STREQ(basl_function_object_name(function), "main");
    EXPECT_EQ(basl_function_object_arity(function), 0U);
    ASSERT_NE(basl_function_object_chunk(function), nullptr);
    EXPECT_EQ(basl_chunk_constant_count(basl_function_object_chunk(function)), 1U);

    EXPECT_EQ(chunk.runtime, nullptr);
    EXPECT_EQ(basl_chunk_code_size(&chunk), 0U);
    EXPECT_EQ(basl_chunk_constant_count(&chunk), 0U);

    basl_object_release(&function);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, FunctionObjectValidatesArguments) {
    basl_runtime_t *runtime = nullptr;
    basl_runtime_t *other_runtime = nullptr;
    basl_error_t error = {};
    basl_chunk_t chunk;
    basl_object_t *function = nullptr;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_runtime_open(&other_runtime, nullptr, &error), BASL_STATUS_OK);
    basl_chunk_init(&chunk, other_runtime);

    EXPECT_EQ(
        basl_function_object_new(nullptr, "main", 4U, 0U, &chunk, &function, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        basl_function_object_new(runtime, nullptr, 0U, 0U, &chunk, &function, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        basl_function_object_new(runtime, "main", 4U, 0U, nullptr, &function, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        basl_function_object_new(runtime, "main", 4U, 0U, &chunk, nullptr, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        basl_function_object_new(runtime, "main", 4U, 0U, &chunk, &function, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(
        std::strcmp(error.value, "function object chunk runtime must match runtime"),
        0
    );

    basl_chunk_free(&chunk);
    basl_runtime_close(&other_runtime);
    basl_runtime_close(&runtime);
}

TEST(BaslValueTest, InstanceObjectStoresAndUpdatesFields) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_object_t *instance = nullptr;
    basl_value_t fields[2];
    basl_value_t field_value;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_value_init_int(&fields[0], 3);
    basl_value_init_bool(&fields[1], true);

    ASSERT_EQ(
        basl_instance_object_new(runtime, 3U, fields, 2U, &instance, &error),
        BASL_STATUS_OK
    );
    ASSERT_NE(instance, nullptr);
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
