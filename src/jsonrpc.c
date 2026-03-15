#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/jsonrpc.h"
#include "internal/basl_internal.h"

void basl_jsonrpc_transport_init(
    basl_jsonrpc_transport_t *transport,
    FILE *in,
    FILE *out,
    const basl_allocator_t *allocator
) {
    if (transport == NULL) return;
    transport->in = in;
    transport->out = out;
    transport->allocator = allocator;
}

/* ── Read ────────────────────────────────────────────────────────── */

static int read_header_line(FILE *f, char *buf, size_t cap) {
    size_t i = 0;
    for (;;) {
        int c = fgetc(f);
        if (c == EOF) return -1;
        if (i + 1 >= cap) return -1;
        buf[i++] = (char)c;
        if (i >= 2 && buf[i - 2] == '\r' && buf[i - 1] == '\n') {
            buf[i] = '\0';
            return (int)i;
        }
    }
}

basl_status_t basl_jsonrpc_read(
    basl_jsonrpc_transport_t *transport,
    basl_json_value_t **out,
    basl_error_t *error
) {
    if (transport == NULL || transport->in == NULL || out == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "jsonrpc: invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Read headers until empty line. */
    size_t content_length = 0;
    int found_length = 0;
    char line[256];

    for (;;) {
        int n = read_header_line(transport->in, line, sizeof(line));
        if (n < 0) {
            basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                                   "jsonrpc: failed to read header");
            return BASL_STATUS_INTERNAL;
        }
        /* Empty line (just \r\n) terminates headers. */
        if (n == 2 && line[0] == '\r' && line[1] == '\n') break;

        /* Parse Content-Length. */
        const char *prefix = "Content-Length: ";
        size_t prefix_len = 16;
        if ((size_t)n > prefix_len &&
            memcmp(line, prefix, prefix_len) == 0) {
            content_length = (size_t)strtoul(line + prefix_len, NULL, 10);
            found_length = 1;
        }
    }

    if (!found_length || content_length == 0) {
        basl_error_set_literal(error, BASL_STATUS_SYNTAX_ERROR,
                               "jsonrpc: missing Content-Length");
        return BASL_STATUS_SYNTAX_ERROR;
    }

    /* Read body. */
    basl_allocator_t a = transport->allocator != NULL &&
                         basl_allocator_is_valid(transport->allocator)
                             ? *transport->allocator
                             : basl_default_allocator();

    char *body = (char *)a.allocate(a.user_data, content_length + 1);
    if (body == NULL) {
        basl_error_set_literal(error, BASL_STATUS_OUT_OF_MEMORY,
                               "jsonrpc: allocation failed");
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    size_t total = 0;
    while (total < content_length) {
        size_t n = fread(body + total, 1, content_length - total, transport->in);
        if (n == 0) {
            a.deallocate(a.user_data, body);
            basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                                   "jsonrpc: unexpected EOF reading body");
            return BASL_STATUS_INTERNAL;
        }
        total += n;
    }
    body[content_length] = '\0';

    basl_status_t s = basl_json_parse(
        transport->allocator, body, content_length, out, error);
    a.deallocate(a.user_data, body);
    return s;
}

/* ── Write ───────────────────────────────────────────────────────── */

basl_status_t basl_jsonrpc_write(
    basl_jsonrpc_transport_t *transport,
    const basl_json_value_t *message,
    basl_error_t *error
) {
    if (transport == NULL || transport->out == NULL || message == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "jsonrpc: invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    char *json_str = NULL;
    size_t json_len = 0;
    basl_status_t s = basl_json_emit(message, &json_str, &json_len, error);
    if (s != BASL_STATUS_OK) return s;

    /* Determine which allocator was used for the emitted string. */
    basl_allocator_t a = transport->allocator != NULL &&
                         basl_allocator_is_valid(transport->allocator)
                             ? *transport->allocator
                             : basl_default_allocator();

    fprintf(transport->out, "Content-Length: %zu\r\n\r\n", json_len);
    size_t written = fwrite(json_str, 1, json_len, transport->out);
    fflush(transport->out);

    a.deallocate(a.user_data, json_str);

    if (written != json_len) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "jsonrpc: write failed");
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}
