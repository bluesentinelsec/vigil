#ifndef BASL_LSP_H
#define BASL_LSP_H

#include <stdio.h>

#include "basl/allocator.h"
#include "basl/export.h"
#include "basl/jsonrpc.h"
#include "basl/semantic.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LSP (Language Server Protocol) server.
 *
 * Provides IDE features for BASL:
 * - Diagnostics (errors/warnings)
 * - Document symbols (outline)
 * - Go to definition
 * - Completions
 * - Hover information
 *
 * Uses JSON-RPC transport (same as DAP).
 */

typedef struct basl_lsp_server basl_lsp_server_t;

/* ── Server Lifecycle ─────────────────────────────────────── */

BASL_API basl_status_t basl_lsp_server_create(
    basl_lsp_server_t **out,
    FILE *in,
    FILE *out_stream,
    const basl_allocator_t *allocator,
    basl_error_t *error
);

BASL_API void basl_lsp_server_destroy(basl_lsp_server_t **server);

/*
 * Run the LSP message loop.
 * Blocks until a "shutdown" request is received or an I/O error occurs.
 */
BASL_API basl_status_t basl_lsp_server_process_one(
    basl_lsp_server_t *server,
    basl_error_t *error
);

BASL_API basl_status_t basl_lsp_server_run(
    basl_lsp_server_t *server,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
