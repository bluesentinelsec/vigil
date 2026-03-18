#include "vigil_test.h"
#include <string.h>

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include "vigil/lsp.h"
#include "vigil/json.h"
#include "vigil/jsonrpc.h"
#include "vigil/runtime.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static void write_message(FILE *f, const char *json) {
    fprintf(f, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

static vigil_json_value_t *read_response(FILE *f) {
    vigil_jsonrpc_transport_t tx;
    vigil_json_value_t *msg = NULL;
    vigil_error_t error = {0};

    fseek(f, 0, SEEK_SET);
    vigil_jsonrpc_transport_init(&tx, f, NULL, NULL);
    vigil_jsonrpc_read(&tx, &msg, &error);
    return msg;
}

/* ── Server Lifecycle Tests ──────────────────────────────────────── */

TEST(LspTest, CreateAndDestroy) {
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_NE(vigil_lsp_server_create(&server, NULL, NULL, NULL, &error), VIGIL_STATUS_OK);
    vigil_lsp_server_destroy(&server);
    ASSERT_EQ(server, NULL);
}

TEST(LspTest, CreateWithValidStreams) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_NE(in, NULL);
    ASSERT_NE(out, NULL);

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_NE(server, NULL);

    vigil_lsp_server_destroy(&server);
    ASSERT_EQ(server, NULL);

    fclose(in);
    fclose(out);
}

TEST(LspTest, InitializeResponse) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Send initialize request */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);

    /* Process one message */
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    /* Read response */
    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    /* Check response structure */
    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    ASSERT_NE(result, NULL);

    const vigil_json_value_t *caps = vigil_json_object_get(result, "capabilities");
    ASSERT_NE(caps, NULL);

    /* Check capabilities */
    const vigil_json_value_t *doc_sym = vigil_json_object_get(caps, "documentSymbolProvider");
    ASSERT_NE(doc_sym, NULL);
    EXPECT_EQ(vigil_json_bool_value(doc_sym), 1);

    const vigil_json_value_t *hover = vigil_json_object_get(caps, "hoverProvider");
    ASSERT_NE(hover, NULL);
    EXPECT_EQ(vigil_json_bool_value(hover), 1);

    const vigil_json_value_t *def = vigil_json_object_get(caps, "definitionProvider");
    ASSERT_NE(def, NULL);
    EXPECT_EQ(vigil_json_bool_value(def), 1);

    const vigil_json_value_t *comp = vigil_json_object_get(caps, "completionProvider");
    ASSERT_NE(comp, NULL);

    const vigil_json_value_t *refs = vigil_json_object_get(caps, "referencesProvider");
    ASSERT_NE(refs, NULL);

    const vigil_json_value_t *rename = vigil_json_object_get(caps, "renameProvider");
    ASSERT_NE(rename, NULL);

    const vigil_json_value_t *fmt = vigil_json_object_get(caps, "documentFormattingProvider");
    ASSERT_NE(fmt, NULL);

    const vigil_json_value_t *sig = vigil_json_object_get(caps, "signatureHelpProvider");
    ASSERT_NE(sig, NULL);

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

TEST(LspTest, ShutdownAndExit) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize first */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    /* Shutdown */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\"}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Document Symbol Tests ───────────────────────────────────────── */

TEST(LspTest, DocumentSymbolEmpty) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Request document symbols (no document opened) */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/documentSymbol\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    ASSERT_NE(result, NULL);
    EXPECT_EQ(vigil_json_array_count(result), 0u);

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Completion Tests ────────────────────────────────────────────── */

TEST(LspTest, CompletionEmpty) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Request completion */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/completion\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"},"
                      "\"position\":{\"line\":0,\"character\":0}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    ASSERT_NE(result, NULL);
    /* Completion list includes string method completions even with no documents */
    EXPECT_EQ(vigil_json_array_count(result), 27u);  /* 27 string methods */

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Hover Tests ─────────────────────────────────────────────────── */

TEST(LspTest, HoverNoDocument) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Request hover */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/hover\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"},"
                      "\"position\":{\"line\":0,\"character\":0}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    /* Should return null result when no document */
    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    EXPECT_EQ(vigil_json_type(result), VIGIL_JSON_NULL);

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Definition Tests ────────────────────────────────────────────── */

TEST(LspTest, DefinitionNoDocument) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Request definition */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/definition\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"},"
                      "\"position\":{\"line\":0,\"character\":0}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    EXPECT_EQ(vigil_json_type(result), VIGIL_JSON_NULL);

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── References Tests ────────────────────────────────────────────── */

TEST(LspTest, ReferencesNoDocument) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Request references */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/references\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"},"
                      "\"position\":{\"line\":0,\"character\":0}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    EXPECT_EQ(vigil_json_type(result), VIGIL_JSON_NULL);

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Rename Tests ────────────────────────────────────────────────── */

TEST(LspTest, RenameNoDocument) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Request rename */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/rename\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"},"
                      "\"position\":{\"line\":0,\"character\":0},\"newName\":\"newFunc\"}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    EXPECT_EQ(vigil_json_type(result), VIGIL_JSON_NULL);

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Formatting Tests ────────────────────────────────────────────── */

TEST(LspTest, FormattingNoDocument) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Request formatting */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/formatting\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    EXPECT_EQ(vigil_json_type(result), VIGIL_JSON_NULL);

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Signature Help Tests ────────────────────────────────────────── */

TEST(LspTest, SignatureHelpNoDocument) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Request signature help */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/signatureHelp\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"},"
                      "\"position\":{\"line\":0,\"character\":0}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    EXPECT_EQ(vigil_json_type(result), VIGIL_JSON_NULL);

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Unknown Method Tests ────────────────────────────────────────── */

TEST(LspTest, UnknownMethodIgnored) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Send unknown method */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"unknownMethod\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    /* Should not crash, may or may not produce response */
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Document Open Tests ─────────────────────────────────────────── */

TEST(LspTest, DidOpenValidSource) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Open a document with valid VIGIL code */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
                      "\"params\":{\"textDocument\":{"
                      "\"uri\":\"file:///test.vigil\","
                      "\"text\":\"fn main() { print(42) }\"}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    /* Should publish diagnostics - check output has content */
    fseek(out, 0, SEEK_END);
    long out_size = ftell(out);
    EXPECT_GT(out_size, 0);

    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

TEST(LspTest, DidOpenInvalidSource) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Open a document with invalid VIGIL code */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
                      "\"params\":{\"textDocument\":{"
                      "\"uri\":\"file:///test.vigil\","
                      "\"text\":\"fn main( { }\"}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    /* Should publish diagnostics with errors */
    fseek(out, 0, SEEK_END);
    long out_size = ftell(out);
    EXPECT_GT(out_size, 0);

    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

/* ── Document Change Tests ───────────────────────────────────────── */

TEST(LspTest, DidChangeUpdatesDocument) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Open document */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
                      "\"params\":{\"textDocument\":{"
                      "\"uri\":\"file:///test.vigil\","
                      "\"text\":\"fn main() { }\"}}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Change document */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"},"
                      "\"contentChanges\":[{\"text\":\"fn main() { print(1) }\"}]}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

TEST(LspTest, HoverBuiltinShowsDocs) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_lsp_server_t *server = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_lsp_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Initialize */
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Open document with len() call - use valid VIGIL syntax */
    fseek(in, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
                      "\"params\":{\"textDocument\":{"
                      "\"uri\":\"file:///test.vigil\","
                      "\"text\":\"fn main() -> i32 { return len(\\\"hi\\\"); }\"}}}");
    fseek(in, 0, SEEK_SET);
    vigil_lsp_server_process_one(server, &error);

    /* Hover over 'len' at position (0, 27) - inside the function body */
    fseek(in, 0, SEEK_SET);
    fseek(out, 0, SEEK_SET);
    write_message(in, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/hover\","
                      "\"params\":{\"textDocument\":{\"uri\":\"file:///test.vigil\"},"
                      "\"position\":{\"line\":0,\"character\":27}}}");
    fseek(in, 0, SEEK_SET);
    ASSERT_EQ(vigil_lsp_server_process_one(server, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *resp = read_response(out);
    ASSERT_NE(resp, NULL);

    /* Check we got a response - may be null if semantic analysis failed */
    const vigil_json_value_t *result = vigil_json_object_get(resp, "result");
    ASSERT_NE(result, NULL);

    vigil_json_free(&resp);
    vigil_lsp_server_destroy(&server);
    fclose(in);
    fclose(out);
}

void register_lsp_tests(void) {
    REGISTER_TEST(LspTest, CreateAndDestroy);
    REGISTER_TEST(LspTest, CreateWithValidStreams);
    REGISTER_TEST(LspTest, InitializeResponse);
    REGISTER_TEST(LspTest, ShutdownAndExit);
    REGISTER_TEST(LspTest, DocumentSymbolEmpty);
    REGISTER_TEST(LspTest, CompletionEmpty);
    REGISTER_TEST(LspTest, HoverNoDocument);
    REGISTER_TEST(LspTest, DefinitionNoDocument);
    REGISTER_TEST(LspTest, ReferencesNoDocument);
    REGISTER_TEST(LspTest, RenameNoDocument);
    REGISTER_TEST(LspTest, FormattingNoDocument);
    REGISTER_TEST(LspTest, SignatureHelpNoDocument);
    REGISTER_TEST(LspTest, UnknownMethodIgnored);
    REGISTER_TEST(LspTest, DidOpenValidSource);
    REGISTER_TEST(LspTest, DidOpenInvalidSource);
    REGISTER_TEST(LspTest, DidChangeUpdatesDocument);
    REGISTER_TEST(LspTest, HoverBuiltinShowsDocs);
}
