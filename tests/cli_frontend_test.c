#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

#include "internal/vigil_cli_frontend.h"
#include "platform/platform.h"

typedef struct CliFrontendTest
{
    vigil_error_t error;
} CliFrontendTest;

static void CliFrontendTest_SetUp(void *fixture)
{
    memset(fixture, 0, sizeof(CliFrontendTest));
}

static void CliFrontendTest_TearDown(void *fixture)
{
    vigil_error_clear(&((CliFrontendTest *)fixture)->error);
}

static void remove_path_if_present(const char *path, vigil_error_t *error)
{
    vigil_error_clear(error);
    (void)vigil_platform_remove(path, error);
    vigil_error_clear(error);
}

static void remove_cli_frontend_project(const char *root, const char *module_name, vigil_error_t *error)
{
    char path[4096];

    snprintf(path, sizeof(path), "%s/lib/%s.vigil", root, module_name);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/test/main_test.vigil", root);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/main.vigil", root);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/vigil.toml", root);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/lib", root);
    remove_path_if_present(path, error);
    snprintf(path, sizeof(path), "%s/test", root);
    remove_path_if_present(path, error);
    remove_path_if_present(root, error);
}

static vigil_status_t write_text_fixture(const char *path, const char *text, vigil_error_t *error)
{
    return vigil_platform_write_file(path, text, strlen(text), error);
}

static const vigil_source_file_t *find_registered_source(const vigil_source_registry_t *registry, const char *path)
{
    size_t index;

    for (index = 1U; index <= vigil_source_registry_count(registry); index += 1U)
    {
        const vigil_source_file_t *source;

        source = vigil_source_registry_get(registry, (vigil_source_id_t)index);
        if (source != NULL && strcmp(vigil_string_c_str(&source->path), path) == 0)
            return source;
    }
    return NULL;
}

#define ERR (&FIXTURE(CliFrontendTest)->error)

TEST_F(CliFrontendTest, FindProjectRootReturnsProjectDirectory)
{
    const char *root = "cli_frontend_root_proj";
    const char *test_source = "import \"test\";\nfn test_ok(test.T t) -> void { t.assert(true, \"ok\"); }\n";
    char path[4096];
    char out[4096];

    remove_cli_frontend_project(root, "helper", ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(root, ERR));
    snprintf(path, sizeof(path), "%s/test", root);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(path, ERR));
    snprintf(path, sizeof(path), "%s/vigil.toml", root);
    ASSERT_EQ(VIGIL_STATUS_OK, write_text_fixture(path, "[project]\nname = \"root\"\n", ERR));
    snprintf(path, sizeof(path), "%s/test/main_test.vigil", root);
    ASSERT_EQ(VIGIL_STATUS_OK, write_text_fixture(path, test_source, ERR));

    ASSERT_TRUE(find_project_root(path, out, sizeof(out)));
    EXPECT_STREQ(out, root);

    remove_cli_frontend_project(root, "helper", ERR);
}

TEST_F(CliFrontendTest, FindProjectRootReturnsZeroWithoutManifest)
{
    const char *root = "cli_frontend_no_manifest";
    const char *test_source = "import \"test\";\nfn test_ok(test.T t) -> void { t.assert(true, \"ok\"); }\n";
    char path[4096];
    char out[4096];

    remove_cli_frontend_project(root, "helper", ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(root, ERR));
    snprintf(path, sizeof(path), "%s/test", root);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(path, ERR));
    snprintf(path, sizeof(path), "%s/test/main_test.vigil", root);
    ASSERT_EQ(VIGIL_STATUS_OK, write_text_fixture(path, test_source, ERR));

    EXPECT_FALSE(find_project_root(path, out, sizeof(out)));

    remove_cli_frontend_project(root, "helper", ERR);
}

TEST_F(CliFrontendTest, RegisterSourceTreeRegistersProjectLibImports)
{
    const char *root = "cli_frontend_register_proj";
    const char *main_source = "import \"helper\";\nfn main() -> i32 { return helper.value(); }\n";
    const vigil_source_file_t *helper_source;
    vigil_runtime_t *runtime;
    vigil_source_registry_t registry;
    vigil_source_id_t source_id;
    char main_path[4096];
    char helper_import_path[4096];
    char project_root[4096];

    runtime = NULL;
    source_id = 0U;
    remove_cli_frontend_project(root, "helper", ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(root, ERR));
    snprintf(main_path, sizeof(main_path), "%s/lib", root);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(main_path, ERR));
    snprintf(main_path, sizeof(main_path), "%s/vigil.toml", root);
    ASSERT_EQ(VIGIL_STATUS_OK, write_text_fixture(main_path, "[project]\nname = \"register\"\n", ERR));
    snprintf(main_path, sizeof(main_path), "%s/lib/helper.vigil", root);
    ASSERT_EQ(VIGIL_STATUS_OK, write_text_fixture(main_path, "pub fn value() -> i32 { return 7; }\n", ERR));
    snprintf(main_path, sizeof(main_path), "%s/main.vigil", root);
    ASSERT_EQ(VIGIL_STATUS_OK, write_text_fixture(main_path, main_source, ERR));

    ASSERT_TRUE(find_project_root(main_path, project_root, sizeof(project_root)));
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_runtime_open(&runtime, NULL, ERR));
    vigil_source_registry_init(&registry, runtime);

    ASSERT_TRUE(register_source_tree(&registry, main_path, project_root, &source_id, ERR));
    EXPECT_NE(source_id, 0U);
    EXPECT_EQ(vigil_source_registry_count(&registry), 2U);

    snprintf(helper_import_path, sizeof(helper_import_path), "%s/helper.vigil", root);
    helper_source = find_registered_source(&registry, helper_import_path);
    ASSERT_TRUE(helper_source != NULL);
    EXPECT_TRUE(strstr(vigil_string_c_str(&helper_source->text), "pub fn value") != NULL);

    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
    remove_cli_frontend_project(root, "helper", ERR);
}

TEST_F(CliFrontendTest, RegisterSourceTreeFailsForMissingImport)
{
    const char *root = "cli_frontend_missing_import";
    vigil_runtime_t *runtime;
    vigil_source_registry_t registry;
    char main_path[4096];
    char project_root[4096];

    runtime = NULL;
    remove_cli_frontend_project(root, "missing", ERR);
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_platform_mkdir_p(root, ERR));
    snprintf(main_path, sizeof(main_path), "%s/vigil.toml", root);
    ASSERT_EQ(VIGIL_STATUS_OK, write_text_fixture(main_path, "[project]\nname = \"missing\"\n", ERR));
    snprintf(main_path, sizeof(main_path), "%s/main.vigil", root);
    ASSERT_EQ(VIGIL_STATUS_OK,
              write_text_fixture(main_path, "import \"missing\";\nfn main() -> i32 { return 0; }\n", ERR));

    ASSERT_TRUE(find_project_root(main_path, project_root, sizeof(project_root)));
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_runtime_open(&runtime, NULL, ERR));
    vigil_source_registry_init(&registry, runtime);

    EXPECT_FALSE(register_source_tree(&registry, main_path, project_root, NULL, ERR));
    EXPECT_STREQ(vigil_error_message(ERR), "failed to read imported source");

    vigil_source_registry_free(&registry);
    vigil_runtime_close(&runtime);
    remove_cli_frontend_project(root, "missing", ERR);
}

#undef ERR

void register_cli_frontend_tests(void)
{
    REGISTER_TEST_F(CliFrontendTest, FindProjectRootReturnsProjectDirectory);
    REGISTER_TEST_F(CliFrontendTest, FindProjectRootReturnsZeroWithoutManifest);
    REGISTER_TEST_F(CliFrontendTest, RegisterSourceTreeRegistersProjectLibImports);
    REGISTER_TEST_F(CliFrontendTest, RegisterSourceTreeFailsForMissingImport);
}
