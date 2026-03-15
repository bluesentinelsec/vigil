#ifndef BASL_DAP_H
#define BASL_DAP_H

#include <stdio.h>

#include "basl/allocator.h"
#include "basl/debugger.h"
#include "basl/export.h"
#include "basl/json.h"
#include "basl/jsonrpc.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DAP (Debug Adapter Protocol) server.
 *
 * Bridges a JSON-RPC transport to the basl_debugger API.
 * The server reads DAP requests from stdin, translates them to
 * debugger API calls, and writes DAP events/responses to stdout.
 *
 * Typical usage:
 *   basl_dap_server_t server;
 *   basl_dap_server_init(&server, stdin, stdout, allocator);
 *   basl_dap_server_set_runtime(&server, vm, sources);
 *   basl_dap_server_run(&server, &error);  // blocks until disconnect
 */

typedef struct basl_dap_server basl_dap_server_t;

BASL_API basl_status_t basl_dap_server_create(
    basl_dap_server_t **out,
    FILE *in,
    FILE *out_stream,
    const basl_allocator_t *allocator,
    basl_error_t *error
);

BASL_API void basl_dap_server_destroy(basl_dap_server_t **server);

/*
 * Attach the DAP server to a VM and source registry.
 * Must be called before run.  The server creates its own debugger internally.
 */
BASL_API basl_status_t basl_dap_server_set_runtime(
    basl_dap_server_t *server,
    basl_vm_t *vm,
    const basl_source_registry_t *sources,
    basl_error_t *error
);

/*
 * Set the compiled function to execute when a "launch" request arrives.
 */
BASL_API void basl_dap_server_set_program(
    basl_dap_server_t *server,
    basl_object_t *function,
    basl_source_id_t source_id
);

/*
 * Run the DAP message loop.  Blocks until a "disconnect" request
 * is received or an I/O error occurs.
 */
BASL_API basl_status_t basl_dap_server_run(
    basl_dap_server_t *server,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
