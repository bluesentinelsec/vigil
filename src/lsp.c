#include "basl/lsp.h"

#include <stdlib.h>
#include <string.h>

#include "basl/json.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "internal/basl_internal.h"

/* ── Server State ─────────────────────────────────────────── */

struct basl_lsp_server {
    basl_jsonrpc_transport_t transport;
    basl_allocator_t allocator;
    basl_runtime_t *runtime;
    basl_source_registry_t sources;
    basl_semantic_index_t *index;
    int initialized;
    int shutdown_requested;
};

/* ── JSON Helpers ─────────────────────────────────────────── */

static basl_status_t jset_str(
    basl_json_value_t *obj, const char *key, const char *val,
    const basl_allocator_t *alloc, basl_error_t *error
) {
    basl_json_value_t *v = NULL;
    basl_status_t s = basl_json_string_new(alloc, val, strlen(val), &v, error);
    if (s != BASL_STATUS_OK) return s;
    return basl_json_object_set(obj, key, strlen(key), v, error);
}

static basl_status_t jset_int(
    basl_json_value_t *obj, const char *key, int64_t val,
    const basl_allocator_t *alloc, basl_error_t *error
) {
    basl_json_value_t *v = NULL;
    basl_status_t s = basl_json_number_new(alloc, (double)val, &v, error);
    if (s != BASL_STATUS_OK) return s;
    return basl_json_object_set(obj, key, strlen(key), v, error);
}

static basl_status_t jset_bool(
    basl_json_value_t *obj, const char *key, int val,
    const basl_allocator_t *alloc, basl_error_t *error
) {
    basl_json_value_t *v = NULL;
    basl_status_t s = basl_json_bool_new(alloc, val, &v, error);
    if (s != BASL_STATUS_OK) return s;
    return basl_json_object_set(obj, key, strlen(key), v, error);
}

static basl_status_t jset_null(
    basl_json_value_t *obj, const char *key,
    const basl_allocator_t *alloc, basl_error_t *error
) {
    basl_json_value_t *v = NULL;
    basl_status_t s = basl_json_null_new(alloc, &v, error);
    if (s != BASL_STATUS_OK) return s;
    return basl_json_object_set(obj, key, strlen(key), v, error);
}

static basl_status_t jset_obj(
    basl_json_value_t *obj, const char *key, basl_json_value_t *val,
    basl_error_t *error
) {
    return basl_json_object_set(obj, key, strlen(key), val, error);
}

/* ── Response Builders ────────────────────────────────────── */

static basl_status_t lsp_make_response(
    const basl_allocator_t *alloc,
    const basl_json_value_t *id,
    basl_json_value_t *result,
    basl_json_value_t **out,
    basl_error_t *error
) {
    basl_json_value_t *response = NULL;
    basl_status_t s;

    s = basl_json_object_new(alloc, &response, error);
    if (s != BASL_STATUS_OK) return s;

    jset_str(response, "jsonrpc", "2.0", alloc, error);

    if (id != NULL) {
        if (basl_json_type(id) == BASL_JSON_NUMBER) {
            jset_int(response, "id", (int64_t)basl_json_number_value(id), alloc, error);
        } else {
            const char *str = basl_json_string_value(id);
            if (str != NULL) {
                size_t len = basl_json_string_length(id);
                basl_json_value_t *id_copy = NULL;
                basl_json_string_new(alloc, str, len, &id_copy, error);
                jset_obj(response, "id", id_copy, error);
            }
        }
    }

    if (result != NULL) {
        jset_obj(response, "result", result, error);
    } else {
        jset_null(response, "result", alloc, error);
    }

    *out = response;
    return BASL_STATUS_OK;
}

/* ── Request Handlers ─────────────────────────────────────── */

static basl_status_t handle_initialize(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    basl_json_value_t **out,
    basl_error_t *error
) {
    basl_json_value_t *result = NULL;
    basl_json_value_t *capabilities = NULL;
    basl_json_value_t *server_info = NULL;
    const basl_allocator_t *a = &server->allocator;

    server->initialized = 1;

    basl_json_object_new(a, &result, error);
    basl_json_object_new(a, &capabilities, error);
    basl_json_object_new(a, &server_info, error);

    /* Text document sync: full document on open/change. */
    jset_int(capabilities, "textDocumentSync", 1, a, error);

    /* Document symbols (outline). */
    jset_bool(capabilities, "documentSymbolProvider", 1, a, error);

    /* Server info. */
    jset_str(server_info, "name", "basl-lsp", a, error);
    jset_str(server_info, "version", "0.1.0", a, error);

    jset_obj(result, "capabilities", capabilities, error);
    jset_obj(result, "serverInfo", server_info, error);

    return lsp_make_response(a, id, result, out, error);
}

static basl_status_t handle_shutdown(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    basl_json_value_t **out,
    basl_error_t *error
) {
    server->shutdown_requested = 1;
    return lsp_make_response(&server->allocator, id, NULL, out, error);
}

/* ── Notifications ────────────────────────────────────────── */

static void offset_to_line_col(
    const char *text, size_t text_len, size_t offset,
    size_t *line, size_t *col
) {
    size_t l = 0, c = 0, i;
    for (i = 0; i < text_len && i < offset; i++) {
        if (text[i] == '\n') {
            l++;
            c = 0;
        } else {
            c++;
        }
    }
    *line = l;
    *col = c;
}

static basl_status_t lsp_send_notification(
    basl_lsp_server_t *server,
    const char *method,
    basl_json_value_t *params,
    basl_error_t *error
) {
    const basl_allocator_t *a = &server->allocator;
    basl_json_value_t *msg = NULL;
    basl_status_t st;

    st = basl_json_object_new(a, &msg, error);
    if (st != BASL_STATUS_OK) { basl_json_free(&params); return st; }

    jset_str(msg, "jsonrpc", "2.0", a, error);
    jset_str(msg, "method", method, a, error);
    if (params != NULL) {
        jset_obj(msg, "params", params, error);
    }

    st = basl_jsonrpc_write(&server->transport, msg, error);
    basl_json_free(&msg);
    return st;
}

static basl_status_t publish_diagnostics(
    basl_lsp_server_t *server,
    const char *uri,
    size_t uri_len,
    basl_source_id_t source_id,
    basl_error_t *error
) {
    const basl_allocator_t *a = &server->allocator;
    basl_json_value_t *params = NULL;
    basl_json_value_t *diag_array = NULL;
    basl_json_value_t *uri_val = NULL;
    const basl_source_file_t *src;
    const basl_semantic_file_t *sem_file = NULL;
    size_t i, count;

    /* Find semantic file for this source */
    for (i = 0; i < server->index->file_count; i++) {
        if (server->index->files[i]->source_id == source_id) {
            sem_file = server->index->files[i];
            break;
        }
    }

    basl_json_object_new(a, &params, error);
    basl_json_array_new(a, &diag_array, error);
    basl_json_string_new(a, uri, uri_len, &uri_val, error);
    jset_obj(params, "uri", uri_val, error);

    if (sem_file != NULL) {
        src = basl_source_registry_get(&server->sources, source_id);
        count = basl_diagnostic_list_count(&sem_file->diagnostics);

        for (i = 0; i < count; i++) {
            const basl_diagnostic_t *d = basl_diagnostic_list_get(&sem_file->diagnostics, i);
            basl_json_value_t *diag = NULL;
            basl_json_value_t *range = NULL;
            basl_json_value_t *start_pos = NULL;
            basl_json_value_t *end_pos = NULL;
            size_t start_line, start_col, end_line, end_col;
            int severity;

            /* Map severity: LSP uses 1=Error, 2=Warning, 3=Info, 4=Hint */
            switch (d->severity) {
                case BASL_DIAGNOSTIC_ERROR: severity = 1; break;
                case BASL_DIAGNOSTIC_WARNING: severity = 2; break;
                default: severity = 3; break;
            }

            /* Convert offsets to line/column */
            offset_to_line_col(basl_string_c_str(&src->text), basl_string_length(&src->text), d->span.start_offset, &start_line, &start_col);
            offset_to_line_col(basl_string_c_str(&src->text), basl_string_length(&src->text), d->span.end_offset, &end_line, &end_col);

            basl_json_object_new(a, &diag, error);
            basl_json_object_new(a, &range, error);
            basl_json_object_new(a, &start_pos, error);
            basl_json_object_new(a, &end_pos, error);

            jset_int(start_pos, "line", (int64_t)start_line, a, error);
            jset_int(start_pos, "character", (int64_t)start_col, a, error);
            jset_int(end_pos, "line", (int64_t)end_line, a, error);
            jset_int(end_pos, "character", (int64_t)end_col, a, error);

            jset_obj(range, "start", start_pos, error);
            jset_obj(range, "end", end_pos, error);
            jset_obj(diag, "range", range, error);
            jset_int(diag, "severity", severity, a, error);
            jset_str(diag, "source", "basl", a, error);
            {
                basl_json_value_t *msg_val = NULL;
                basl_json_string_new(a, basl_string_c_str(&d->message), basl_string_length(&d->message), &msg_val, error);
                jset_obj(diag, "message", msg_val, error);
            }

            basl_json_array_push(diag_array, diag, error);
        }
    }

    jset_obj(params, "diagnostics", diag_array, error);
    return lsp_send_notification(server, "textDocument/publishDiagnostics", params, error);
}

static void handle_did_open(
    basl_lsp_server_t *server,
    const basl_json_value_t *params
) {
    const basl_json_value_t *text_doc;
    const basl_json_value_t *uri_val;
    const basl_json_value_t *text_val;
    const char *uri;
    size_t uri_len;
    const char *text;
    size_t text_len;
    basl_source_id_t source_id;
    basl_error_t error = {0};

    text_doc = basl_json_object_get(params, "textDocument");
    if (text_doc == NULL) return;

    uri_val = basl_json_object_get(text_doc, "uri");
    text_val = basl_json_object_get(text_doc, "text");
    if (uri_val == NULL || text_val == NULL) return;

    uri = basl_json_string_value(uri_val);
    text = basl_json_string_value(text_val);
    if (uri == NULL || text == NULL) return;

    uri_len = basl_json_string_length(uri_val);
    text_len = basl_json_string_length(text_val);

    /* Register source and analyze. */
    if (basl_source_registry_register(&server->sources, uri, uri_len,
                                       text, text_len, &source_id, &error) == BASL_STATUS_OK) {
        basl_semantic_index_analyze(server->index, source_id, &error);
        publish_diagnostics(server, uri, uri_len, source_id, &error);
    }
}

static basl_status_t handle_document_symbol(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    const basl_json_value_t *params,
    basl_json_value_t **out,
    basl_error_t *error
) {
    basl_json_value_t *result = NULL;
    const basl_allocator_t *a = &server->allocator;
    size_t i, count;

    (void)params;

    basl_json_array_new(a, &result, error);

    /* Return symbols from index. */
    count = basl_debug_symbol_table_count(&server->index->symbols);
    for (i = 0; i < count; i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&server->index->symbols, i);
        basl_json_value_t *symbol_info = NULL;
        basl_json_value_t *location = NULL;
        basl_json_value_t *range = NULL;
        basl_json_value_t *start_pos = NULL;
        basl_json_value_t *end_pos = NULL;
        int kind;

        /* Map symbol kind to LSP SymbolKind. */
        switch (sym->kind) {
            case BASL_DEBUG_SYMBOL_FUNCTION: kind = 12; break;
            case BASL_DEBUG_SYMBOL_CLASS: kind = 5; break;
            case BASL_DEBUG_SYMBOL_INTERFACE: kind = 11; break;
            case BASL_DEBUG_SYMBOL_ENUM: kind = 10; break;
            case BASL_DEBUG_SYMBOL_FIELD: kind = 8; break;
            case BASL_DEBUG_SYMBOL_METHOD: kind = 6; break;
            case BASL_DEBUG_SYMBOL_GLOBAL_CONST: kind = 14; break;
            case BASL_DEBUG_SYMBOL_GLOBAL_VAR: kind = 13; break;
            default: kind = 1; break;
        }

        basl_json_object_new(a, &symbol_info, error);
        basl_json_object_new(a, &location, error);
        basl_json_object_new(a, &range, error);
        basl_json_object_new(a, &start_pos, error);
        basl_json_object_new(a, &end_pos, error);

        {
            basl_json_value_t *name_val = NULL;
            basl_json_string_new(a, sym->name, sym->name_length, &name_val, error);
            jset_obj(symbol_info, "name", name_val, error);
        }
        jset_int(symbol_info, "kind", kind, a, error);

        jset_int(start_pos, "line", 0, a, error);
        jset_int(start_pos, "character", (int64_t)sym->span.start_offset, a, error);
        jset_int(end_pos, "line", 0, a, error);
        jset_int(end_pos, "character", (int64_t)sym->span.end_offset, a, error);

        jset_obj(range, "start", start_pos, error);
        jset_obj(range, "end", end_pos, error);
        jset_str(location, "uri", "", a, error);
        jset_obj(location, "range", range, error);
        jset_obj(symbol_info, "location", location, error);

        basl_json_array_push(result, symbol_info, error);
    }

    return lsp_make_response(a, id, result, out, error);
}

/* ── Message Dispatch ─────────────────────────────────────── */

static basl_status_t lsp_handle_message(
    basl_lsp_server_t *server,
    const basl_json_value_t *message,
    basl_error_t *error
) {
    const basl_json_value_t *method_val;
    const basl_json_value_t *id;
    const basl_json_value_t *params;
    basl_json_value_t *response = NULL;
    const char *method;
    size_t method_len;
    basl_status_t status = BASL_STATUS_OK;

    method_val = basl_json_object_get(message, "method");
    if (method_val == NULL) {
        return BASL_STATUS_OK;
    }

    method = basl_json_string_value(method_val);
    if (method == NULL) {
        return BASL_STATUS_OK;
    }
    method_len = basl_json_string_length(method_val);

    id = basl_json_object_get(message, "id");
    params = basl_json_object_get(message, "params");

    /* Dispatch based on method. */
    if (method_len == 10 && strncmp(method, "initialize", 10) == 0) {
        status = handle_initialize(server, id, &response, error);
    } else if (method_len == 8 && strncmp(method, "shutdown", 8) == 0) {
        status = handle_shutdown(server, id, &response, error);
    } else if (method_len == 4 && strncmp(method, "exit", 4) == 0) {
        server->shutdown_requested = 1;
        return BASL_STATUS_OK;
    } else if (method_len == 20 && strncmp(method, "textDocument/didOpen", 20) == 0) {
        handle_did_open(server, params);
    } else if (method_len == 27 && strncmp(method, "textDocument/documentSymbol", 27) == 0) {
        status = handle_document_symbol(server, id, params, &response, error);
    }
    /* Ignore other methods for now. */

    if (status != BASL_STATUS_OK) {
        basl_json_free(&response);
        return status;
    }

    if (response != NULL) {
        status = basl_jsonrpc_write(&server->transport, response, error);
        basl_json_free(&response);
    }

    return status;
}

/* ── Server Lifecycle ─────────────────────────────────────── */

basl_status_t basl_lsp_server_create(
    basl_lsp_server_t **out,
    FILE *in,
    FILE *out_stream,
    const basl_allocator_t *allocator,
    basl_error_t *error
) {
    basl_lsp_server_t *server;
    basl_status_t status;

    if (out == NULL || in == NULL || out_stream == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "lsp: invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    server = calloc(1, sizeof(basl_lsp_server_t));
    if (server == NULL) {
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (allocator != NULL && basl_allocator_is_valid(allocator)) {
        server->allocator = *allocator;
    } else {
        server->allocator = basl_default_allocator();
    }

    basl_jsonrpc_transport_init(&server->transport, in, out_stream, &server->allocator);

    status = basl_runtime_open(&server->runtime, NULL, error);
    if (status != BASL_STATUS_OK) {
        free(server);
        return status;
    }

    basl_source_registry_init(&server->sources, server->runtime);

    status = basl_semantic_index_create(&server->index, server->runtime, &server->sources, error);
    if (status != BASL_STATUS_OK) {
        basl_source_registry_free(&server->sources);
        basl_runtime_close(&server->runtime);
        free(server);
        return status;
    }

    *out = server;
    return BASL_STATUS_OK;
}

void basl_lsp_server_destroy(basl_lsp_server_t **server) {
    basl_lsp_server_t *s;

    if (server == NULL || *server == NULL) return;
    s = *server;

    basl_semantic_index_destroy(&s->index);
    basl_source_registry_free(&s->sources);
    basl_runtime_close(&s->runtime);
    free(s);
    *server = NULL;
}

basl_status_t basl_lsp_server_run(
    basl_lsp_server_t *server,
    basl_error_t *error
) {
    basl_status_t status;
    basl_json_value_t *message = NULL;

    if (server == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    while (!server->shutdown_requested) {
        status = basl_jsonrpc_read(&server->transport, &message, error);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = lsp_handle_message(server, message, error);
        basl_json_free(&message);

        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}
