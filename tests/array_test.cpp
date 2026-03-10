#include <gtest/gtest.h>

#include <cstdint>
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

TEST(BaslArrayTest, InitStartsEmpty) {
    basl_byte_buffer_t buffer;

    basl_byte_buffer_init(&buffer, nullptr);

    EXPECT_EQ(buffer.runtime, nullptr);
    EXPECT_EQ(buffer.data, nullptr);
    EXPECT_EQ(buffer.length, 0U);
    EXPECT_EQ(buffer.capacity, 0U);
}

TEST(BaslArrayTest, ReserveAllocatesAndPreservesLength) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_byte_buffer_t buffer;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_byte_buffer_init(&buffer, runtime);

    ASSERT_EQ(basl_byte_buffer_reserve(&buffer, 32U, &error), BASL_STATUS_OK);
    EXPECT_NE(buffer.data, nullptr);
    EXPECT_EQ(buffer.length, 0U);
    EXPECT_GE(buffer.capacity, 32U);

    basl_byte_buffer_free(&buffer);
    basl_runtime_close(&runtime);
}

TEST(BaslArrayTest, ResizeZeroInitializesNewBytes) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_byte_buffer_t buffer;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_byte_buffer_init(&buffer, runtime);

    ASSERT_EQ(basl_byte_buffer_resize(&buffer, 8U, &error), BASL_STATUS_OK);
    ASSERT_EQ(buffer.length, 8U);
    EXPECT_EQ(std::memcmp(buffer.data, "\0\0\0\0\0\0\0\0", 8), 0);

    buffer.data[0] = 0xAAU;
    buffer.data[1] = 0xBBU;
    ASSERT_EQ(basl_byte_buffer_resize(&buffer, 12U, &error), BASL_STATUS_OK);
    EXPECT_EQ(buffer.data[0], 0xAAU);
    EXPECT_EQ(buffer.data[1], 0xBBU);
    EXPECT_EQ(std::memcmp(buffer.data + 8, "\0\0\0\0", 4), 0);

    basl_byte_buffer_free(&buffer);
    basl_runtime_close(&runtime);
}

TEST(BaslArrayTest, AppendAddsBytesInOrder) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_byte_buffer_t buffer;
    const uint8_t prefix[] = {1U, 2U, 3U};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_byte_buffer_init(&buffer, runtime);

    ASSERT_EQ(
        basl_byte_buffer_append(&buffer, prefix, sizeof(prefix), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(basl_byte_buffer_append_byte(&buffer, 4U, &error), BASL_STATUS_OK);

    ASSERT_EQ(buffer.length, 4U);
    EXPECT_EQ(std::memcmp(buffer.data, "\x01\x02\x03\x04", 4), 0);

    basl_byte_buffer_free(&buffer);
    basl_runtime_close(&runtime);
}

TEST(BaslArrayTest, ClearKeepsCapacityButResetsLength) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_byte_buffer_t buffer;
    size_t capacity;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_byte_buffer_init(&buffer, runtime);
    ASSERT_EQ(basl_byte_buffer_resize(&buffer, 16U, &error), BASL_STATUS_OK);

    capacity = buffer.capacity;
    basl_byte_buffer_clear(&buffer);

    EXPECT_EQ(buffer.length, 0U);
    EXPECT_EQ(buffer.capacity, capacity);
    EXPECT_NE(buffer.data, nullptr);

    basl_byte_buffer_free(&buffer);
    basl_runtime_close(&runtime);
}

TEST(BaslArrayTest, FreeResetsWholeBuffer) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_byte_buffer_t buffer;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_byte_buffer_init(&buffer, runtime);
    ASSERT_EQ(basl_byte_buffer_resize(&buffer, 4U, &error), BASL_STATUS_OK);

    basl_byte_buffer_free(&buffer);

    EXPECT_EQ(buffer.runtime, nullptr);
    EXPECT_EQ(buffer.data, nullptr);
    EXPECT_EQ(buffer.length, 0U);
    EXPECT_EQ(buffer.capacity, 0U);

    basl_runtime_close(&runtime);
}

TEST(BaslArrayTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_byte_buffer_t buffer;
    AllocatorStats stats = {};
    basl_allocator_t allocator = {};
    basl_runtime_options_t options = {};
    const uint8_t data[40] = {0U};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    basl_byte_buffer_init(&buffer, runtime);

    ASSERT_EQ(
        basl_byte_buffer_append(&buffer, data, sizeof(data), &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_byte_buffer_append(&buffer, data, sizeof(data), &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(stats.allocate_calls, 2);
    EXPECT_GE(stats.reallocate_calls, 1);

    basl_byte_buffer_free(&buffer);
    EXPECT_GE(stats.deallocate_calls, 1);
    basl_runtime_close(&runtime);
}

TEST(BaslArrayTest, RejectsMissingRuntime) {
    basl_byte_buffer_t buffer;
    basl_error_t error = {};

    basl_byte_buffer_init(&buffer, nullptr);

    EXPECT_EQ(
        basl_byte_buffer_reserve(&buffer, 8U, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "byte buffer runtime must not be null"), 0);
}

TEST(BaslArrayTest, DetectsAppendOverflow) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_byte_buffer_t buffer;
    uint8_t value = 0U;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_byte_buffer_init(&buffer, runtime);
    buffer.data = reinterpret_cast<uint8_t *>(1);
    buffer.length = SIZE_MAX;
    buffer.capacity = SIZE_MAX;

    EXPECT_EQ(
        basl_byte_buffer_append(&buffer, &value, 1U, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "byte buffer append would overflow"), 0);

    buffer.data = nullptr;
    buffer.length = 0U;
    buffer.capacity = 0U;
    basl_runtime_close(&runtime);
}
