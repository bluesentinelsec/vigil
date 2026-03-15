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


TEST(BaslSourceTest, InitStartsEmpty) {
    basl_source_registry_t registry;

    basl_source_registry_init(&registry, NULL);

    EXPECT_EQ(registry.runtime, NULL);
    EXPECT_EQ(registry.files, NULL);
    EXPECT_EQ(registry.count, 0U);
    EXPECT_EQ(registry.capacity, 0U);
}

TEST(BaslSourceTest, RegisterCopiesOwnedPathAndText) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;
    char path[] = "main.basl";
    char text[] = "print(\"hi\")";
    const basl_source_file_t *file;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);

    ASSERT_EQ(
        basl_source_registry_register(
            &registry,
            path,
            strlen(path),
            text,
            strlen(text),
            &source_id,
            &error
        ),
        BASL_STATUS_OK
    );
    ASSERT_EQ(source_id, 1U);

    path[0] = 'x';
    text[0] = 'X';
    file = basl_source_registry_get(&registry, source_id);
    ASSERT_NE(file, NULL);
    EXPECT_STREQ(basl_string_c_str(&file->path), "main.basl");
    EXPECT_STREQ(basl_string_c_str(&file->text), "print(\"hi\")");

    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslSourceTest, MultipleRegistrationsUseStableSourceIds) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_source_id_t first = 0U;
    basl_source_id_t second = 0U;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);

    ASSERT_EQ(
        basl_source_registry_register_cstr(&registry, "a.basl", "a", &first, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_source_registry_register_cstr(&registry, "b.basl", "b", &second, &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(first, 1U);
    EXPECT_EQ(second, 2U);
    EXPECT_EQ(basl_source_registry_count(&registry), 2U);
    ASSERT_NE(basl_source_registry_get(&registry, second), NULL);
    EXPECT_STREQ(
        basl_string_c_str(&basl_source_registry_get(&registry, second)->path),
        "b.basl"
    );

    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslSourceTest, RejectsMissingRuntime) {
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;
    basl_error_t error = {0};

    basl_source_registry_init(&registry, NULL);

    EXPECT_EQ(
        basl_source_registry_register_cstr(&registry, "main.basl", "", &source_id, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "source registry runtime must not be null"), 0);
}

TEST(BaslSourceTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    struct AllocatorStats stats = {0};
    basl_allocator_t allocator = {0};
    basl_runtime_options_t options = {0};
    basl_source_id_t source_id = 0U;

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);

    ASSERT_EQ(
        basl_source_registry_register_cstr(
            &registry,
            "main.basl",
            "let value = 1",
            &source_id,
            &error
        ),
        BASL_STATUS_OK
    );

    EXPECT_GE(stats.allocate_calls, 3);
    EXPECT_EQ(source_id, 1U);

    basl_source_registry_free(&registry);
    EXPECT_GE(stats.deallocate_calls, 3);
    basl_runtime_close(&runtime);
}

TEST(BaslSourceTest, FreeResetsWholeRegistry) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    ASSERT_EQ(
        basl_source_registry_register_cstr(&registry, "main.basl", "", &source_id, &error),
        BASL_STATUS_OK
    );

    basl_source_registry_free(&registry);

    EXPECT_EQ(registry.runtime, NULL);
    EXPECT_EQ(registry.files, NULL);
    EXPECT_EQ(registry.count, 0U);
    EXPECT_EQ(registry.capacity, 0U);
    basl_runtime_close(&runtime);
}

TEST(BaslSourceTest, ResolvesOffsetToLineAndColumn) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;
    basl_source_location_t location = {0};

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    ASSERT_EQ(
        basl_source_registry_register_cstr(
            &registry,
            "main.basl",
            "alpha\nbeta\ngamma",
            &source_id,
            &error
        ),
        BASL_STATUS_OK
    );

    location.source_id = source_id;
    location.offset = 7U;
    ASSERT_EQ(
        basl_source_registry_resolve_location(&registry, &location, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(location.source_id, source_id);
    EXPECT_EQ(location.offset, 7U);
    EXPECT_EQ(location.line, 2U);
    EXPECT_EQ(location.column, 2U);

    ASSERT_EQ(
        basl_source_registry_resolve_span_start(
            &registry,
            (basl_source_span_t){source_id, 11U, 16U},
            &location,
            &error
        ),
        BASL_STATUS_OK
    );
    EXPECT_EQ(location.offset, 11U);
    EXPECT_EQ(location.line, 3U);
    EXPECT_EQ(location.column, 1U);

    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}
