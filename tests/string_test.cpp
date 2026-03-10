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

TEST(BaslStringTest, InitStartsEmptyAndNullTerminated) {
    basl_string_t string;

    basl_string_init(&string, nullptr);

    EXPECT_EQ(basl_string_length(&string), 0U);
    EXPECT_STREQ(basl_string_c_str(&string), "");
}

TEST(BaslStringTest, AssignCstrSetsLengthAndTerminator) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_string_t string;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_string_init(&string, runtime);

    ASSERT_EQ(
        basl_string_assign_cstr(&string, "hello", &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(basl_string_length(&string), 5U);
    EXPECT_STREQ(basl_string_c_str(&string), "hello");
    EXPECT_EQ(string.bytes.data[5], '\0');

    basl_string_free(&string);
    basl_runtime_close(&runtime);
}

TEST(BaslStringTest, AppendPreservesExistingContents) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_string_t string;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_string_init(&string, runtime);

    ASSERT_EQ(basl_string_assign_cstr(&string, "bas", &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_string_append_cstr(&string, "l", &error), BASL_STATUS_OK);
    ASSERT_EQ(
        basl_string_append(&string, " runtime", std::strlen(" runtime"), &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(basl_string_length(&string), std::strlen("basl runtime"));
    EXPECT_STREQ(basl_string_c_str(&string), "basl runtime");
    EXPECT_EQ(string.bytes.data[basl_string_length(&string)], '\0');

    basl_string_free(&string);
    basl_runtime_close(&runtime);
}

TEST(BaslStringTest, ClearResetsToEmptyButKeepsUsableStorage) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_string_t string;
    size_t capacity;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_string_init(&string, runtime);
    ASSERT_EQ(basl_string_assign_cstr(&string, "hello", &error), BASL_STATUS_OK);
    capacity = string.bytes.capacity;

    basl_string_clear(&string);

    EXPECT_EQ(basl_string_length(&string), 0U);
    EXPECT_STREQ(basl_string_c_str(&string), "");
    EXPECT_EQ(string.bytes.capacity, capacity);

    ASSERT_EQ(basl_string_append_cstr(&string, "world", &error), BASL_STATUS_OK);
    EXPECT_STREQ(basl_string_c_str(&string), "world");

    basl_string_free(&string);
    basl_runtime_close(&runtime);
}

TEST(BaslStringTest, FreeResetsWholeString) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_string_t string;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_string_init(&string, runtime);
    ASSERT_EQ(basl_string_assign_cstr(&string, "hello", &error), BASL_STATUS_OK);

    basl_string_free(&string);

    EXPECT_EQ(string.bytes.runtime, nullptr);
    EXPECT_EQ(string.bytes.data, nullptr);
    EXPECT_EQ(string.bytes.length, 0U);
    EXPECT_EQ(string.bytes.capacity, 0U);
    EXPECT_STREQ(basl_string_c_str(&string), "");

    basl_runtime_close(&runtime);
}

TEST(BaslStringTest, CompareAndEqualsUseLexicographicOrder) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_string_t left;
    basl_string_t right;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_string_init(&left, runtime);
    basl_string_init(&right, runtime);

    ASSERT_EQ(basl_string_assign_cstr(&left, "alpha", &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_string_assign_cstr(&right, "beta", &error), BASL_STATUS_OK);

    EXPECT_LT(basl_string_compare(&left, &right), 0);
    EXPECT_GT(basl_string_compare(&right, &left), 0);
    EXPECT_TRUE(basl_string_equals_cstr(&left, "alpha"));
    EXPECT_FALSE(basl_string_equals_cstr(&left, "alp"));

    basl_string_free(&left);
    basl_string_free(&right);
    basl_runtime_close(&runtime);
}

TEST(BaslStringTest, ReservePreparesStorageAndTerminator) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_string_t string;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_string_init(&string, runtime);

    ASSERT_EQ(basl_string_reserve(&string, 32U, &error), BASL_STATUS_OK);
    EXPECT_GE(string.bytes.capacity, 33U);
    EXPECT_EQ(basl_string_length(&string), 0U);
    EXPECT_STREQ(basl_string_c_str(&string), "");

    basl_string_free(&string);
    basl_runtime_close(&runtime);
}

TEST(BaslStringTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_string_t string;
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
    basl_string_init(&string, runtime);

    ASSERT_EQ(
        basl_string_assign_cstr(&string, "0123456789abcdef", &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_string_append_cstr(&string, "0123456789abcdef", &error),
        BASL_STATUS_OK
    );

    EXPECT_GE(stats.allocate_calls, 2);
    EXPECT_GE(stats.reallocate_calls, 1);

    basl_string_free(&string);
    EXPECT_GE(stats.deallocate_calls, 1);
    basl_runtime_close(&runtime);
}

TEST(BaslStringTest, RejectsMissingRuntimeForMutation) {
    basl_string_t string;
    basl_error_t error = {};

    basl_string_init(&string, nullptr);

    EXPECT_EQ(
        basl_string_assign_cstr(&string, "hello", &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "string runtime must not be null"), 0);
}

TEST(BaslStringTest, NullValueWithZeroLengthIsAllowed) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_string_t string;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_string_init(&string, runtime);

    ASSERT_EQ(basl_string_assign(&string, nullptr, 0U, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_string_length(&string), 0U);
    EXPECT_STREQ(basl_string_c_str(&string), "");

    ASSERT_EQ(basl_string_append(&string, nullptr, 0U, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_string_length(&string), 0U);
    EXPECT_STREQ(basl_string_c_str(&string), "");

    basl_string_free(&string);
    basl_runtime_close(&runtime);
}
