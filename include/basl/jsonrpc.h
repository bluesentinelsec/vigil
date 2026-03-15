#ifndef BASL_JSONRPC_H
#define BASL_JSONRPC_H

#include <stdio.h>

#include "basl/allocator.h"
#include "basl/export.h"
#include "basl/json.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * JSON-RPC message transport using Content-Length framing.
 *
 * Messages are delimited by HTTP-style headers:
 *   Content-Length: <n>\r\n
 *   \r\n
 *   <n bytes of JSON>
 *
 * This is the wire format used by both DAP and LSP.
 */

typedef struct basl_jsonrpc_transport {
    FILE *in;
    FILE *out;
    const basl_allocator_t *allocator;
} basl_jsonrpc_transport_t;

BASL_API void basl_jsonrpc_transport_init(
    basl_jsonrpc_transport_t *transport,
    FILE *in,
    FILE *out,
    const basl_allocator_t *allocator
);

/*
 * Read one Content-Length framed JSON message.
 * Blocks until a complete message is available or EOF/error.
 * Returns BASL_STATUS_OK on success, with *out set to the parsed JSON.
 * Returns BASL_STATUS_INTERNAL on EOF or I/O error.
 */
BASL_API basl_status_t basl_jsonrpc_read(
    basl_jsonrpc_transport_t *transport,
    basl_json_value_t **out,
    basl_error_t *error
);

/*
 * Write one Content-Length framed JSON message.
 */
BASL_API basl_status_t basl_jsonrpc_write(
    basl_jsonrpc_transport_t *transport,
    const basl_json_value_t *message,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
