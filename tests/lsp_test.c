#include "basl_test.h"
#include "basl/lsp.h"

TEST(LspTest, CreateAndDestroy) {
    basl_lsp_server_t *server = NULL;
    basl_error_t error = {0};

    /* Create with NULL streams should fail. */
    ASSERT_NE(basl_lsp_server_create(&server, NULL, NULL, NULL, &error), BASL_STATUS_OK);

    basl_lsp_server_destroy(&server);
    ASSERT_EQ(server, NULL);
}

void register_lsp_tests(void) {
    REGISTER_TEST(LspTest, CreateAndDestroy);
}
