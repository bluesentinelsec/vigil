#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "basl/basl.h"
}

namespace {

struct AllocatorStats {
    int allocate_calls;
    int deallocate_calls;
};

void *CountedAllocate(void *user_data, size_t size) {
    AllocatorStats *stats = static_cast<AllocatorStats *>(user_data);

    stats->allocate_calls += 1;
    return std::malloc(size);
}

void *CountedReallocate(void *user_data, void *memory, size_t size) {
    (void)user_data;
    return std::realloc(memory, size);
}

void CountedDeallocate(void *user_data, void *memory) {
    AllocatorStats *stats = static_cast<AllocatorStats *>(user_data);

    stats->deallocate_calls += 1;
    std::free(memory);
}

void ExpectClearedLocation(const basl_source_location_t &location) {
    EXPECT_EQ(location.source_id, 0U);
    EXPECT_EQ(location.line, 0U);
    EXPECT_EQ(location.column, 0U);
}

}  // namespace

TEST(BaslTest, SumAddsTwoIntegers) {
    EXPECT_EQ(basl_sum(2, 3), 5);
}

TEST(BaslTest, SumHandlesNegativeValues) {
    EXPECT_EQ(basl_sum(-2, 3), 1);
}

TEST(BaslTest, RuntimeOpensAndClosesWithDefaultAllocator) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};

    EXPECT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    EXPECT_NE(runtime, nullptr);
    EXPECT_EQ(error.type, BASL_STATUS_OK);
    EXPECT_EQ(error.value, nullptr);
    EXPECT_EQ(error.length, static_cast<size_t>(0));
    ExpectClearedLocation(error.location);
    EXPECT_NE(basl_runtime_allocator(runtime), nullptr);

    basl_runtime_close(runtime);
}

TEST(BaslTest, RuntimeOpenValidatesArguments) {
    basl_error_t error = {};

    EXPECT_EQ(basl_runtime_open(nullptr, nullptr, &error), BASL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "out_runtime must not be null"), 0);
    EXPECT_EQ(error.length, std::strlen(error.value));
    ExpectClearedLocation(error.location);
}

TEST(BaslTest, RuntimeUsesCustomAllocatorHooks) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    AllocatorStats stats = {};
    basl_allocator_t allocator = {};
    basl_runtime_options_t options = {};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;

    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    EXPECT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    ASSERT_NE(runtime, nullptr);
    EXPECT_EQ(stats.allocate_calls, 1);
    EXPECT_EQ(stats.deallocate_calls, 0);
    ASSERT_NE(basl_runtime_allocator(runtime), nullptr);
    EXPECT_EQ(basl_runtime_allocator(runtime)->user_data, &stats);

    basl_runtime_close(runtime);
    EXPECT_EQ(stats.deallocate_calls, 1);
}

TEST(BaslTest, RuntimeOptionsInitClearsFields) {
    basl_runtime_options_t options = {};
    basl_allocator_t allocator = {};

    options.allocator = &allocator;
    basl_runtime_options_init(&options);

    EXPECT_EQ(options.allocator, nullptr);
}

TEST(BaslTest, RuntimeRejectsIncompleteAllocator) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_allocator_t allocator = {};
    basl_runtime_options_t options = {};

    allocator.allocate = CountedAllocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    EXPECT_EQ(
        basl_runtime_open(&runtime, &options, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(runtime, nullptr);
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(
        std::strcmp(error.value, "allocator must define allocate and deallocate"),
        0
    );
    EXPECT_EQ(error.length, std::strlen(error.value));
}

TEST(BaslTest, StatusNamesAreStable) {
    EXPECT_STREQ(basl_status_name(BASL_STATUS_OK), "ok");
    EXPECT_STREQ(
        basl_status_name(BASL_STATUS_INVALID_ARGUMENT),
        "invalid_argument"
    );
    EXPECT_STREQ(basl_status_name(BASL_STATUS_OUT_OF_MEMORY), "out_of_memory");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_INTERNAL), "internal");
}

TEST(BaslTest, ErrorClearResetsSourceLocation) {
    basl_error_t error = {};

    error.type = BASL_STATUS_INTERNAL;
    error.value = "bad";
    error.length = 3U;
    error.location.source_id = 7U;
    error.location.line = 11U;
    error.location.column = 13U;

    basl_error_clear(&error);

    EXPECT_EQ(error.type, BASL_STATUS_OK);
    EXPECT_EQ(error.value, nullptr);
    EXPECT_EQ(error.length, 0U);
    ExpectClearedLocation(error.location);
}

TEST(BaslTest, SourceLocationClearResetsFields) {
    basl_source_location_t location = {};

    location.source_id = 3U;
    location.line = 5U;
    location.column = 8U;

    basl_source_location_clear(&location);

    ExpectClearedLocation(location);
}
