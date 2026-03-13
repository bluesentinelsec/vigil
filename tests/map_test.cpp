#include <gtest/gtest.h>

#include <cstdio>
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

TEST(BaslMapTest, InitStartsEmpty) {
    basl_map_t map;

    basl_map_init(&map, nullptr);

    EXPECT_EQ(map.runtime, nullptr);
    EXPECT_EQ(map.entries, nullptr);
    EXPECT_EQ(map.count, 0U);
    EXPECT_EQ(map.capacity, 0U);
    EXPECT_EQ(map.tombstone_count, 0U);
    EXPECT_EQ(basl_map_count(&map), 0U);
}

TEST(BaslMapTest, SetAndGetImmediateValue) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_map_t map;
    basl_value_t value;
    const basl_value_t *stored;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_map_init(&map, runtime);
    basl_value_init_int(&value, 42);

    ASSERT_EQ(basl_map_set_cstr(&map, "answer", &value, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_map_count(&map), 1U);
    EXPECT_TRUE(basl_map_contains_cstr(&map, "answer"));

    stored = basl_map_get_cstr(&map, "answer");
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(basl_value_kind(stored), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(stored), 42);

    basl_map_free(&map);
    basl_runtime_close(&runtime);
}

TEST(BaslMapTest, SupportsIntegerUnsignedAndBoolKeys) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_map_t map;
    basl_value_t int_key;
    basl_value_t uint_key;
    basl_value_t bool_key;
    basl_value_t value;
    const basl_value_t *stored;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_map_init(&map, runtime);

    basl_value_init_int(&int_key, 7);
    basl_value_init_uint(&uint_key, UINT64_C(9223372036854775808));
    basl_value_init_bool(&bool_key, true);
    basl_value_init_int(&value, 42);
    ASSERT_EQ(basl_map_set_value(&map, &int_key, &value, &error), BASL_STATUS_OK);
    basl_value_init_int(&value, 77);
    ASSERT_EQ(basl_map_set_value(&map, &uint_key, &value, &error), BASL_STATUS_OK);
    basl_value_init_int(&value, 99);
    ASSERT_EQ(basl_map_set_value(&map, &bool_key, &value, &error), BASL_STATUS_OK);

    stored = basl_map_get_value(&map, &int_key);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(basl_value_as_int(stored), 42);
    stored = basl_map_get_value(&map, &uint_key);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(basl_value_as_int(stored), 77);
    stored = basl_map_get_value(&map, &bool_key);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(basl_value_as_int(stored), 99);
    EXPECT_TRUE(basl_map_contains_value(&map, &int_key));
    EXPECT_TRUE(basl_map_contains_value(&map, &uint_key));
    EXPECT_TRUE(basl_map_contains_value(&map, &bool_key));

    basl_map_free(&map);
    basl_runtime_close(&runtime);
}

TEST(BaslMapTest, OverwriteReleasesPreviousObjectValue) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_map_t map;
    basl_object_t *first = nullptr;
    basl_object_t *second = nullptr;
    basl_object_t *held = nullptr;
    basl_value_t first_value;
    basl_value_t second_value;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_map_init(&map, runtime);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "first", &first, &error),
        BASL_STATUS_OK
    );
    basl_value_init_object(&first_value, &first);
    ASSERT_EQ(basl_map_set_cstr(&map, "key", &first_value, &error), BASL_STATUS_OK);
    basl_value_release(&first_value);

    held = basl_value_as_object(basl_map_get_cstr(&map, "key"));
    ASSERT_NE(held, nullptr);
    basl_object_retain(held);
    EXPECT_EQ(basl_object_ref_count(held), 2U);

    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "second", &second, &error),
        BASL_STATUS_OK
    );
    basl_value_init_object(&second_value, &second);
    ASSERT_EQ(basl_map_set_cstr(&map, "key", &second_value, &error), BASL_STATUS_OK);
    basl_value_release(&second_value);

    EXPECT_EQ(basl_object_ref_count(held), 1U);
    EXPECT_STREQ(basl_string_object_c_str(held), "first");
    EXPECT_STREQ(
        basl_string_object_c_str(basl_value_as_object(basl_map_get_cstr(&map, "key"))),
        "second"
    );

    basl_object_release(&held);
    basl_map_free(&map);
    basl_runtime_close(&runtime);
}

TEST(BaslMapTest, RemoveReportsPresenceAndReleasesValue) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_map_t map;
    basl_object_t *object = nullptr;
    basl_object_t *held = nullptr;
    basl_value_t value;
    int removed;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_map_init(&map, runtime);
    ASSERT_EQ(
        basl_string_object_new_cstr(runtime, "value", &object, &error),
        BASL_STATUS_OK
    );
    basl_value_init_object(&value, &object);
    ASSERT_EQ(basl_map_set_cstr(&map, "key", &value, &error), BASL_STATUS_OK);
    basl_value_release(&value);

    held = basl_value_as_object(basl_map_get_cstr(&map, "key"));
    ASSERT_NE(held, nullptr);
    basl_object_retain(held);

    removed = 0;
    ASSERT_EQ(
        basl_map_remove_cstr(&map, "key", &removed, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(basl_map_count(&map), 0U);
    EXPECT_FALSE(basl_map_contains_cstr(&map, "key"));
    EXPECT_EQ(basl_object_ref_count(held), 1U);

    removed = 0;
    ASSERT_EQ(
        basl_map_remove_cstr(&map, "key", &removed, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(removed, 0);

    basl_object_release(&held);
    basl_map_free(&map);
    basl_runtime_close(&runtime);
}

TEST(BaslMapTest, GrowthPreservesInsertedValues) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_map_t map;
    char key[32];
    size_t index;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_map_init(&map, runtime);

    for (index = 0U; index < 128U; index += 1U) {
        basl_value_t value;

        std::snprintf(key, sizeof(key), "key-%zu", index);
        basl_value_init_int(&value, static_cast<int64_t>(index));
        ASSERT_EQ(basl_map_set_cstr(&map, key, &value, &error), BASL_STATUS_OK);
    }

    EXPECT_EQ(basl_map_count(&map), 128U);
    for (index = 0U; index < 128U; index += 1U) {
        const basl_value_t *stored;

        std::snprintf(key, sizeof(key), "key-%zu", index);
        stored = basl_map_get_cstr(&map, key);
        ASSERT_NE(stored, nullptr);
        EXPECT_EQ(basl_value_as_int(stored), static_cast<int64_t>(index));
    }

    basl_map_free(&map);
    basl_runtime_close(&runtime);
}

TEST(BaslMapTest, ClearKeepsMapReusable) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_map_t map;
    basl_value_t value;
    size_t capacity;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_map_init(&map, runtime);

    basl_value_init_bool(&value, true);
    ASSERT_EQ(basl_map_set_cstr(&map, "a", &value, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_map_set_cstr(&map, "b", &value, &error), BASL_STATUS_OK);
    capacity = map.capacity;

    basl_map_clear(&map);
    EXPECT_EQ(basl_map_count(&map), 0U);
    EXPECT_EQ(map.capacity, capacity);
    EXPECT_EQ(map.tombstone_count, 0U);
    EXPECT_FALSE(basl_map_contains_cstr(&map, "a"));

    basl_value_init_int(&value, 7);
    ASSERT_EQ(basl_map_set_cstr(&map, "c", &value, &error), BASL_STATUS_OK);
    ASSERT_NE(basl_map_get_cstr(&map, "c"), nullptr);
    EXPECT_EQ(basl_value_as_int(basl_map_get_cstr(&map, "c")), 7);

    basl_map_free(&map);
    basl_runtime_close(&runtime);
}

TEST(BaslMapTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_map_t map;
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
    basl_map_init(&map, runtime);
    basl_value_init_int(&value, 1);

    ASSERT_EQ(basl_map_set_cstr(&map, "first", &value, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_map_set_cstr(&map, "second", &value, &error), BASL_STATUS_OK);

    EXPECT_GE(stats.allocate_calls, 3);

    basl_map_free(&map);
    EXPECT_GE(stats.deallocate_calls, 2);
    basl_runtime_close(&runtime);
}

TEST(BaslMapTest, RejectsMissingRuntimeAndInvalidArguments) {
    basl_map_t map;
    basl_value_t value;
    basl_value_t key;
    basl_error_t error = {};
    int removed;

    basl_map_init(&map, nullptr);
    basl_value_init_int(&value, 1);

    EXPECT_EQ(
        basl_map_set_cstr(&map, "key", &value, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "map runtime must not be null"), 0);

    EXPECT_EQ(
        basl_map_set(&map, nullptr, 0U, &value, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "map runtime must not be null"), 0);

    removed = 0;
    EXPECT_EQ(
        basl_map_remove_cstr(&map, "key", &removed, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "map runtime must not be null"), 0);

    basl_runtime_t *runtime = nullptr;
    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_map_init(&map, runtime);
    basl_value_init_float(&key, 1.5);
    EXPECT_EQ(
        basl_map_set_value(&map, &key, &value, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "map key must be an integer, bool, or string"), 0);
    basl_map_free(&map);
    basl_runtime_close(&runtime);
}
