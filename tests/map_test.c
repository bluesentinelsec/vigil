#include "vigil_test.h"

#include <stdio.h>
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

TEST(VigilMapTest, InitStartsEmpty)
{
    vigil_map_t map;

    vigil_map_init(&map, NULL);

    EXPECT_EQ(map.runtime, NULL);
    EXPECT_EQ(map.entries, NULL);
    EXPECT_EQ(map.count, 0U);
    EXPECT_EQ(map.capacity, 0U);
    EXPECT_EQ(map.tombstone_count, 0U);
    EXPECT_EQ(vigil_map_count(&map), 0U);
}

TEST(VigilMapTest, SetAndGetImmediateValue)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_map_t map;
    vigil_value_t value;
    const vigil_value_t *stored;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_map_init(&map, runtime);
    vigil_value_init_int(&value, 42);

    ASSERT_EQ(vigil_map_set_cstr(&map, "answer", &value, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_map_count(&map), 1U);
    EXPECT_TRUE(vigil_map_contains_cstr(&map, "answer"));

    stored = vigil_map_get_cstr(&map, "answer");
    ASSERT_NE(stored, NULL);
    EXPECT_EQ(vigil_value_kind(stored), VIGIL_VALUE_INT);
    EXPECT_EQ(vigil_value_as_int(stored), 42);

    vigil_map_free(&map);
    vigil_runtime_close(&runtime);
}

TEST(VigilMapTest, SupportsIntegerUnsignedAndBoolKeys)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_map_t map;
    vigil_value_t int_key;
    vigil_value_t uint_key;
    vigil_value_t bool_key;
    vigil_value_t value;
    const vigil_value_t *stored;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_map_init(&map, runtime);

    vigil_value_init_int(&int_key, 7);
    vigil_value_init_uint(&uint_key, UINT64_C(9223372036854775808));
    vigil_value_init_bool(&bool_key, true);
    vigil_value_init_int(&value, 42);
    ASSERT_EQ(vigil_map_set_value(&map, &int_key, &value, &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&value, 77);
    ASSERT_EQ(vigil_map_set_value(&map, &uint_key, &value, &error), VIGIL_STATUS_OK);
    vigil_value_init_int(&value, 99);
    ASSERT_EQ(vigil_map_set_value(&map, &bool_key, &value, &error), VIGIL_STATUS_OK);

    stored = vigil_map_get_value(&map, &int_key);
    ASSERT_NE(stored, NULL);
    EXPECT_EQ(vigil_value_as_int(stored), 42);
    stored = vigil_map_get_value(&map, &uint_key);
    ASSERT_NE(stored, NULL);
    EXPECT_EQ(vigil_value_as_int(stored), 77);
    stored = vigil_map_get_value(&map, &bool_key);
    ASSERT_NE(stored, NULL);
    EXPECT_EQ(vigil_value_as_int(stored), 99);
    EXPECT_TRUE(vigil_map_contains_value(&map, &int_key));
    EXPECT_TRUE(vigil_map_contains_value(&map, &uint_key));
    EXPECT_TRUE(vigil_map_contains_value(&map, &bool_key));

    vigil_value_release(&uint_key);
    vigil_map_free(&map);
    vigil_runtime_close(&runtime);
}

TEST(VigilMapTest, OverwriteReleasesPreviousObjectValue)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_map_t map;
    vigil_object_t *first = NULL;
    vigil_object_t *second = NULL;
    vigil_object_t *held = NULL;
    vigil_value_t first_value;
    vigil_value_t second_value;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_map_init(&map, runtime);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "first", &first, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&first_value, &first);
    ASSERT_EQ(vigil_map_set_cstr(&map, "key", &first_value, &error), VIGIL_STATUS_OK);
    vigil_value_release(&first_value);

    held = vigil_value_as_object(vigil_map_get_cstr(&map, "key"));
    ASSERT_NE(held, NULL);
    vigil_object_retain(held);
    EXPECT_EQ(vigil_object_ref_count(held), 2U);

    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "second", &second, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&second_value, &second);
    ASSERT_EQ(vigil_map_set_cstr(&map, "key", &second_value, &error), VIGIL_STATUS_OK);
    vigil_value_release(&second_value);

    EXPECT_EQ(vigil_object_ref_count(held), 1U);
    EXPECT_STREQ(vigil_string_object_c_str(held), "first");
    EXPECT_STREQ(vigil_string_object_c_str(vigil_value_as_object(vigil_map_get_cstr(&map, "key"))), "second");

    vigil_object_release(&held);
    vigil_map_free(&map);
    vigil_runtime_close(&runtime);
}

TEST(VigilMapTest, RemoveReportsPresenceAndReleasesValue)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_map_t map;
    vigil_object_t *object = NULL;
    vigil_object_t *held = NULL;
    vigil_value_t value;
    int removed;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_map_init(&map, runtime);
    ASSERT_EQ(vigil_string_object_new_cstr(runtime, "value", &object, &error), VIGIL_STATUS_OK);
    vigil_value_init_object(&value, &object);
    ASSERT_EQ(vigil_map_set_cstr(&map, "key", &value, &error), VIGIL_STATUS_OK);
    vigil_value_release(&value);

    held = vigil_value_as_object(vigil_map_get_cstr(&map, "key"));
    ASSERT_NE(held, NULL);
    vigil_object_retain(held);

    removed = 0;
    ASSERT_EQ(vigil_map_remove_cstr(&map, "key", &removed, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(vigil_map_count(&map), 0U);
    EXPECT_FALSE(vigil_map_contains_cstr(&map, "key"));
    EXPECT_EQ(vigil_object_ref_count(held), 1U);

    removed = 0;
    ASSERT_EQ(vigil_map_remove_cstr(&map, "key", &removed, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(removed, 0);

    vigil_object_release(&held);
    vigil_map_free(&map);
    vigil_runtime_close(&runtime);
}

TEST(VigilMapTest, GrowthPreservesInsertedValues)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_map_t map;
    char key[32];
    size_t index;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_map_init(&map, runtime);

    for (index = 0U; index < 128U; index += 1U)
    {
        vigil_value_t value;

        snprintf(key, sizeof(key), "key-%zu", index);
        vigil_value_init_int(&value, (int64_t)(index));
        ASSERT_EQ(vigil_map_set_cstr(&map, key, &value, &error), VIGIL_STATUS_OK);
    }

    EXPECT_EQ(vigil_map_count(&map), 128U);
    for (index = 0U; index < 128U; index += 1U)
    {
        const vigil_value_t *stored;

        snprintf(key, sizeof(key), "key-%zu", index);
        stored = vigil_map_get_cstr(&map, key);
        ASSERT_NE(stored, NULL);
        EXPECT_EQ(vigil_value_as_int(stored), (int64_t)(index));
    }

    vigil_map_free(&map);
    vigil_runtime_close(&runtime);
}

TEST(VigilMapTest, ClearKeepsMapReusable)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_map_t map;
    vigil_value_t value;
    size_t capacity;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_map_init(&map, runtime);

    vigil_value_init_bool(&value, true);
    ASSERT_EQ(vigil_map_set_cstr(&map, "a", &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_map_set_cstr(&map, "b", &value, &error), VIGIL_STATUS_OK);
    capacity = map.capacity;

    vigil_map_clear(&map);
    EXPECT_EQ(vigil_map_count(&map), 0U);
    EXPECT_EQ(map.capacity, capacity);
    EXPECT_EQ(map.tombstone_count, 0U);
    EXPECT_FALSE(vigil_map_contains_cstr(&map, "a"));

    vigil_value_init_int(&value, 7);
    ASSERT_EQ(vigil_map_set_cstr(&map, "c", &value, &error), VIGIL_STATUS_OK);
    ASSERT_NE(vigil_map_get_cstr(&map, "c"), NULL);
    EXPECT_EQ(vigil_value_as_int(vigil_map_get_cstr(&map, "c")), 7);

    vigil_map_free(&map);
    vigil_runtime_close(&runtime);
}

TEST(VigilMapTest, UsesRuntimeAllocatorHooks)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_map_t map;
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
    vigil_map_init(&map, runtime);
    vigil_value_init_int(&value, 1);

    ASSERT_EQ(vigil_map_set_cstr(&map, "first", &value, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_map_set_cstr(&map, "second", &value, &error), VIGIL_STATUS_OK);

    EXPECT_GE(stats.allocate_calls, 3);

    vigil_map_free(&map);
    EXPECT_GE(stats.deallocate_calls, 2);
    vigil_runtime_close(&runtime);
}

TEST(VigilMapTest, RejectsMissingRuntimeAndInvalidArguments)
{
    vigil_map_t map;
    vigil_value_t value;
    vigil_value_t key;
    vigil_error_t error = {0};
    int removed;

    vigil_map_init(&map, NULL);
    vigil_value_init_int(&value, 1);

    EXPECT_EQ(vigil_map_set_cstr(&map, "key", &value, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "map runtime must not be null"), 0);

    EXPECT_EQ(vigil_map_set(&map, NULL, 0U, &value, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "map runtime must not be null"), 0);

    removed = 0;
    EXPECT_EQ(vigil_map_remove_cstr(&map, "key", &removed, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "map runtime must not be null"), 0);

    vigil_runtime_t *runtime = NULL;
    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_map_init(&map, runtime);
    vigil_value_init_float(&key, 1.5);
    EXPECT_EQ(vigil_map_set_value(&map, &key, &value, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "map key must be an integer, bool, or string"), 0);
    vigil_map_free(&map);
    vigil_runtime_close(&runtime);
}

void register_map_tests(void)
{
    REGISTER_TEST(VigilMapTest, InitStartsEmpty);
    REGISTER_TEST(VigilMapTest, SetAndGetImmediateValue);
    REGISTER_TEST(VigilMapTest, SupportsIntegerUnsignedAndBoolKeys);
    REGISTER_TEST(VigilMapTest, OverwriteReleasesPreviousObjectValue);
    REGISTER_TEST(VigilMapTest, RemoveReportsPresenceAndReleasesValue);
    REGISTER_TEST(VigilMapTest, GrowthPreservesInsertedValues);
    REGISTER_TEST(VigilMapTest, ClearKeepsMapReusable);
    REGISTER_TEST(VigilMapTest, UsesRuntimeAllocatorHooks);
    REGISTER_TEST(VigilMapTest, RejectsMissingRuntimeAndInvalidArguments);
}
