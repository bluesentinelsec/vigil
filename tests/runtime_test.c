#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#include "vigil/vigil.h"

struct AllocatorStats
{
    int allocate_calls;
    int reallocate_calls;
    int deallocate_calls;
};

struct FailingAllocatorState
{
    int allocate_calls;
    int deallocate_calls;
    int fail_after;
};

static void *CountedAllocate(void *user_data, size_t size)
{
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->allocate_calls += 1;
    return malloc(size);
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

void *FailAllocate(void *user_data, size_t size)
{
    struct FailingAllocatorState *state = (struct FailingAllocatorState *)(user_data);

    state->allocate_calls += 1;
    if (state->allocate_calls > state->fail_after)
    {
        return NULL;
    }

    return malloc(size);
}

void FailDeallocate(void *user_data, void *memory)
{
    struct FailingAllocatorState *state = (struct FailingAllocatorState *)(user_data);

    state->deallocate_calls += 1;
    free(memory);
}

static void ExpectClearedLocation(int *vigil_test_failed_, const vigil_source_location_t *location)
{
    EXPECT_EQ(location->source_id, 0U);
    EXPECT_EQ(location->offset, 0U);
    EXPECT_EQ(location->line, 0U);
    EXPECT_EQ(location->column, 0U);
}

TEST(VigilRuntimeTest, RuntimeOpensAndClosesWithDefaultAllocator)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};

    EXPECT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_NE(runtime, NULL);
    EXPECT_EQ(error.type, VIGIL_STATUS_OK);
    EXPECT_EQ(error.value, NULL);
    EXPECT_EQ(error.length, (size_t)(0));
    ExpectClearedLocation(vigil_test_failed_, &error.location);
    EXPECT_NE(vigil_runtime_allocator(runtime), NULL);

    vigil_runtime_close(&runtime);
    EXPECT_EQ(runtime, NULL);
}

TEST(VigilRuntimeTest, RuntimeOpenValidatesArguments)
{
    vigil_error_t error = {0};

    EXPECT_EQ(vigil_runtime_open(NULL, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "out_runtime must not be null"), 0);
    EXPECT_EQ(error.length, strlen(error.value));
    ExpectClearedLocation(vigil_test_failed_, &error.location);
}

TEST(VigilRuntimeTest, RuntimeUsesCustomAllocatorHooks)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    struct AllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t options = {0};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;

    vigil_runtime_options_init(&options);
    options.allocator = &allocator;

    EXPECT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_OK);
    ASSERT_NE(runtime, NULL);
    EXPECT_EQ(stats.allocate_calls, 3); /* runtime struct + ok_error object + ok_error string */
    EXPECT_EQ(stats.reallocate_calls, 0);
    EXPECT_EQ(stats.deallocate_calls, 0);
    ASSERT_NE(vigil_runtime_allocator(runtime), NULL);
    EXPECT_EQ(vigil_runtime_allocator(runtime)->user_data, &stats);

    vigil_runtime_close(&runtime);
    EXPECT_EQ(runtime, NULL);
    EXPECT_EQ(stats.deallocate_calls, 3); /* ok_error string + ok_error object + runtime struct */
}

TEST(VigilRuntimeTest, RuntimeOptionsInitClearsFields)
{
    vigil_runtime_options_t options = {0};
    vigil_allocator_t allocator = {0};
    vigil_logger_t logger = {0};

    options.allocator = &allocator;
    options.logger = &logger;
    vigil_runtime_options_init(&options);

    EXPECT_EQ(options.allocator, NULL);
    EXPECT_EQ(options.logger, NULL);
}

TEST(VigilRuntimeTest, RuntimeRejectsIncompleteAllocator)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t options = {0};

    allocator.allocate = CountedAllocate;
    vigil_runtime_options_init(&options);
    options.allocator = &allocator;

    EXPECT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(runtime, NULL);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "allocator must define allocate and deallocate"), 0);
    EXPECT_EQ(error.length, strlen(error.value));
}

TEST(VigilRuntimeTest, RuntimeAllocAndFreeClearPointer)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    void *memory = NULL;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_runtime_alloc(runtime, 32U, &memory, &error), VIGIL_STATUS_OK);
    ASSERT_NE(memory, NULL);
    EXPECT_EQ(memcmp(memory, "\0\0\0\0", 4), 0);

    vigil_runtime_free(runtime, &memory);
    EXPECT_EQ(memory, NULL);

    vigil_runtime_free(runtime, &memory);
    EXPECT_EQ(memory, NULL);

    vigil_runtime_close(&runtime);
}

TEST(VigilRuntimeTest, RuntimeAllocReportsOutOfMemory)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    struct FailingAllocatorState state = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t options = {0};
    void *memory = NULL;

    state.fail_after = 3; /* allow runtime struct + ok_error object + ok_error string */
    allocator.user_data = &state;
    allocator.allocate = FailAllocate;
    allocator.deallocate = FailDeallocate;
    vigil_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_runtime_alloc(runtime, 64U, &memory, &error), VIGIL_STATUS_OUT_OF_MEMORY);
    EXPECT_EQ(memory, NULL);
    EXPECT_EQ(error.type, VIGIL_STATUS_OUT_OF_MEMORY);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "allocation failed"), 0);

    vigil_runtime_close(&runtime);
    EXPECT_EQ(state.deallocate_calls, 3); /* ok_error string + ok_error object + runtime struct */
}

TEST(VigilRuntimeTest, RuntimeReallocUsesAllocatorWhenAvailable)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    struct AllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t options = {0};
    void *memory = NULL;

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    vigil_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_runtime_alloc(runtime, 16U, &memory, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_runtime_realloc(runtime, &memory, 64U, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(stats.reallocate_calls, 1);
    ASSERT_NE(memory, NULL);

    vigil_runtime_free(runtime, &memory);
    vigil_runtime_close(&runtime);
}

TEST(VigilRuntimeTest, RuntimeReallocRejectsUnsupportedAllocator)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    struct AllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t options = {0};
    void *memory = NULL;

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.deallocate = CountedDeallocate;
    vigil_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_runtime_alloc(runtime, 16U, &memory, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_runtime_realloc(runtime, &memory, 32U, &error), VIGIL_STATUS_UNSUPPORTED);
    EXPECT_EQ(error.type, VIGIL_STATUS_UNSUPPORTED);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "allocator does not support reallocate"), 0);

    vigil_runtime_free(runtime, &memory);
    vigil_runtime_close(&runtime);
}

void register_runtime_tests(void)
{
    REGISTER_TEST(VigilRuntimeTest, RuntimeOpensAndClosesWithDefaultAllocator);
    REGISTER_TEST(VigilRuntimeTest, RuntimeOpenValidatesArguments);
    REGISTER_TEST(VigilRuntimeTest, RuntimeUsesCustomAllocatorHooks);
    REGISTER_TEST(VigilRuntimeTest, RuntimeOptionsInitClearsFields);
    REGISTER_TEST(VigilRuntimeTest, RuntimeRejectsIncompleteAllocator);
    REGISTER_TEST(VigilRuntimeTest, RuntimeAllocAndFreeClearPointer);
    REGISTER_TEST(VigilRuntimeTest, RuntimeAllocReportsOutOfMemory);
    REGISTER_TEST(VigilRuntimeTest, RuntimeReallocUsesAllocatorWhenAvailable);
    REGISTER_TEST(VigilRuntimeTest, RuntimeReallocRejectsUnsupportedAllocator);
}
