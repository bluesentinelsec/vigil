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


TEST(VigilStringTest, InitStartsEmptyAndNullTerminated) {
    vigil_string_t string;

    vigil_string_init(&string, NULL);

    EXPECT_EQ(vigil_string_length(&string), 0U);
    EXPECT_STREQ(vigil_string_c_str(&string), "");
}

TEST(VigilStringTest, AssignCstrSetsLengthAndTerminator) {
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_string_t string;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&string, runtime);

    ASSERT_EQ(
        vigil_string_assign_cstr(&string, "hello", &error),
        VIGIL_STATUS_OK
    );
    EXPECT_EQ(vigil_string_length(&string), 5U);
    EXPECT_STREQ(vigil_string_c_str(&string), "hello");
    EXPECT_EQ(string.bytes.data[5], '\0');

    vigil_string_free(&string);
    vigil_runtime_close(&runtime);
}

TEST(VigilStringTest, AppendPreservesExistingContents) {
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_string_t string;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&string, runtime);

    ASSERT_EQ(vigil_string_assign_cstr(&string, "bas", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_append_cstr(&string, "l", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(
        vigil_string_append(&string, " runtime", strlen(" runtime"), &error),
        VIGIL_STATUS_OK
    );

    EXPECT_EQ(vigil_string_length(&string), strlen("vigil runtime"));
    EXPECT_STREQ(vigil_string_c_str(&string), "vigil runtime");
    EXPECT_EQ(string.bytes.data[vigil_string_length(&string)], '\0');

    vigil_string_free(&string);
    vigil_runtime_close(&runtime);
}

TEST(VigilStringTest, ClearResetsToEmptyButKeepsUsableStorage) {
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_string_t string;
    size_t capacity;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&string, runtime);
    ASSERT_EQ(vigil_string_assign_cstr(&string, "hello", &error), VIGIL_STATUS_OK);
    capacity = string.bytes.capacity;

    vigil_string_clear(&string);

    EXPECT_EQ(vigil_string_length(&string), 0U);
    EXPECT_STREQ(vigil_string_c_str(&string), "");
    EXPECT_EQ(string.bytes.capacity, capacity);

    ASSERT_EQ(vigil_string_append_cstr(&string, "world", &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(vigil_string_c_str(&string), "world");

    vigil_string_free(&string);
    vigil_runtime_close(&runtime);
}

TEST(VigilStringTest, FreeResetsWholeString) {
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_string_t string;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&string, runtime);
    ASSERT_EQ(vigil_string_assign_cstr(&string, "hello", &error), VIGIL_STATUS_OK);

    vigil_string_free(&string);

    EXPECT_EQ(string.bytes.runtime, NULL);
    EXPECT_EQ(string.bytes.data, NULL);
    EXPECT_EQ(string.bytes.length, 0U);
    EXPECT_EQ(string.bytes.capacity, 0U);
    EXPECT_STREQ(vigil_string_c_str(&string), "");

    vigil_runtime_close(&runtime);
}

TEST(VigilStringTest, CompareAndEqualsUseLexicographicOrder) {
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_string_t left;
    vigil_string_t right;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&left, runtime);
    vigil_string_init(&right, runtime);

    ASSERT_EQ(vigil_string_assign_cstr(&left, "alpha", &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_string_assign_cstr(&right, "beta", &error), VIGIL_STATUS_OK);

    EXPECT_LT(vigil_string_compare(&left, &right), 0);
    EXPECT_GT(vigil_string_compare(&right, &left), 0);
    EXPECT_TRUE(vigil_string_equals_cstr(&left, "alpha"));
    EXPECT_FALSE(vigil_string_equals_cstr(&left, "alp"));

    vigil_string_free(&left);
    vigil_string_free(&right);
    vigil_runtime_close(&runtime);
}

TEST(VigilStringTest, ReservePreparesStorageAndTerminator) {
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_string_t string;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&string, runtime);

    ASSERT_EQ(vigil_string_reserve(&string, 32U, &error), VIGIL_STATUS_OK);
    EXPECT_GE(string.bytes.capacity, 33U);
    EXPECT_EQ(vigil_string_length(&string), 0U);
    EXPECT_STREQ(vigil_string_c_str(&string), "");

    vigil_string_free(&string);
    vigil_runtime_close(&runtime);
}

TEST(VigilStringTest, UsesRuntimeAllocatorHooks) {
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_string_t string;
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
    vigil_string_init(&string, runtime);

    ASSERT_EQ(
        vigil_string_assign_cstr(&string, "0123456789abcdef", &error),
        VIGIL_STATUS_OK
    );
    ASSERT_EQ(
        vigil_string_append_cstr(&string, "0123456789abcdef", &error),
        VIGIL_STATUS_OK
    );

    EXPECT_GE(stats.allocate_calls, 2);
    EXPECT_GE(stats.reallocate_calls, 1);

    vigil_string_free(&string);
    EXPECT_GE(stats.deallocate_calls, 1);
    vigil_runtime_close(&runtime);
}

TEST(VigilStringTest, RejectsMissingRuntimeForMutation) {
    vigil_string_t string;
    vigil_error_t error = {0};

    vigil_string_init(&string, NULL);

    EXPECT_EQ(
        vigil_string_assign_cstr(&string, "hello", &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "string runtime must not be null"), 0);
}

TEST(VigilStringTest, RejectsNullValueEvenForEmptyOperations) {
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_string_t string;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&string, runtime);

    EXPECT_EQ(
        vigil_string_assign(&string, NULL, 0U, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "string value must not be null"), 0);

    EXPECT_EQ(
        vigil_string_append(&string, NULL, 0U, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "string value must not be null"), 0);

    EXPECT_EQ(
        vigil_string_assign_cstr(&string, NULL, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(
        vigil_string_append_cstr(&string, NULL, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );

    vigil_string_free(&string);
    vigil_runtime_close(&runtime);
}

TEST(VigilStringTest, SelfAppendIsSafe) {
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_string_t string;
    const char *value;
    size_t length;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_string_init(&string, runtime);
    ASSERT_EQ(vigil_string_assign_cstr(&string, "echo", &error), VIGIL_STATUS_OK);

    value = vigil_string_c_str(&string);
    length = vigil_string_length(&string);
    ASSERT_EQ(
        vigil_string_append(&string, value, length, &error),
        VIGIL_STATUS_OK
    );
    EXPECT_STREQ(vigil_string_c_str(&string), "echoecho");

    vigil_string_free(&string);
    vigil_runtime_close(&runtime);
}

void register_string_tests(void) {
    REGISTER_TEST(VigilStringTest, InitStartsEmptyAndNullTerminated);
    REGISTER_TEST(VigilStringTest, AssignCstrSetsLengthAndTerminator);
    REGISTER_TEST(VigilStringTest, AppendPreservesExistingContents);
    REGISTER_TEST(VigilStringTest, ClearResetsToEmptyButKeepsUsableStorage);
    REGISTER_TEST(VigilStringTest, FreeResetsWholeString);
    REGISTER_TEST(VigilStringTest, CompareAndEqualsUseLexicographicOrder);
    REGISTER_TEST(VigilStringTest, ReservePreparesStorageAndTerminator);
    REGISTER_TEST(VigilStringTest, UsesRuntimeAllocatorHooks);
    REGISTER_TEST(VigilStringTest, RejectsMissingRuntimeForMutation);
    REGISTER_TEST(VigilStringTest, RejectsNullValueEvenForEmptyOperations);
    REGISTER_TEST(VigilStringTest, SelfAppendIsSafe);
}
