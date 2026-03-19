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

TEST(VigilSourceTest, InitStartsEmpty)
{
    vigil_source_registry_t registry;

    vigil_source_registry_init(&registry, NULL);

    EXPECT_EQ(registry.runtime, NULL);
    EXPECT_EQ(registry.files, NULL);
    EXPECT_EQ(registry.count, 0U);
    EXPECT_EQ(registry.capacity, 0U);
}

TEST(VigilSourceTest, RegisterCopiesOwnedPathAndText)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_source_id_t source_id = 0U;
    char path[] = "main.vigil";
    char text[] = "print(\"hi\")";
    const vigil_source_file_t *file;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);

    ASSERT_EQ(vigil_source_registry_register(&registry, path, strlen(path), text, strlen(text), &source_id, &error),
              VIGIL_STATUS_OK);
    ASSERT_EQ(source_id, 1U);

    path[0] = 'x';
    text[0] = 'X';
    file = vigil_source_registry_get(&registry, source_id);
    ASSERT_NE(file, NULL);
    EXPECT_STREQ(vigil_string_c_str(&file->path), "main.vigil");
    EXPECT_STREQ(vigil_string_c_str(&file->text), "print(\"hi\")");

    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilSourceTest, MultipleRegistrationsUseStableSourceIds)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_source_id_t first = 0U;
    vigil_source_id_t second = 0U;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);

    ASSERT_EQ(vigil_source_registry_register_cstr(&registry, "a.vigil", "a", &first, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_source_registry_register_cstr(&registry, "b.vigil", "b", &second, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(first, 1U);
    EXPECT_EQ(second, 2U);
    EXPECT_EQ(vigil_source_registry_count(&registry), 2U);
    ASSERT_NE(vigil_source_registry_get(&registry, second), NULL);
    EXPECT_STREQ(vigil_string_c_str(&vigil_source_registry_get(&registry, second)->path), "b.vigil");

    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

TEST(VigilSourceTest, RejectsMissingRuntime)
{
    vigil_source_registry_t registry;
    vigil_source_id_t source_id = 0U;
    vigil_error_t error = {0};

    vigil_source_registry_init(&registry, NULL);

    EXPECT_EQ(vigil_source_registry_register_cstr(&registry, "main.vigil", "", &source_id, &error),
              VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "source registry runtime must not be null"), 0);
}

TEST(VigilSourceTest, UsesRuntimeAllocatorHooks)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    struct AllocatorStats stats = {0};
    vigil_allocator_t allocator = {0};
    vigil_runtime_options_t options = {0};
    vigil_source_id_t source_id = 0U;

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    vigil_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);

    ASSERT_EQ(vigil_source_registry_register_cstr(&registry, "main.vigil", "let value = 1", &source_id, &error),
              VIGIL_STATUS_OK);

    EXPECT_GE(stats.allocate_calls, 3);
    EXPECT_EQ(source_id, 1U);

    vigil_source_registry_free(&registry);
    EXPECT_GE(stats.deallocate_calls, 3);
    vigil_runtime_close(&runtime);
}

TEST(VigilSourceTest, FreeResetsWholeRegistry)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_source_id_t source_id = 0U;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    ASSERT_EQ(vigil_source_registry_register_cstr(&registry, "main.vigil", "", &source_id, &error), VIGIL_STATUS_OK);

    vigil_source_registry_free(&registry);

    EXPECT_EQ(registry.runtime, NULL);
    EXPECT_EQ(registry.files, NULL);
    EXPECT_EQ(registry.count, 0U);
    EXPECT_EQ(registry.capacity, 0U);
    vigil_runtime_close(&runtime);
}

TEST(VigilSourceTest, ResolvesOffsetToLineAndColumn)
{
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};
    vigil_source_registry_t registry;
    vigil_source_id_t source_id = 0U;
    vigil_source_location_t location = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_source_registry_init(&registry, runtime);
    ASSERT_EQ(vigil_source_registry_register_cstr(&registry, "main.vigil", "alpha\nbeta\ngamma", &source_id, &error),
              VIGIL_STATUS_OK);

    location.source_id = source_id;
    location.offset = 7U;
    ASSERT_EQ(vigil_source_registry_resolve_location(&registry, &location, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(location.source_id, source_id);
    EXPECT_EQ(location.offset, 7U);
    EXPECT_EQ(location.line, 2U);
    EXPECT_EQ(location.column, 2U);

    ASSERT_EQ(vigil_source_registry_resolve_span_start(&registry, (vigil_source_span_t){source_id, 11U, 16U}, &location,
                                                       &error),
              VIGIL_STATUS_OK);
    EXPECT_EQ(location.offset, 11U);
    EXPECT_EQ(location.line, 3U);
    EXPECT_EQ(location.column, 1U);

    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
}

void register_source_tests(void)
{
    REGISTER_TEST(VigilSourceTest, InitStartsEmpty);
    REGISTER_TEST(VigilSourceTest, RegisterCopiesOwnedPathAndText);
    REGISTER_TEST(VigilSourceTest, MultipleRegistrationsUseStableSourceIds);
    REGISTER_TEST(VigilSourceTest, RejectsMissingRuntime);
    REGISTER_TEST(VigilSourceTest, UsesRuntimeAllocatorHooks);
    REGISTER_TEST(VigilSourceTest, FreeResetsWholeRegistry);
    REGISTER_TEST(VigilSourceTest, ResolvesOffsetToLineAndColumn);
}
