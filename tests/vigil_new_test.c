#include "vigil_test.h"
#include <string.h>

#include "platform/platform.h"
#include "vigil/toml.h"

/* ── Fixture ─────────────────────────────────────────────────────── */

typedef struct VigilNewTest {
    vigil_error_t error;
} VigilNewTest;

static void VigilNewTest_SetUp(void *p) { memset(p, 0, sizeof(VigilNewTest)); }
static void VigilNewTest_TearDown(void *p) { vigil_error_clear(&((VigilNewTest *)p)->error); }

static void remove_project(const char *name, vigil_error_t *error) {
    char path[4096];
    const char *files[] = { "vigil.toml", "main.vigil", ".gitignore", NULL };
    for (int i = 0; files[i]; i++) {
        vigil_platform_path_join(name, files[i], path, sizeof(path), error);
        vigil_platform_remove(path, error);
    }
    snprintf(path, sizeof(path), "%s/lib", name);
    vigil_platform_remove(path, error);
    snprintf(path, sizeof(path), "%s/test", name);
    vigil_platform_remove(path, error);
    vigil_platform_remove(name, error);
}

static void remove_lib_project(const char *name, vigil_error_t *error) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/lib/%s.vigil", name, name);
    vigil_platform_remove(path, error);
    snprintf(path, sizeof(path), "%s/test/%s_test.vigil", name, name);
    vigil_platform_remove(path, error);
    const char *files[] = { "vigil.toml", ".gitignore", NULL };
    for (int i = 0; files[i]; i++) {
        vigil_platform_path_join(name, files[i], path, sizeof(path), error);
        vigil_platform_remove(path, error);
    }
    snprintf(path, sizeof(path), "%s/lib", name);
    vigil_platform_remove(path, error);
    snprintf(path, sizeof(path), "%s/test", name);
    vigil_platform_remove(path, error);
    vigil_platform_remove(name, error);
}

#define ERR (&FIXTURE(VigilNewTest)->error)

/* ── Platform path_join ──────────────────────────────────────────── */

TEST_F(VigilNewTest, PathJoinBasic) {
    char buf[256];
    ASSERT_EQ(VIGIL_STATUS_OK,
              vigil_platform_path_join("foo", "bar", buf, sizeof(buf), ERR));
    EXPECT_STREQ(buf, "foo/bar");
}

TEST_F(VigilNewTest, PathJoinTrailingSlash) {
    char buf[256];
    ASSERT_EQ(VIGIL_STATUS_OK,
              vigil_platform_path_join("foo/", "bar", buf, sizeof(buf), ERR));
    EXPECT_STREQ(buf, "foo/bar");
}

TEST_F(VigilNewTest, PathJoinOverflow) {
    char buf[5];
    EXPECT_NE(VIGIL_STATUS_OK,
              vigil_platform_path_join("long", "path", buf, sizeof(buf), ERR));
}

TEST_F(VigilNewTest, PathJoinNull) {
    char buf[256];
    EXPECT_EQ(VIGIL_STATUS_INVALID_ARGUMENT,
              vigil_platform_path_join(NULL, "b", buf, sizeof(buf), ERR));
}

/* ── App project scaffolding ─────────────────────────────────────── */

TEST_F(VigilNewTest, AppProjectStructure) {
    const char *name = "test_new_app_proj";
    char path[4096];
    int exists = 0;
    int is_dir = 0;

    remove_project(name, ERR);

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(name, ERR));

    vigil_platform_path_join(name, "lib", path, sizeof(path), ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir(path, ERR));

    vigil_platform_path_join(name, "test", path, sizeof(path), ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir(path, ERR));

    {
        vigil_toml_value_t *root = NULL;
        vigil_toml_value_t *val = NULL;
        char *toml_str = NULL;
        size_t toml_len = 0;

        ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_table_new(NULL, &root, ERR));
        ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_string_new(NULL, name, strlen(name), &val, ERR));
        ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_table_set(root, "name", 4, val, ERR));
        val = NULL;
        ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_string_new(NULL, "0.1.0", 5, &val, ERR));
        ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_table_set(root, "version", 7, val, ERR));
        val = NULL;
        ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_emit(root, &toml_str, &toml_len, ERR));

        vigil_platform_path_join(name, "vigil.toml", path, sizeof(path), ERR);
        ASSERT_EQ(VIGIL_STATUS_OK,
                  vigil_platform_write_file(path, toml_str, toml_len, ERR));
        free(toml_str);
        vigil_toml_free(&root);
    }

    {
        const char *content = "import \"fmt\";\n\nfn main() -> i32 {\n    fmt.println(\"hello\");\n    return 0;\n}\n";
        vigil_platform_path_join(name, "main.vigil", path, sizeof(path), ERR);
        ASSERT_EQ(VIGIL_STATUS_OK,
                  vigil_platform_write_file(path, content, strlen(content), ERR));
    }

    {
        vigil_platform_path_join(name, ".gitignore", path, sizeof(path), ERR);
        ASSERT_EQ(VIGIL_STATUS_OK,
                  vigil_platform_write_file(path, "deps/\n", 6, ERR));
    }

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_is_directory(name, &is_dir));
    EXPECT_EQ(is_dir, 1);

    vigil_platform_path_join(name, "lib", path, sizeof(path), ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    vigil_platform_path_join(name, "test", path, sizeof(path), ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    vigil_platform_path_join(name, "vigil.toml", path, sizeof(path), ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);
    {
        char *data = NULL;
        size_t len = 0;
        vigil_toml_value_t *parsed = NULL;
        ASSERT_EQ(VIGIL_STATUS_OK,
                  vigil_platform_read_file(NULL, path, &data, &len, ERR));
        ASSERT_EQ(VIGIL_STATUS_OK,
                  vigil_toml_parse(NULL, data, len, &parsed, ERR));
        EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(parsed, "name")), name);
        EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(parsed, "version")), "0.1.0");
        vigil_toml_free(&parsed);
        free(data);
    }

    vigil_platform_path_join(name, "main.vigil", path, sizeof(path), ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    vigil_platform_path_join(name, ".gitignore", path, sizeof(path), ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    remove_project(name, ERR);
}

/* ── Library project scaffolding ─────────────────────────────────── */

TEST_F(VigilNewTest, LibProjectStructure) {
    const char *name = "test_new_lib_proj";
    char path[4096];
    int exists = 0;

    remove_lib_project(name, ERR);

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(name, ERR));

    vigil_platform_path_join(name, "lib", path, sizeof(path), ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir(path, ERR));

    vigil_platform_path_join(name, "test", path, sizeof(path), ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir(path, ERR));

    {
        char content[512];
        snprintf(content, sizeof(content),
            "/// %s library module.\n\npub fn hello() -> string {\n"
            "    return \"hello from %s\";\n}\n", name, name);
        snprintf(path, sizeof(path), "%s/lib/%s.vigil", name, name);
        ASSERT_EQ(VIGIL_STATUS_OK,
                  vigil_platform_write_file(path, content, strlen(content), ERR));
    }

    {
        char content[512];
        snprintf(content, sizeof(content),
            "import \"test\";\nimport \"%s\";\n\n"
            "fn test_hello(test.T t) -> void {\n"
            "    t.assert(%s.hello() == \"hello from %s\", \"hello should match\");\n}\n",
            name, name, name);
        snprintf(path, sizeof(path), "%s/test/%s_test.vigil", name, name);
        ASSERT_EQ(VIGIL_STATUS_OK,
                  vigil_platform_write_file(path, content, strlen(content), ERR));
    }

    snprintf(path, sizeof(path), "%s/lib/%s.vigil", name, name);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);
    {
        char *data = NULL;
        size_t len = 0;
        ASSERT_EQ(VIGIL_STATUS_OK,
                  vigil_platform_read_file(NULL, path, &data, &len, ERR));
        EXPECT_TRUE(strstr(data, "pub fn hello") != NULL);
        free(data);
    }

    snprintf(path, sizeof(path), "%s/test/%s_test.vigil", name, name);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    remove_lib_project(name, ERR);
}

/* ── Readline ────────────────────────────────────────────────────── */

TEST_F(VigilNewTest, ReadlineNullArgs) {
    EXPECT_EQ(VIGIL_STATUS_INVALID_ARGUMENT,
              vigil_platform_readline(NULL, NULL, 0, ERR));
}

#undef ERR

void register_vigil_new_tests(void) {
    REGISTER_TEST_F(VigilNewTest, PathJoinBasic);
    REGISTER_TEST_F(VigilNewTest, PathJoinTrailingSlash);
    REGISTER_TEST_F(VigilNewTest, PathJoinOverflow);
    REGISTER_TEST_F(VigilNewTest, PathJoinNull);
    REGISTER_TEST_F(VigilNewTest, AppProjectStructure);
    REGISTER_TEST_F(VigilNewTest, LibProjectStructure);
    REGISTER_TEST_F(VigilNewTest, ReadlineNullArgs);
}
