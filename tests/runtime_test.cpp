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

struct FailingAllocatorState {
    int allocate_calls;
    int deallocate_calls;
    int fail_after;
};

void *CountedAllocate(void *user_data, size_t size) {
    AllocatorStats *stats = static_cast<AllocatorStats *>(user_data);

    stats->allocate_calls += 1;
    return std::malloc(size);
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

void *FailAllocate(void *user_data, size_t size) {
    FailingAllocatorState *state = static_cast<FailingAllocatorState *>(user_data);

    state->allocate_calls += 1;
    if (state->allocate_calls > state->fail_after) {
        return nullptr;
    }

    return std::malloc(size);
}

void FailDeallocate(void *user_data, void *memory) {
    FailingAllocatorState *state = static_cast<FailingAllocatorState *>(user_data);

    state->deallocate_calls += 1;
    std::free(memory);
}

void ExpectClearedLocation(const basl_source_location_t &location) {
    EXPECT_EQ(location.source_id, 0U);
    EXPECT_EQ(location.offset, 0U);
    EXPECT_EQ(location.line, 0U);
    EXPECT_EQ(location.column, 0U);
}

}  // namespace

TEST(BaslRuntimeTest, RuntimeOpensAndClosesWithDefaultAllocator) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};

    EXPECT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    EXPECT_NE(runtime, nullptr);
    EXPECT_EQ(error.type, BASL_STATUS_OK);
    EXPECT_EQ(error.value, nullptr);
    EXPECT_EQ(error.length, static_cast<size_t>(0));
    ExpectClearedLocation(error.location);
    EXPECT_NE(basl_runtime_allocator(runtime), nullptr);

    basl_runtime_close(&runtime);
    EXPECT_EQ(runtime, nullptr);
}

TEST(BaslRuntimeTest, RuntimeOpenValidatesArguments) {
    basl_error_t error = {};

    EXPECT_EQ(basl_runtime_open(nullptr, nullptr, &error), BASL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "out_runtime must not be null"), 0);
    EXPECT_EQ(error.length, std::strlen(error.value));
    ExpectClearedLocation(error.location);
}

TEST(BaslRuntimeTest, RuntimeUsesCustomAllocatorHooks) {
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
    EXPECT_EQ(stats.reallocate_calls, 0);
    EXPECT_EQ(stats.deallocate_calls, 0);
    ASSERT_NE(basl_runtime_allocator(runtime), nullptr);
    EXPECT_EQ(basl_runtime_allocator(runtime)->user_data, &stats);

    basl_runtime_close(&runtime);
    EXPECT_EQ(runtime, nullptr);
    EXPECT_EQ(stats.deallocate_calls, 1);
}

TEST(BaslRuntimeTest, RuntimeOptionsInitClearsFields) {
    basl_runtime_options_t options = {};
    basl_allocator_t allocator = {};
    basl_logger_t logger = {};

    options.allocator = &allocator;
    options.logger = &logger;
    basl_runtime_options_init(&options);

    EXPECT_EQ(options.allocator, nullptr);
    EXPECT_EQ(options.logger, nullptr);
}

TEST(BaslRuntimeTest, RuntimeRejectsIncompleteAllocator) {
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

TEST(BaslRuntimeTest, RuntimeAllocAndFreeClearPointer) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    void *memory = nullptr;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_runtime_alloc(runtime, 32U, &memory, &error), BASL_STATUS_OK);
    ASSERT_NE(memory, nullptr);
    EXPECT_EQ(std::memcmp(memory, "\0\0\0\0", 4), 0);

    basl_runtime_free(runtime, &memory);
    EXPECT_EQ(memory, nullptr);

    basl_runtime_free(runtime, &memory);
    EXPECT_EQ(memory, nullptr);

    basl_runtime_close(&runtime);
}

TEST(BaslRuntimeTest, RuntimeAllocReportsOutOfMemory) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    FailingAllocatorState state = {};
    basl_allocator_t allocator = {};
    basl_runtime_options_t options = {};
    void *memory = nullptr;

    state.fail_after = 1;
    allocator.user_data = &state;
    allocator.allocate = FailAllocate;
    allocator.deallocate = FailDeallocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    EXPECT_EQ(
        basl_runtime_alloc(runtime, 64U, &memory, &error),
        BASL_STATUS_OUT_OF_MEMORY
    );
    EXPECT_EQ(memory, nullptr);
    EXPECT_EQ(error.type, BASL_STATUS_OUT_OF_MEMORY);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "allocation failed"), 0);

    basl_runtime_close(&runtime);
    EXPECT_EQ(state.deallocate_calls, 1);
}

TEST(BaslRuntimeTest, RuntimeReallocUsesAllocatorWhenAvailable) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    AllocatorStats stats = {};
    basl_allocator_t allocator = {};
    basl_runtime_options_t options = {};
    void *memory = nullptr;

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_runtime_alloc(runtime, 16U, &memory, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_runtime_realloc(runtime, &memory, 64U, &error), BASL_STATUS_OK);
    EXPECT_EQ(stats.reallocate_calls, 1);
    ASSERT_NE(memory, nullptr);

    basl_runtime_free(runtime, &memory);
    basl_runtime_close(&runtime);
}

TEST(BaslRuntimeTest, RuntimeReallocRejectsUnsupportedAllocator) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    AllocatorStats stats = {};
    basl_allocator_t allocator = {};
    basl_runtime_options_t options = {};
    void *memory = nullptr;

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.deallocate = CountedDeallocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_runtime_alloc(runtime, 16U, &memory, &error), BASL_STATUS_OK);
    EXPECT_EQ(
        basl_runtime_realloc(runtime, &memory, 32U, &error),
        BASL_STATUS_UNSUPPORTED
    );
    EXPECT_EQ(error.type, BASL_STATUS_UNSUPPORTED);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(
        std::strcmp(error.value, "allocator does not support reallocate"),
        0
    );

    basl_runtime_free(runtime, &memory);
    basl_runtime_close(&runtime);
}
