#include "basl_test.h"
#include <string.h>

/* Suppress MSVC C4996 for tmpfile — it's standard C and fine for tests. */
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif


#include "basl/dap.h"
#include "basl/json.h"
#include "basl/jsonrpc.h"
#include "basl/native_module.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/stdlib.h"
#include "basl/vm.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Write a DAP request as a Content-Length framed message to a FILE. */
static void write_message(FILE *f, const char *json) {
    fprintf(f, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

/* Read all content from a FILE into a string. */
static char *read_all(FILE *f) {
    long size;
    char *buf;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)calloc(1, (size_t)size + 1);
    (void)fread(buf, 1, (size_t)size, f);
    return buf;
}

/* ── JSON-RPC transport tests ────────────────────────────────────── */

TEST(BaslJsonRpcTest, ReadWriteRoundtrip) {
    FILE *pipe = tmpfile();
    ASSERT_NE(pipe, NULL);

    /* Write a message. */
    basl_jsonrpc_transport_t tx;
    basl_jsonrpc_transport_init(&tx, NULL, pipe, NULL);

    basl_json_value_t *msg = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_object_new(NULL, &msg, &error), BASL_STATUS_OK);

    basl_json_value_t *val = NULL;
    basl_json_string_new(NULL, "hello", 5, &val, &error);
    basl_json_object_set(msg, "test", 4, val, &error);

    ASSERT_EQ(basl_jsonrpc_write(&tx, msg, &error), BASL_STATUS_OK);
    basl_json_free(&msg);

    /* Read it back. */
    fseek(pipe, 0, SEEK_SET);
    tx.in = pipe;
    basl_json_value_t *read_msg = NULL;
    ASSERT_EQ(basl_jsonrpc_read(&tx, &read_msg, &error), BASL_STATUS_OK);
    ASSERT_NE(read_msg, NULL);

    const basl_json_value_t *test_val = basl_json_object_get(read_msg, "test");
    ASSERT_NE(test_val, NULL);
    EXPECT_STREQ(basl_json_string_value(test_val), "hello");

    basl_json_free(&read_msg);
    fclose(pipe);
}

TEST(BaslJsonRpcTest, ReadMultipleMessages) {
    FILE *pipe = tmpfile();
    ASSERT_NE(pipe, NULL);

    write_message(pipe, "{\"seq\":1}");
    write_message(pipe, "{\"seq\":2}");
    fseek(pipe, 0, SEEK_SET);

    basl_jsonrpc_transport_t tx;
    basl_jsonrpc_transport_init(&tx, pipe, NULL, NULL);
    basl_error_t error = {0};

    basl_json_value_t *m1 = NULL, *m2 = NULL;
    ASSERT_EQ(basl_jsonrpc_read(&tx, &m1, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_jsonrpc_read(&tx, &m2, &error), BASL_STATUS_OK);

    EXPECT_EQ((int)basl_json_number_value(basl_json_object_get(m1, "seq")), 1);
    EXPECT_EQ((int)basl_json_number_value(basl_json_object_get(m2, "seq")), 2);

    basl_json_free(&m1);
    basl_json_free(&m2);
    fclose(pipe);
}

TEST(BaslJsonRpcTest, ReadEofReturnsError) {
    FILE *pipe = tmpfile();
    ASSERT_NE(pipe, NULL);
    fseek(pipe, 0, SEEK_SET);

    basl_jsonrpc_transport_t tx;
    basl_jsonrpc_transport_init(&tx, pipe, NULL, NULL);
    basl_error_t error = {0};

    basl_json_value_t *msg = NULL;
    EXPECT_NE(basl_jsonrpc_read(&tx, &msg, &error), BASL_STATUS_OK);
    EXPECT_EQ(msg, NULL);
    fclose(pipe);
}

/* ── DAP server tests ────────────────────────────────────────────── */

TEST(BaslDapTest, CreateAndDestroy) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    basl_error_t error = {0};

    basl_dap_server_t *server = NULL;
    ASSERT_EQ(basl_dap_server_create(&server, in, out, NULL, &error), BASL_STATUS_OK);
    ASSERT_NE(server, NULL);
    basl_dap_server_destroy(&server);
    EXPECT_EQ(server, NULL);

    fclose(in);
    fclose(out);
}

TEST(BaslDapTest, InitializeAndDisconnect) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    basl_error_t error = {0};

    /* Write initialize + disconnect sequence. */
    write_message(in, "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{}}");
    write_message(in, "{\"seq\":2,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}");
    fseek(in, 0, SEEK_SET);

    basl_dap_server_t *server = NULL;
    ASSERT_EQ(basl_dap_server_create(&server, in, out, NULL, &error), BASL_STATUS_OK);

    /* Need a runtime for set_runtime. */
    basl_runtime_t *runtime = NULL;
    basl_vm_t *vm = NULL;
    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, NULL, &error), BASL_STATUS_OK);

    basl_source_registry_t registry;
    basl_source_registry_init(&registry, runtime);

    ASSERT_EQ(basl_dap_server_set_runtime(server, vm, &registry, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_dap_server_run(server, &error), BASL_STATUS_OK);

    /* Check output contains initialize response. */
    fseek(out, 0, SEEK_SET);
    char *output = read_all(out);
    EXPECT_TRUE(strstr(output, "\"command\":\"initialize\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"success\":true") != NULL);
    EXPECT_TRUE(strstr(output, "\"event\":\"initialized\"") != NULL);

    basl_dap_server_destroy(&server);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    fclose(in);
    fclose(out);
}

TEST(BaslDapTest, LaunchAndBreakpoint) {
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    basl_error_t error = {0};

    /* Compile a simple program. */
    basl_runtime_t *runtime = NULL;
    basl_vm_t *vm = NULL;
    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, NULL, &error), BASL_STATUS_OK);

    basl_source_registry_t registry;
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_t diagnostics;
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_native_registry_t natives;
    basl_native_registry_init(&natives);
    basl_stdlib_register_all(&natives, &error);

    basl_source_id_t source_id = 0;
    basl_source_registry_register_cstr(
        &registry, "main.basl",
        "fn main() -> i32 {\n"
        "    i32 x = 10;\n"
        "    i32 y = 20;\n"
        "    return x + y;\n"
        "}\n",
        &source_id, &error);

    basl_object_t *function = NULL;
    ASSERT_EQ(
        basl_compile_source_with_natives(
            &registry, source_id, &natives, &function,
            &diagnostics, &error),
        BASL_STATUS_OK);

    /* Write DAP sequence: initialize, setBreakpoints on line 3,
       configurationDone, launch, then continue + disconnect when stopped. */
    write_message(in, "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{}}");
    write_message(in, "{\"seq\":2,\"type\":\"request\",\"command\":\"setBreakpoints\","
                      "\"arguments\":{\"source\":{\"path\":\"main.basl\"},"
                      "\"breakpoints\":[{\"line\":3}]}}");
    write_message(in, "{\"seq\":3,\"type\":\"request\",\"command\":\"configurationDone\",\"arguments\":{}}");
    write_message(in, "{\"seq\":4,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{}}");
    /* When stopped at breakpoint, request stack trace then continue. */
    write_message(in, "{\"seq\":5,\"type\":\"request\",\"command\":\"stackTrace\",\"arguments\":{\"threadId\":1}}");
    write_message(in, "{\"seq\":6,\"type\":\"request\",\"command\":\"continue\",\"arguments\":{\"threadId\":1}}");
    /* After program finishes, disconnect. */
    write_message(in, "{\"seq\":7,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}");
    fseek(in, 0, SEEK_SET);

    basl_dap_server_t *server = NULL;
    ASSERT_EQ(basl_dap_server_create(&server, in, out, NULL, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_dap_server_set_runtime(server, vm, &registry, &error), BASL_STATUS_OK);
    basl_dap_server_set_program(server, function, source_id);
    ASSERT_EQ(basl_dap_server_run(server, &error), BASL_STATUS_OK);

    /* Verify output. */
    fseek(out, 0, SEEK_SET);
    char *output = read_all(out);

    /* Should see: initialized event, breakpoint response, stopped event,
       stack trace response, terminated event. */
    EXPECT_TRUE(strstr(output, "\"event\":\"initialized\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"command\":\"setBreakpoints\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"event\":\"stopped\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"reason\":\"breakpoint\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"command\":\"stackTrace\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"event\":\"terminated\"") != NULL);

    basl_dap_server_destroy(&server);
    basl_object_release(&function);
    basl_diagnostic_list_free(&diagnostics);
    basl_native_registry_free(&natives);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    fclose(in);
    fclose(out);
}
