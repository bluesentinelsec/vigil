#include "basl_test.h"

#include <stdlib.h>
#include <string.h>


#include "basl/basl.h"

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


TEST(BaslSymbolTest, InitStartsEmpty) {
    basl_symbol_table_t table;

    basl_symbol_table_init(&table, NULL);

    EXPECT_EQ(table.runtime, NULL);
    EXPECT_EQ(table.count, 0U);
    EXPECT_EQ(table.capacity, 0U);
    EXPECT_EQ(table.strings, NULL);
    EXPECT_EQ(basl_symbol_table_count(&table), 0U);
    EXPECT_FALSE(basl_symbol_table_is_valid(&table, BASL_SYMBOL_INVALID));
    EXPECT_EQ(basl_symbol_table_c_str(&table, BASL_SYMBOL_INVALID), NULL);
    EXPECT_EQ(basl_symbol_table_length(&table, BASL_SYMBOL_INVALID), 0U);
}

TEST(BaslSymbolTest, InternReturnsStableSymbolForSameText) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_symbol_table_t table;
    basl_symbol_t first;
    basl_symbol_t second;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_symbol_table_init(&table, runtime);

    ASSERT_EQ(
        basl_symbol_table_intern_cstr(&table, "alpha", &first, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_symbol_table_intern_cstr(&table, "alpha", &second, &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(first, second);
    EXPECT_EQ(first, 1U);
    EXPECT_EQ(basl_symbol_table_count(&table), 1U);
    EXPECT_TRUE(basl_symbol_table_is_valid(&table, first));
    ASSERT_NE(basl_symbol_table_c_str(&table, first), NULL);
    EXPECT_STREQ(basl_symbol_table_c_str(&table, first), "alpha");
    EXPECT_EQ(basl_symbol_table_length(&table, first), 5U);

    basl_symbol_table_free(&table);
    basl_runtime_close(&runtime);
}

TEST(BaslSymbolTest, DistinctSymbolsGetDistinctIdsAndReverseLookup) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_symbol_table_t table;
    basl_symbol_t alpha;
    basl_symbol_t beta;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_symbol_table_init(&table, runtime);

    ASSERT_EQ(
        basl_symbol_table_intern(&table, "alpha", 5U, &alpha, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_symbol_table_intern(&table, "beta", 4U, &beta, &error),
        BASL_STATUS_OK
    );

    EXPECT_NE(alpha, beta);
    EXPECT_EQ(alpha, 1U);
    EXPECT_EQ(beta, 2U);
    EXPECT_STREQ(basl_symbol_table_c_str(&table, alpha), "alpha");
    EXPECT_STREQ(basl_symbol_table_c_str(&table, beta), "beta");
    EXPECT_EQ(basl_symbol_table_length(&table, alpha), 5U);
    EXPECT_EQ(basl_symbol_table_length(&table, beta), 4U);
    EXPECT_EQ(basl_symbol_table_c_str(&table, 3U), NULL);

    basl_symbol_table_free(&table);
    basl_runtime_close(&runtime);
}

TEST(BaslSymbolTest, ClearKeepsTableReusable) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_symbol_table_t table;
    basl_symbol_t symbol;
    size_t capacity;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_symbol_table_init(&table, runtime);

    ASSERT_EQ(
        basl_symbol_table_intern_cstr(&table, "alpha", &symbol, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_symbol_table_intern_cstr(&table, "beta", &symbol, &error),
        BASL_STATUS_OK
    );
    capacity = table.capacity;

    basl_symbol_table_clear(&table);
    EXPECT_EQ(basl_symbol_table_count(&table), 0U);
    EXPECT_EQ(table.capacity, capacity);
    EXPECT_EQ(basl_symbol_table_c_str(&table, 1U), NULL);

    ASSERT_EQ(
        basl_symbol_table_intern_cstr(&table, "gamma", &symbol, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(symbol, 1U);
    EXPECT_STREQ(basl_symbol_table_c_str(&table, symbol), "gamma");

    basl_symbol_table_free(&table);
    basl_runtime_close(&runtime);
}

TEST(BaslSymbolTest, GrowthPreservesInternedNames) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_symbol_table_t table;
    char name[32];
    size_t index;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_symbol_table_init(&table, runtime);

    for (index = 0U; index < 128U; index += 1U) {
        basl_symbol_t symbol;

        snprintf(name, sizeof(name), "name-%zu", index);
        ASSERT_EQ(
            basl_symbol_table_intern_cstr(&table, name, &symbol, &error),
            BASL_STATUS_OK
        );
        EXPECT_EQ(symbol, index + 1U);
    }

    for (index = 0U; index < 128U; index += 1U) {
        basl_symbol_t symbol;

        snprintf(name, sizeof(name), "name-%zu", index);
        symbol = (basl_symbol_t)(index + 1U);
        ASSERT_NE(basl_symbol_table_c_str(&table, symbol), NULL);
        EXPECT_STREQ(basl_symbol_table_c_str(&table, symbol), name);
    }

    basl_symbol_table_free(&table);
    basl_runtime_close(&runtime);
}

TEST(BaslSymbolTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_symbol_table_t table;
    basl_symbol_t symbol;
    struct AllocatorStats stats = {0};
    basl_allocator_t allocator = {0};
    basl_runtime_options_t options = {0};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    basl_symbol_table_init(&table, runtime);

    ASSERT_EQ(
        basl_symbol_table_intern_cstr(&table, "alpha", &symbol, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_symbol_table_intern_cstr(&table, "beta", &symbol, &error),
        BASL_STATUS_OK
    );

    EXPECT_GE(stats.allocate_calls, 4);

    basl_symbol_table_free(&table);
    EXPECT_GE(stats.deallocate_calls, 3);
    basl_runtime_close(&runtime);
}

TEST(BaslSymbolTest, RejectsMissingRuntimeAndInvalidArguments) {
    basl_symbol_table_t table;
    basl_error_t error = {0};
    basl_symbol_t symbol;

    basl_symbol_table_init(&table, NULL);

    EXPECT_EQ(
        basl_symbol_table_intern_cstr(&table, "alpha", &symbol, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "symbol table runtime must not be null"), 0);

    EXPECT_EQ(
        basl_symbol_table_intern(NULL, "alpha", 5U, &symbol, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "symbol table must not be null"), 0);
}
