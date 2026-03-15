#include "basl_test.h"
#include <string.h>

#include "platform/platform.h"
#include "basl/toml.h"

/* ── Fixture ─────────────────────────────────────────────────────── */

typedef struct BaslNewTest {
    basl_error_t error;
} BaslNewTest;

static void BaslNewTest_SetUp(void *p) { memset(p, 0, sizeof(BaslNewTest)); }
static void BaslNewTest_TearDown(void *p) { basl_error_clear(&((BaslNewTest *)p)->error); }

static void remove_project(const char *name, basl_error_t *error) {
    char path[4096];
    const char *files[] = { "basl.toml", "main.basl", ".gitignore", NULL };
    for (int i = 0; files[i]; i++) {
        basl_platform_path_join(name, files[i], path, sizeof(path), error);
        basl_platform_remove(path, error);
    }
    snprintf(path, sizeof(path), "%s/lib", name);
    basl_platform_remove(path, error);
    snprintf(path, sizeof(path), "%s/test", name);
    basl_platform_remove(path, error);
    basl_platform_remove(name, error);
}

static void remove_lib_project(const char *name, basl_error_t *error) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/lib/%s.basl", name, name);
    basl_platform_remove(path, error);
    snprintf(path, sizeof(path), "%s/test/%s_test.basl", name, name);
    basl_platform_remove(path, error);
    const char *files[] = { "basl.toml", ".gitignore", NULL };
    for (int i = 0; files[i]; i++) {
        basl_platform_path_join(name, files[i], path, sizeof(path), error);
        basl_platform_remove(path, error);
    }
    snprintf(path, sizeof(path), "%s/lib", name);
    basl_platform_remove(path, error);
    snprintf(path, sizeof(path), "%s/test", name);
    basl_platform_remove(path, error);
    basl_platform_remove(name, error);
}

#define ERR (&FIXTURE(BaslNewTest)->error)

/* ── Platform path_join ──────────────────────────────────────────── */

TEST_F(BaslNewTest, PathJoinBasic) {
    char buf[256];
    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_path_join("foo", "bar", buf, sizeof(buf), ERR));
    EXPECT_STREQ(buf, "foo/bar");
}

TEST_F(BaslNewTest, PathJoinTrailingSlash) {
    char buf[256];
    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_path_join("foo/", "bar", buf, sizeof(buf), ERR));
    EXPECT_STREQ(buf, "foo/bar");
}

TEST_F(BaslNewTest, PathJoinOverflow) {
    char buf[5];
    EXPECT_NE(BASL_STATUS_OK,
              basl_platform_path_join("long", "path", buf, sizeof(buf), ERR));
}

TEST_F(BaslNewTest, PathJoinNull) {
    char buf[256];
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_path_join(NULL, "b", buf, sizeof(buf), ERR));
}

/* ── App project scaffolding ─────────────────────────────────────── */

TEST_F(BaslNewTest, AppProjectStructure) {
    const char *name = "test_new_app_proj";
    char path[4096];
    int exists = 0;
    int is_dir = 0;

    remove_project(name, ERR);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir_p(name, ERR));

    basl_platform_path_join(name, "lib", path, sizeof(path), ERR);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, ERR));

    basl_platform_path_join(name, "test", path, sizeof(path), ERR);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, ERR));

    {
        basl_toml_value_t *root = NULL;
        basl_toml_value_t *val = NULL;
        char *toml_str = NULL;
        size_t toml_len = 0;

        ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_new(NULL, &root, ERR));
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_string_new(NULL, name, strlen(name), &val, ERR));
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_set(root, "name", 4, val, ERR));
        val = NULL;
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_string_new(NULL, "0.1.0", 5, &val, ERR));
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_set(root, "version", 7, val, ERR));
        val = NULL;
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_emit(root, &toml_str, &toml_len, ERR));

        basl_platform_path_join(name, "basl.toml", path, sizeof(path), ERR);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, toml_str, toml_len, ERR));
        free(toml_str);
        basl_toml_free(&root);
    }

    {
        const char *content = "import \"fmt\";\n\nfn main() -> i32 {\n    fmt.println(\"hello\");\n    return 0;\n}\n";
        basl_platform_path_join(name, "main.basl", path, sizeof(path), ERR);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, content, strlen(content), ERR));
    }

    {
        basl_platform_path_join(name, ".gitignore", path, sizeof(path), ERR);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, "deps/\n", 6, ERR));
    }

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(name, &is_dir));
    EXPECT_EQ(is_dir, 1);

    basl_platform_path_join(name, "lib", path, sizeof(path), ERR);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    basl_platform_path_join(name, "test", path, sizeof(path), ERR);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    basl_platform_path_join(name, "basl.toml", path, sizeof(path), ERR);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);
    {
        char *data = NULL;
        size_t len = 0;
        basl_toml_value_t *parsed = NULL;
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_read_file(NULL, path, &data, &len, ERR));
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_toml_parse(NULL, data, len, &parsed, ERR));
        EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(parsed, "name")), name);
        EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(parsed, "version")), "0.1.0");
        basl_toml_free(&parsed);
        free(data);
    }

    basl_platform_path_join(name, "main.basl", path, sizeof(path), ERR);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    basl_platform_path_join(name, ".gitignore", path, sizeof(path), ERR);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    remove_project(name, ERR);
}

/* ── Library project scaffolding ─────────────────────────────────── */

TEST_F(BaslNewTest, LibProjectStructure) {
    const char *name = "test_new_lib_proj";
    char path[4096];
    int exists = 0;

    remove_lib_project(name, ERR);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir_p(name, ERR));

    basl_platform_path_join(name, "lib", path, sizeof(path), ERR);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, ERR));

    basl_platform_path_join(name, "test", path, sizeof(path), ERR);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, ERR));

    {
        char content[512];
        snprintf(content, sizeof(content),
            "/// %s library module.\n\npub fn hello() -> string {\n"
            "    return \"hello from %s\";\n}\n", name, name);
        snprintf(path, sizeof(path), "%s/lib/%s.basl", name, name);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, content, strlen(content), ERR));
    }

    {
        char content[512];
        snprintf(content, sizeof(content),
            "import \"test\";\nimport \"%s\";\n\n"
            "fn test_hello(test.T t) -> void {\n"
            "    t.assert(%s.hello() == \"hello from %s\", \"hello should match\");\n}\n",
            name, name, name);
        snprintf(path, sizeof(path), "%s/test/%s_test.basl", name, name);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, content, strlen(content), ERR));
    }

    snprintf(path, sizeof(path), "%s/lib/%s.basl", name, name);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);
    {
        char *data = NULL;
        size_t len = 0;
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_read_file(NULL, path, &data, &len, ERR));
        EXPECT_TRUE(strstr(data, "pub fn hello") != NULL);
        free(data);
    }

    snprintf(path, sizeof(path), "%s/test/%s_test.basl", name, name);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    remove_lib_project(name, ERR);
}

/* ── Readline ────────────────────────────────────────────────────── */

TEST_F(BaslNewTest, ReadlineNullArgs) {
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_readline(NULL, NULL, 0, ERR));
}

#undef ERR

void register_basl_new_tests(void) {
    REGISTER_TEST_F(BaslNewTest, PathJoinBasic);
    REGISTER_TEST_F(BaslNewTest, PathJoinTrailingSlash);
    REGISTER_TEST_F(BaslNewTest, PathJoinOverflow);
    REGISTER_TEST_F(BaslNewTest, PathJoinNull);
    REGISTER_TEST_F(BaslNewTest, AppProjectStructure);
    REGISTER_TEST_F(BaslNewTest, LibProjectStructure);
    REGISTER_TEST_F(BaslNewTest, ReadlineNullArgs);
}
