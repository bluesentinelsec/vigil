#include "vigil_test.h"
#include <string.h>

/* Suppress MSVC C4996 for tmpfile — it's standard C and fine for tests. */
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include "vigil/dap.h"
#include "vigil/json.h"
#include "vigil/jsonrpc.h"
#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/stdlib.h"
#include "vigil/vm.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Write a DAP request as a Content-Length framed message to a FILE. */
static void write_message(FILE *f, const char *json)
{
    fprintf(f, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

/* Read all content from a FILE into a string. */
static char *read_all(FILE *f)
{
    long size;
    char *buf;
    size_t n;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)calloc(1, (size_t)size + 1);
    n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    return buf;
}

/* ── JSON-RPC transport tests ────────────────────────────────────── */

TEST(VigilJsonRpcTest, ReadWriteRoundtrip)
{
    FILE *pipe = tmpfile();
    ASSERT_NE(pipe, NULL);

    /* Write a message. */
    vigil_jsonrpc_transport_t tx;
    vigil_jsonrpc_transport_init(&tx, NULL, pipe, NULL);

    vigil_json_value_t *msg = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_object_new(NULL, &msg, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *val = NULL;
    vigil_json_string_new(NULL, "hello", 5, &val, &error);
    vigil_json_object_set(msg, "test", 4, val, &error);

    ASSERT_EQ(vigil_jsonrpc_write(&tx, msg, &error), VIGIL_STATUS_OK);
    vigil_json_free(&msg);

    /* Read it back. */
    fseek(pipe, 0, SEEK_SET);
    tx.in = pipe;
    vigil_json_value_t *read_msg = NULL;
    ASSERT_EQ(vigil_jsonrpc_read(&tx, &read_msg, &error), VIGIL_STATUS_OK);
    ASSERT_NE(read_msg, NULL);

    const vigil_json_value_t *test_val = vigil_json_object_get(read_msg, "test");
    ASSERT_NE(test_val, NULL);
    EXPECT_STREQ(vigil_json_string_value(test_val), "hello");

    vigil_json_free(&read_msg);
    fclose(pipe);
}

TEST(VigilJsonRpcTest, ReadMultipleMessages)
{
    FILE *pipe = tmpfile();
    ASSERT_NE(pipe, NULL);

    write_message(pipe, "{\"seq\":1}");
    write_message(pipe, "{\"seq\":2}");
    fseek(pipe, 0, SEEK_SET);

    vigil_jsonrpc_transport_t tx;
    vigil_jsonrpc_transport_init(&tx, pipe, NULL, NULL);
    vigil_error_t error = {0};

    vigil_json_value_t *m1 = NULL, *m2 = NULL;
    ASSERT_EQ(vigil_jsonrpc_read(&tx, &m1, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_jsonrpc_read(&tx, &m2, &error), VIGIL_STATUS_OK);

    EXPECT_EQ((int)vigil_json_number_value(vigil_json_object_get(m1, "seq")), 1);
    EXPECT_EQ((int)vigil_json_number_value(vigil_json_object_get(m2, "seq")), 2);

    vigil_json_free(&m1);
    vigil_json_free(&m2);
    fclose(pipe);
}

TEST(VigilJsonRpcTest, ReadEofReturnsError)
{
    FILE *pipe = tmpfile();
    ASSERT_NE(pipe, NULL);
    fseek(pipe, 0, SEEK_SET);

    vigil_jsonrpc_transport_t tx;
    vigil_jsonrpc_transport_init(&tx, pipe, NULL, NULL);
    vigil_error_t error = {0};

    vigil_json_value_t *msg = NULL;
    EXPECT_NE(vigil_jsonrpc_read(&tx, &msg, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(msg, NULL);
    fclose(pipe);
}

/* ── DAP server tests ────────────────────────────────────────────── */

TEST(VigilDapTest, CreateAndDestroy)
{
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_error_t error = {0};

    vigil_dap_server_t *server = NULL;
    ASSERT_EQ(vigil_dap_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_NE(server, NULL);
    vigil_dap_server_destroy(&server);
    EXPECT_EQ(server, NULL);

    fclose(in);
    fclose(out);
}

TEST(VigilDapTest, InitializeAndDisconnect)
{
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_error_t error = {0};

    /* Write initialize + disconnect sequence. */
    write_message(in, "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{}}");
    write_message(in, "{\"seq\":2,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}");
    fseek(in, 0, SEEK_SET);

    vigil_dap_server_t *server = NULL;
    ASSERT_EQ(vigil_dap_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);

    /* Need a runtime for set_runtime. */
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);

    vigil_source_registry_t registry;
    vigil_source_registry_init(&registry, runtime);

    ASSERT_EQ(vigil_dap_server_set_runtime(server, vm, &registry, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_dap_server_run(server, &error), VIGIL_STATUS_OK);

    /* Check output contains initialize response. */
    fseek(out, 0, SEEK_SET);
    char *output = read_all(out);
    EXPECT_TRUE(strstr(output, "\"command\":\"initialize\"") != NULL);
    EXPECT_TRUE(strstr(output, "\"success\":true") != NULL);
    EXPECT_TRUE(strstr(output, "\"event\":\"initialized\"") != NULL);

    vigil_dap_server_destroy(&server);
    free(output);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    fclose(in);
    fclose(out);
}

TEST(VigilDapTest, LaunchAndBreakpoint)
{
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    vigil_error_t error = {0};

    /* Compile a simple program. */
    vigil_runtime_t *runtime = NULL;
    vigil_vm_t *vm = NULL;
    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_vm_open(&vm, runtime, NULL, &error), VIGIL_STATUS_OK);

    vigil_source_registry_t registry;
    vigil_source_registry_init(&registry, runtime);
    vigil_diagnostic_list_t diagnostics;
    vigil_diagnostic_list_init(&diagnostics, runtime);
    vigil_native_registry_t natives;
    vigil_native_registry_init(&natives);
    vigil_stdlib_register_all(&natives, &error);

    vigil_source_id_t source_id = 0;
    vigil_source_registry_register_cstr(&registry, "main.vigil",
                                        "fn main() -> i32 {\n"
                                        "    i32 x = 10;\n"
                                        "    i32 y = 20;\n"
                                        "    return x + y;\n"
                                        "}\n",
                                        &source_id, &error);

    vigil_object_t *function = NULL;
    ASSERT_EQ(vigil_compile_source_with_natives(&registry, source_id, &natives, &function, &diagnostics, &error),
              VIGIL_STATUS_OK);

    /* Write DAP sequence: initialize, setBreakpoints on line 3,
       configurationDone, launch, then continue + disconnect when stopped. */
    write_message(in, "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{}}");
    write_message(in, "{\"seq\":2,\"type\":\"request\",\"command\":\"setBreakpoints\","
                      "\"arguments\":{\"source\":{\"path\":\"main.vigil\"},"
                      "\"breakpoints\":[{\"line\":3}]}}");
    write_message(in, "{\"seq\":3,\"type\":\"request\",\"command\":\"configurationDone\",\"arguments\":{}}");
    write_message(in, "{\"seq\":4,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{}}");
    /* When stopped at breakpoint, request stack trace then continue. */
    write_message(in, "{\"seq\":5,\"type\":\"request\",\"command\":\"stackTrace\",\"arguments\":{\"threadId\":1}}");
    write_message(in, "{\"seq\":6,\"type\":\"request\",\"command\":\"continue\",\"arguments\":{\"threadId\":1}}");
    /* After program finishes, disconnect. */
    write_message(in, "{\"seq\":7,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}");
    fseek(in, 0, SEEK_SET);

    vigil_dap_server_t *server = NULL;
    ASSERT_EQ(vigil_dap_server_create(&server, in, out, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_dap_server_set_runtime(server, vm, &registry, &error), VIGIL_STATUS_OK);
    vigil_dap_server_set_program(server, function, source_id);
    ASSERT_EQ(vigil_dap_server_run(server, &error), VIGIL_STATUS_OK);

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

    vigil_dap_server_destroy(&server);
    free(output);
    vigil_object_release(&function);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_native_registry_free(&natives);
    vigil_source_registry_free(&registry);
    vigil_vm_close(&vm);
    vigil_runtime_close(&runtime);
    fclose(in);
    fclose(out);
}

void register_dap_tests(void)
{
    REGISTER_TEST(VigilJsonRpcTest, ReadWriteRoundtrip);
    REGISTER_TEST(VigilJsonRpcTest, ReadMultipleMessages);
    REGISTER_TEST(VigilJsonRpcTest, ReadEofReturnsError);
    REGISTER_TEST(VigilDapTest, CreateAndDestroy);
    REGISTER_TEST(VigilDapTest, InitializeAndDisconnect);
    REGISTER_TEST(VigilDapTest, LaunchAndBreakpoint);
}
