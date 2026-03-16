#include "basl/lsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/fmt.h"
#include "basl/json.h"
#include "basl/lexer.h"
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

    /* Hover (type info). */
    jset_bool(capabilities, "hoverProvider", 1, a, error);

    /* Go to definition. */
    jset_bool(capabilities, "definitionProvider", 1, a, error);

    /* Completion. */
    jset_bool(capabilities, "completionProvider", 1, a, error);

    /* Find references. */
    jset_bool(capabilities, "referencesProvider", 1, a, error);

    /* Rename. */
    jset_bool(capabilities, "renameProvider", 1, a, error);

    /* Formatting. */
    jset_bool(capabilities, "documentFormattingProvider", 1, a, error);

    /* Signature help. */
    {
        basl_json_value_t *sig_opts = NULL;
        basl_json_value_t *trigger_chars = NULL;
        basl_json_value_t *open_paren = NULL;
        basl_json_value_t *comma = NULL;

        basl_json_object_new(a, &sig_opts, error);
        basl_json_array_new(a, &trigger_chars, error);
        basl_json_string_new(a, "(", 1, &open_paren, error);
        basl_json_string_new(a, ",", 1, &comma, error);

        basl_json_array_push(trigger_chars, open_paren, error);
        basl_json_array_push(trigger_chars, comma, error);
        jset_obj(sig_opts, "triggerCharacters", trigger_chars, error);
        jset_obj(capabilities, "signatureHelpProvider", sig_opts, error);
    }

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

/* ── URI Lookup Helper ────────────────────────────────────── */

static basl_source_id_t find_source_by_uri(
    basl_lsp_server_t *server,
    const char *uri,
    size_t uri_len
) {
    size_t i;
    for (i = 0; i < server->index->file_count; i++) {
        const basl_source_file_t *src = basl_source_registry_get(
            &server->sources, server->index->files[i]->source_id);
        if (src != NULL &&
            basl_string_length(&src->path) == uri_len &&
            strncmp(basl_string_c_str(&src->path), uri, uri_len) == 0) {
            return server->index->files[i]->source_id;
        }
    }
    return 0;
}

static const basl_semantic_file_t *find_semantic_file(
    basl_lsp_server_t *server,
    basl_source_id_t source_id
) {
    size_t i;
    for (i = 0; i < server->index->file_count; i++) {
        if (server->index->files[i]->source_id == source_id) {
            return server->index->files[i];
        }
    }
    return NULL;
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

static size_t line_col_to_offset(
    const char *text, size_t text_len, size_t line, size_t col
) {
    size_t l = 0, c = 0, i;
    for (i = 0; i < text_len; i++) {
        if (l == line && c == col) return i;
        if (text[i] == '\n') {
            l++;
            c = 0;
        } else {
            c++;
        }
    }
    return i;
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

static void handle_did_change(
    basl_lsp_server_t *server,
    const basl_json_value_t *params
) {
    const basl_json_value_t *text_doc;
    const basl_json_value_t *uri_val;
    const basl_json_value_t *changes;
    const basl_json_value_t *change;
    const basl_json_value_t *text_val;
    const char *uri;
    size_t uri_len;
    const char *text;
    size_t text_len;
    basl_source_id_t source_id;
    basl_error_t error = {0};

    text_doc = basl_json_object_get(params, "textDocument");
    changes = basl_json_object_get(params, "contentChanges");
    if (text_doc == NULL || changes == NULL) return;

    uri_val = basl_json_object_get(text_doc, "uri");
    if (uri_val == NULL) return;

    /* Full sync: take the last change's text */
    if (basl_json_array_count(changes) == 0) return;
    change = basl_json_array_get(changes, basl_json_array_count(changes) - 1);
    if (change == NULL) return;

    text_val = basl_json_object_get(change, "text");
    if (text_val == NULL) return;

    uri = basl_json_string_value(uri_val);
    text = basl_json_string_value(text_val);
    if (uri == NULL || text == NULL) return;

    uri_len = basl_json_string_length(uri_val);
    text_len = basl_json_string_length(text_val);

    /* Re-register and re-analyze */
    if (basl_source_registry_register(&server->sources, uri, uri_len,
                                       text, text_len, &source_id, &error) == BASL_STATUS_OK) {
        basl_semantic_index_analyze(server->index, source_id, &error);
        publish_diagnostics(server, uri, uri_len, source_id, &error);
    }
}

static void handle_did_close(
    basl_lsp_server_t *server,
    const basl_json_value_t *params
) {
    (void)server;
    (void)params;
    /* No cleanup needed - source registry doesn't support removal yet */
}

static basl_status_t handle_references(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    const basl_json_value_t *params,
    basl_json_value_t **out,
    basl_error_t *error
) {
    const basl_allocator_t *a = &server->allocator;
    const basl_json_value_t *text_doc;
    const basl_json_value_t *position;
    const basl_json_value_t *uri_val;
    const basl_json_value_t *line_val;
    const basl_json_value_t *char_val;
    const basl_source_file_t *src;
    basl_semantic_reference_t *refs = NULL;
    size_t ref_count = 0;
    basl_json_value_t *result = NULL;
    basl_source_id_t source_id;
    size_t line, col, offset, i;

    text_doc = basl_json_object_get(params, "textDocument");
    position = basl_json_object_get(params, "position");
    if (text_doc == NULL || position == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = basl_json_object_get(text_doc, "uri");
    line_val = basl_json_object_get(position, "line");
    char_val = basl_json_object_get(position, "character");
    if (uri_val == NULL || line_val == NULL || char_val == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, basl_json_string_value(uri_val),
                                   basl_json_string_length(uri_val));
    if (source_id == 0) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = basl_source_registry_get(&server->sources, source_id);
    line = (size_t)basl_json_number_value(line_val);
    col = (size_t)basl_json_number_value(char_val);
    offset = line_col_to_offset(basl_string_c_str(&src->text), basl_string_length(&src->text), line, col);

    basl_json_array_new(a, &result, error);

    if (basl_semantic_index_references_at(server->index, source_id, offset, &refs, &ref_count, error) == BASL_STATUS_OK && refs != NULL) {
        for (i = 0; i < ref_count; i++) {
            basl_json_value_t *loc = NULL;
            basl_json_value_t *range = NULL;
            basl_json_value_t *start_pos = NULL;
            basl_json_value_t *end_pos = NULL;
            const basl_source_file_t *ref_src;
            size_t start_line, start_col, end_line, end_col;

            ref_src = basl_source_registry_get(&server->sources, refs[i].span.source_id);
            if (ref_src == NULL) continue;

            offset_to_line_col(basl_string_c_str(&ref_src->text), basl_string_length(&ref_src->text),
                               refs[i].span.start_offset, &start_line, &start_col);
            offset_to_line_col(basl_string_c_str(&ref_src->text), basl_string_length(&ref_src->text),
                               refs[i].span.end_offset, &end_line, &end_col);

            basl_json_object_new(a, &loc, error);
            basl_json_object_new(a, &range, error);
            basl_json_object_new(a, &start_pos, error);
            basl_json_object_new(a, &end_pos, error);

            {
                basl_json_value_t *uri_out = NULL;
                basl_json_string_new(a, basl_string_c_str(&ref_src->path), basl_string_length(&ref_src->path), &uri_out, error);
                jset_obj(loc, "uri", uri_out, error);
            }

            jset_int(start_pos, "line", (int64_t)start_line, a, error);
            jset_int(start_pos, "character", (int64_t)start_col, a, error);
            jset_int(end_pos, "line", (int64_t)end_line, a, error);
            jset_int(end_pos, "character", (int64_t)end_col, a, error);

            jset_obj(range, "start", start_pos, error);
            jset_obj(range, "end", end_pos, error);
            jset_obj(loc, "range", range, error);

            basl_json_array_push(result, loc, error);
        }
        free(refs);
    }

    return lsp_make_response(a, id, result, out, error);
}

static basl_status_t handle_formatting(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    const basl_json_value_t *params,
    basl_json_value_t **out,
    basl_error_t *error
) {
    const basl_allocator_t *a = &server->allocator;
    const basl_json_value_t *text_doc;
    const basl_json_value_t *uri_val;
    const basl_source_file_t *src;
    basl_json_value_t *result = NULL;
    basl_token_list_t tokens = {0};
    char *formatted = NULL;
    size_t formatted_len = 0;
    basl_source_id_t source_id;

    text_doc = basl_json_object_get(params, "textDocument");
    if (text_doc == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = basl_json_object_get(text_doc, "uri");
    if (uri_val == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, basl_json_string_value(uri_val),
                                   basl_json_string_length(uri_val));
    if (source_id == 0) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = basl_source_registry_get(&server->sources, source_id);

    /* Lex and format */
    basl_token_list_init(&tokens, server->runtime);
    if (basl_lex_source(&server->sources, source_id, &tokens, NULL, error) != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        return lsp_make_response(a, id, NULL, out, error);
    }

    if (basl_fmt(basl_string_c_str(&src->text), basl_string_length(&src->text),
                 &tokens, &formatted, &formatted_len, error) != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        return lsp_make_response(a, id, NULL, out, error);
    }

    basl_token_list_free(&tokens);

    /* Build single edit replacing entire document */
    {
        basl_json_value_t *edit = NULL;
        basl_json_value_t *range = NULL;
        basl_json_value_t *start_pos = NULL;
        basl_json_value_t *end_pos = NULL;
        basl_json_value_t *new_text = NULL;
        size_t end_line, end_col;

        offset_to_line_col(basl_string_c_str(&src->text), basl_string_length(&src->text),
                           basl_string_length(&src->text), &end_line, &end_col);

        basl_json_array_new(a, &result, error);
        basl_json_object_new(a, &edit, error);
        basl_json_object_new(a, &range, error);
        basl_json_object_new(a, &start_pos, error);
        basl_json_object_new(a, &end_pos, error);
        basl_json_string_new(a, formatted, formatted_len, &new_text, error);

        jset_int(start_pos, "line", 0, a, error);
        jset_int(start_pos, "character", 0, a, error);
        jset_int(end_pos, "line", (int64_t)end_line, a, error);
        jset_int(end_pos, "character", (int64_t)end_col, a, error);

        jset_obj(range, "start", start_pos, error);
        jset_obj(range, "end", end_pos, error);
        jset_obj(edit, "range", range, error);
        jset_obj(edit, "newText", new_text, error);

        basl_json_array_push(result, edit, error);
    }

    free(formatted);
    return lsp_make_response(a, id, result, out, error);
}

static basl_status_t handle_signature_help(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    const basl_json_value_t *params,
    basl_json_value_t **out,
    basl_error_t *error
) {
    const basl_allocator_t *a = &server->allocator;
    const basl_json_value_t *text_doc;
    const basl_json_value_t *position;
    const basl_json_value_t *uri_val;
    const basl_json_value_t *line_val;
    const basl_json_value_t *char_val;
    const basl_source_file_t *src;
    const basl_semantic_file_t *sem_file;
    const basl_semantic_node_t *node;
    basl_source_id_t source_id;
    size_t line, col, offset;

    text_doc = basl_json_object_get(params, "textDocument");
    position = basl_json_object_get(params, "position");
    if (text_doc == NULL || position == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = basl_json_object_get(text_doc, "uri");
    line_val = basl_json_object_get(position, "line");
    char_val = basl_json_object_get(position, "character");
    if (uri_val == NULL || line_val == NULL || char_val == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, basl_json_string_value(uri_val),
                                   basl_json_string_length(uri_val));
    if (source_id == 0) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    sem_file = find_semantic_file(server, source_id);
    if (sem_file == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = basl_source_registry_get(&server->sources, source_id);
    line = (size_t)basl_json_number_value(line_val);
    col = (size_t)basl_json_number_value(char_val);
    offset = line_col_to_offset(basl_string_c_str(&src->text), basl_string_length(&src->text), line, col);

    /* Find call expression containing this position */
    node = basl_semantic_file_node_at(sem_file, offset);
    while (node != NULL && node->kind != BASL_NODE_CALL_EXPR && node->kind != BASL_NODE_METHOD_CALL_EXPR) {
        /* Walk up - for now just check nodes at position */
        break;
    }

    if (node != NULL && (node->kind == BASL_NODE_CALL_EXPR || node->kind == BASL_NODE_METHOD_CALL_EXPR)) {
        basl_json_value_t *result = NULL;
        basl_json_value_t *signatures = NULL;
        basl_json_value_t *sig = NULL;
        basl_json_value_t *label = NULL;
        const char *func_name = NULL;
        size_t func_name_len = 0;

        if (node->kind == BASL_NODE_METHOD_CALL_EXPR) {
            func_name = node->data.method_call.method_name;
            func_name_len = node->data.method_call.method_name_length;
        } else if (node->data.call.callee != NULL && 
                   node->data.call.callee->kind == BASL_NODE_IDENTIFIER_EXPR) {
            func_name = node->data.call.callee->data.identifier.name;
            func_name_len = node->data.call.callee->data.identifier.name_length;
        }

        if (func_name != NULL) {
            char sig_label[256];
            int len = snprintf(sig_label, sizeof(sig_label), "%.*s(...)", (int)func_name_len, func_name);

            basl_json_object_new(a, &result, error);
            basl_json_array_new(a, &signatures, error);
            basl_json_object_new(a, &sig, error);
            basl_json_string_new(a, sig_label, (size_t)len, &label, error);

            jset_obj(sig, "label", label, error);
            basl_json_array_push(signatures, sig, error);

            jset_obj(result, "signatures", signatures, error);
            jset_int(result, "activeSignature", 0, a, error);
            jset_int(result, "activeParameter", 0, a, error);

            return lsp_make_response(a, id, result, out, error);
        }
    }

    return lsp_make_response(a, id, NULL, out, error);
}

static basl_status_t handle_rename(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    const basl_json_value_t *params,
    basl_json_value_t **out,
    basl_error_t *error
) {
    const basl_allocator_t *a = &server->allocator;
    const basl_json_value_t *text_doc;
    const basl_json_value_t *position;
    const basl_json_value_t *new_name_val;
    const basl_json_value_t *uri_val;
    const basl_json_value_t *line_val;
    const basl_json_value_t *char_val;
    const basl_source_file_t *src;
    basl_semantic_reference_t *refs = NULL;
    size_t ref_count = 0;
    basl_json_value_t *result = NULL;
    basl_json_value_t *changes = NULL;
    const char *new_name;
    size_t new_name_len;
    basl_source_id_t source_id;
    size_t line, col, offset, i;

    text_doc = basl_json_object_get(params, "textDocument");
    position = basl_json_object_get(params, "position");
    new_name_val = basl_json_object_get(params, "newName");
    if (text_doc == NULL || position == NULL || new_name_val == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = basl_json_object_get(text_doc, "uri");
    line_val = basl_json_object_get(position, "line");
    char_val = basl_json_object_get(position, "character");
    new_name = basl_json_string_value(new_name_val);
    new_name_len = basl_json_string_length(new_name_val);

    if (uri_val == NULL || line_val == NULL || char_val == NULL || new_name == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, basl_json_string_value(uri_val),
                                   basl_json_string_length(uri_val));
    if (source_id == 0) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = basl_source_registry_get(&server->sources, source_id);
    line = (size_t)basl_json_number_value(line_val);
    col = (size_t)basl_json_number_value(char_val);
    offset = line_col_to_offset(basl_string_c_str(&src->text), basl_string_length(&src->text), line, col);

    basl_json_object_new(a, &result, error);
    basl_json_object_new(a, &changes, error);

    if (basl_semantic_index_references_at(server->index, source_id, offset, &refs, &ref_count, error) == BASL_STATUS_OK && refs != NULL) {
        for (i = 0; i < ref_count; i++) {
            const basl_source_file_t *ref_src;
            basl_json_value_t *edits_array = NULL;
            basl_json_value_t *edit = NULL;
            basl_json_value_t *range = NULL;
            basl_json_value_t *start_pos = NULL;
            basl_json_value_t *end_pos = NULL;
            basl_json_value_t *new_text = NULL;
            size_t start_line, start_col, end_line, end_col;
            const char *ref_uri;
            size_t ref_uri_len;

            ref_src = basl_source_registry_get(&server->sources, refs[i].span.source_id);
            if (ref_src == NULL) continue;

            ref_uri = basl_string_c_str(&ref_src->path);
            ref_uri_len = basl_string_length(&ref_src->path);

            /* Get or create edits array for this URI */
            {
                const basl_json_value_t *existing = basl_json_object_get(changes, ref_uri);
                if (existing == NULL) {
                    basl_json_array_new(a, &edits_array, error);
                    basl_json_object_set(changes, ref_uri, ref_uri_len, edits_array, error);
                } else {
                    edits_array = (basl_json_value_t *)existing;  /* Safe: we own changes */
                }
            }

            offset_to_line_col(basl_string_c_str(&ref_src->text), basl_string_length(&ref_src->text),
                               refs[i].span.start_offset, &start_line, &start_col);
            offset_to_line_col(basl_string_c_str(&ref_src->text), basl_string_length(&ref_src->text),
                               refs[i].span.end_offset, &end_line, &end_col);

            basl_json_object_new(a, &edit, error);
            basl_json_object_new(a, &range, error);
            basl_json_object_new(a, &start_pos, error);
            basl_json_object_new(a, &end_pos, error);
            basl_json_string_new(a, new_name, new_name_len, &new_text, error);

            jset_int(start_pos, "line", (int64_t)start_line, a, error);
            jset_int(start_pos, "character", (int64_t)start_col, a, error);
            jset_int(end_pos, "line", (int64_t)end_line, a, error);
            jset_int(end_pos, "character", (int64_t)end_col, a, error);

            jset_obj(range, "start", start_pos, error);
            jset_obj(range, "end", end_pos, error);
            jset_obj(edit, "range", range, error);
            jset_obj(edit, "newText", new_text, error);

            basl_json_array_push(edits_array, edit, error);
        }
        free(refs);
    }

    jset_obj(result, "changes", changes, error);
    return lsp_make_response(a, id, result, out, error);
}

static basl_status_t handle_completion(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    const basl_json_value_t *params,
    basl_json_value_t **out,
    basl_error_t *error
) {
    const basl_allocator_t *a = &server->allocator;
    basl_json_value_t *result = NULL;
    size_t i, count;

    (void)params;  /* Position not used yet - return all symbols */

    basl_json_array_new(a, &result, error);

    count = basl_debug_symbol_table_count(&server->index->symbols);
    for (i = 0; i < count; i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&server->index->symbols, i);
        basl_json_value_t *item = NULL;
        basl_json_value_t *label = NULL;
        int kind;

        /* Map to LSP CompletionItemKind */
        switch (sym->kind) {
            case BASL_DEBUG_SYMBOL_FUNCTION: kind = 3; break;  /* Function */
            case BASL_DEBUG_SYMBOL_CLASS: kind = 7; break;     /* Class */
            case BASL_DEBUG_SYMBOL_INTERFACE: kind = 8; break; /* Interface */
            case BASL_DEBUG_SYMBOL_ENUM: kind = 13; break;     /* Enum */
            case BASL_DEBUG_SYMBOL_FIELD: kind = 5; break;     /* Field */
            case BASL_DEBUG_SYMBOL_METHOD: kind = 2; break;    /* Method */
            case BASL_DEBUG_SYMBOL_GLOBAL_CONST: kind = 21; break; /* Constant */
            case BASL_DEBUG_SYMBOL_GLOBAL_VAR: kind = 6; break;    /* Variable */
            default: kind = 1; break;  /* Text */
        }

        basl_json_object_new(a, &item, error);
        basl_json_string_new(a, sym->name, sym->name_length, &label, error);
        jset_obj(item, "label", label, error);
        jset_int(item, "kind", kind, a, error);

        basl_json_array_push(result, item, error);
    }

    return lsp_make_response(a, id, result, out, error);
}

static basl_status_t handle_definition(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    const basl_json_value_t *params,
    basl_json_value_t **out,
    basl_error_t *error
) {
    const basl_allocator_t *a = &server->allocator;
    const basl_json_value_t *text_doc;
    const basl_json_value_t *position;
    const basl_json_value_t *uri_val;
    const basl_json_value_t *line_val;
    const basl_json_value_t *char_val;
    const basl_source_file_t *src;
    basl_source_span_t def_span;
    basl_source_id_t source_id;
    size_t line, col, offset;

    text_doc = basl_json_object_get(params, "textDocument");
    position = basl_json_object_get(params, "position");
    if (text_doc == NULL || position == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = basl_json_object_get(text_doc, "uri");
    line_val = basl_json_object_get(position, "line");
    char_val = basl_json_object_get(position, "character");
    if (uri_val == NULL || line_val == NULL || char_val == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, basl_json_string_value(uri_val),
                                   basl_json_string_length(uri_val));
    if (source_id == 0) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = basl_source_registry_get(&server->sources, source_id);
    line = (size_t)basl_json_number_value(line_val);
    col = (size_t)basl_json_number_value(char_val);
    offset = line_col_to_offset(basl_string_c_str(&src->text), basl_string_length(&src->text), line, col);

    if (basl_semantic_index_definition_at(server->index, source_id, offset, &def_span, error) == BASL_STATUS_OK) {
        basl_json_value_t *result = NULL;
        basl_json_value_t *range = NULL;
        basl_json_value_t *start_pos = NULL;
        basl_json_value_t *end_pos = NULL;
        const basl_source_file_t *def_src;
        size_t start_line, start_col, end_line, end_col;

        def_src = basl_source_registry_get(&server->sources, def_span.source_id);
        if (def_src == NULL) {
            return lsp_make_response(a, id, NULL, out, error);
        }

        offset_to_line_col(basl_string_c_str(&def_src->text), basl_string_length(&def_src->text),
                           def_span.start_offset, &start_line, &start_col);
        offset_to_line_col(basl_string_c_str(&def_src->text), basl_string_length(&def_src->text),
                           def_span.end_offset, &end_line, &end_col);

        basl_json_object_new(a, &result, error);
        basl_json_object_new(a, &range, error);
        basl_json_object_new(a, &start_pos, error);
        basl_json_object_new(a, &end_pos, error);

        {
            basl_json_value_t *uri_out = NULL;
            basl_json_string_new(a, basl_string_c_str(&def_src->path), basl_string_length(&def_src->path), &uri_out, error);
            jset_obj(result, "uri", uri_out, error);
        }

        jset_int(start_pos, "line", (int64_t)start_line, a, error);
        jset_int(start_pos, "character", (int64_t)start_col, a, error);
        jset_int(end_pos, "line", (int64_t)end_line, a, error);
        jset_int(end_pos, "character", (int64_t)end_col, a, error);

        jset_obj(range, "start", start_pos, error);
        jset_obj(range, "end", end_pos, error);
        jset_obj(result, "range", range, error);

        return lsp_make_response(a, id, result, out, error);
    }

    return lsp_make_response(a, id, NULL, out, error);
}

static basl_status_t handle_hover(
    basl_lsp_server_t *server,
    const basl_json_value_t *id,
    const basl_json_value_t *params,
    basl_json_value_t **out,
    basl_error_t *error
) {
    const basl_allocator_t *a = &server->allocator;
    const basl_json_value_t *text_doc;
    const basl_json_value_t *position;
    const basl_json_value_t *uri_val;
    const basl_json_value_t *line_val;
    const basl_json_value_t *char_val;
    const basl_source_file_t *src;
    const basl_semantic_file_t *sem_file;
    basl_semantic_type_t type;
    basl_source_id_t source_id;
    char *type_str;
    size_t line, col, offset;

    text_doc = basl_json_object_get(params, "textDocument");
    position = basl_json_object_get(params, "position");
    if (text_doc == NULL || position == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    uri_val = basl_json_object_get(text_doc, "uri");
    line_val = basl_json_object_get(position, "line");
    char_val = basl_json_object_get(position, "character");
    if (uri_val == NULL || line_val == NULL || char_val == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    source_id = find_source_by_uri(server, basl_json_string_value(uri_val),
                                   basl_json_string_length(uri_val));
    if (source_id == 0) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    sem_file = find_semantic_file(server, source_id);
    if (sem_file == NULL) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    src = basl_source_registry_get(&server->sources, source_id);
    line = (size_t)basl_json_number_value(line_val);
    col = (size_t)basl_json_number_value(char_val);
    offset = line_col_to_offset(basl_string_c_str(&src->text), basl_string_length(&src->text), line, col);

    type = basl_semantic_file_type_at(sem_file, offset);
    if (!basl_semantic_type_is_valid(type)) {
        return lsp_make_response(a, id, NULL, out, error);
    }

    type_str = basl_semantic_type_to_string(server->index, type);
    if (type_str != NULL) {
        basl_json_value_t *result = NULL;
        basl_json_value_t *contents = NULL;

        basl_json_object_new(a, &result, error);
        basl_json_object_new(a, &contents, error);

        jset_str(contents, "kind", "plaintext", a, error);
        jset_str(contents, "value", type_str, a, error);
        jset_obj(result, "contents", contents, error);

        free(type_str);
        return lsp_make_response(a, id, result, out, error);
    }

    return lsp_make_response(a, id, NULL, out, error);
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
    } else if (method_len == 22 && strncmp(method, "textDocument/didChange", 22) == 0) {
        handle_did_change(server, params);
    } else if (method_len == 21 && strncmp(method, "textDocument/didClose", 21) == 0) {
        handle_did_close(server, params);
    } else if (method_len == 27 && strncmp(method, "textDocument/documentSymbol", 27) == 0) {
        status = handle_document_symbol(server, id, params, &response, error);
    } else if (method_len == 18 && strncmp(method, "textDocument/hover", 18) == 0) {
        status = handle_hover(server, id, params, &response, error);
    } else if (method_len == 23 && strncmp(method, "textDocument/definition", 23) == 0) {
        status = handle_definition(server, id, params, &response, error);
    } else if (method_len == 23 && strncmp(method, "textDocument/completion", 23) == 0) {
        status = handle_completion(server, id, params, &response, error);
    } else if (method_len == 23 && strncmp(method, "textDocument/references", 23) == 0) {
        status = handle_references(server, id, params, &response, error);
    } else if (method_len == 19 && strncmp(method, "textDocument/rename", 19) == 0) {
        status = handle_rename(server, id, params, &response, error);
    } else if (method_len == 23 && strncmp(method, "textDocument/formatting", 23) == 0) {
        status = handle_formatting(server, id, params, &response, error);
    } else if (method_len == 26 && strncmp(method, "textDocument/signatureHelp", 26) == 0) {
        status = handle_signature_help(server, id, params, &response, error);
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

basl_status_t basl_lsp_server_process_one(
    basl_lsp_server_t *server,
    basl_error_t *error
) {
    basl_status_t status;
    basl_json_value_t *message = NULL;

    if (server == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_jsonrpc_read(&server->transport, &message, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = lsp_handle_message(server, message, error);
    basl_json_free(&message);
    return status;
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
