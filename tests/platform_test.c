#include "basl_test.h"
#include <string.h>


#include "platform/platform.h"

/* ── Stub tests (always run — verify the stub API contract) ──────── */

/* Link the real platform on native, so we test actual I/O there.
 * For stub coverage, we call the stub functions directly via a
 * separate compilation unit.  Since we can't link both, we test
 * the stub contract by verifying the real platform returns OK
 * (not UNSUPPORTED) on native, proving the dispatch works. */

typedef struct PlatformTest {
    basl_error_t error;
} PlatformTest;

static void PlatformTest_SetUp(void *p) { memset(p, 0, sizeof(PlatformTest)); }
static void PlatformTest_TearDown(void *p) { basl_error_clear(&((PlatformTest *)p)->error); }

/* ── File read/write round-trip ──────────────────────────────────── */

TEST_F(PlatformTest, WriteAndReadFile) {
    const char *path = "test_platform_rw.tmp";
    const char *content = "hello platform";
    char *data = NULL;
    size_t len = 0;

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, content, strlen(content), &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_read_file(NULL, path, &data, &len, &FIXTURE(PlatformTest)->error));

    ASSERT_NE(data, NULL);
    EXPECT_EQ(len, strlen(content));
    EXPECT_STREQ(data, content);
    free(data);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_remove(path, &FIXTURE(PlatformTest)->error));
}

TEST_F(PlatformTest, ReadNonexistent) {
    char *data = NULL;
    size_t len = 0;
    EXPECT_NE(BASL_STATUS_OK,
              basl_platform_read_file(NULL, "nonexistent_file_xyz.tmp",
                                      &data, &len, &FIXTURE(PlatformTest)->error));
}

/* ── File exists ─────────────────────────────────────────────────── */

TEST_F(PlatformTest, FileExists) {
    const char *path = "test_platform_exists.tmp";
    int exists = 0;

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 0);

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, "x", 1, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    basl_platform_remove(path, &FIXTURE(PlatformTest)->error);
}

/* ── Directory operations ────────────────────────────────────────── */

TEST_F(PlatformTest, MkdirAndIsDirectory) {
    const char *path = "test_platform_dir.tmp";
    int is_dir = 0;

    /* Clean up from any previous failed run. */
    basl_platform_remove(path, &FIXTURE(PlatformTest)->error);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_remove(path, &FIXTURE(PlatformTest)->error));
}

TEST_F(PlatformTest, MkdirP) {
    const char *path = "test_platform_deep.tmp/a/b/c";
    int is_dir = 0;

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir_p(path, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    /* Clean up (deepest first). */
    basl_platform_remove("test_platform_deep.tmp/a/b/c", &FIXTURE(PlatformTest)->error);
    basl_platform_remove("test_platform_deep.tmp/a/b", &FIXTURE(PlatformTest)->error);
    basl_platform_remove("test_platform_deep.tmp/a", &FIXTURE(PlatformTest)->error);
    basl_platform_remove("test_platform_deep.tmp", &FIXTURE(PlatformTest)->error);
}

TEST_F(PlatformTest, IsDirectoryOnFile) {
    const char *path = "test_platform_notdir.tmp";
    int is_dir = 1;

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, "x", 1, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 0);

    basl_platform_remove(path, &FIXTURE(PlatformTest)->error);
}

/* ── Custom allocator ────────────────────────────────────────────── */

static size_t test_alloc_count = 0;
static void *counting_alloc(void *ud, size_t s) { (void)ud; test_alloc_count++; return malloc(s); }
static void *counting_realloc(void *ud, void *p, size_t s) { (void)ud; return realloc(p, s); }
static void counting_dealloc(void *ud, void *p) { (void)ud; free(p); }

TEST_F(PlatformTest, ReadFileCustomAllocator) {
    const char *path = "test_platform_alloc.tmp";
    char *data = NULL;
    size_t len = 0;
    basl_allocator_t alloc = {NULL, counting_alloc, counting_realloc, counting_dealloc};

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, "test", 4, &FIXTURE(PlatformTest)->error));

    test_alloc_count = 0;
    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_read_file(&alloc, path, &data, &len, &FIXTURE(PlatformTest)->error));
    EXPECT_GT(test_alloc_count, 0u);
    EXPECT_STREQ(data, "test");
    free(data);

    basl_platform_remove(path, &FIXTURE(PlatformTest)->error);
}

/* ── NULL argument handling ──────────────────────────────────────── */

TEST_F(PlatformTest, NullArgs) {
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_read_file(NULL, NULL, NULL, NULL, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_write_file(NULL, NULL, 0, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_file_exists(NULL, NULL));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_is_directory(NULL, NULL));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_mkdir(NULL, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_mkdir_p(NULL, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_remove(NULL, &FIXTURE(PlatformTest)->error));
}

/* ── Write empty file ────────────────────────────────────────────── */

TEST_F(PlatformTest, WriteEmptyFile) {
    const char *path = "test_platform_empty.tmp";
    char *data = NULL;
    size_t len = 99;

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, NULL, 0, &FIXTURE(PlatformTest)->error));

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_read_file(NULL, path, &data, &len, &FIXTURE(PlatformTest)->error));
    EXPECT_EQ(len, 0u);
    free(data);

    basl_platform_remove(path, &FIXTURE(PlatformTest)->error);
}

/* ── Stub contract tests (always compiled) ───────────────────────── */

/* These verify the stub returns UNSUPPORTED.  On native builds we
 * link the real platform, so we test that it returns OK instead.
 * This ensures the CMake dispatch is correct for each platform. */

TEST_F(PlatformTest, FileExistsReturnsValidStatus) {
    int exists = 0;
    basl_status_t s = basl_platform_file_exists(".", &exists);
    EXPECT_EQ(s, BASL_STATUS_OK);
    EXPECT_EQ(exists, 1);  /* "." always exists */
}
