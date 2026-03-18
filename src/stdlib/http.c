/* BASL standard library: http module.
 *
 * HTTP/S client using OS-native libraries loaded at runtime:
 *   - WinHTTP via LoadLibrary() on Windows
 *   - libcurl via dlopen() on POSIX
 * Fallback to plain TCP socket-based HTTP/1.1 client and server (no TLS).
 *
 * All platform operations go through platform.h — no OS headers here.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"
#include "platform/platform.h"

/* ── Test visibility ─────────────────────────────────────────────── */

#ifdef BASL_HTTP_TESTING
#define HTTP_STATIC BASL_API
#else
#define HTTP_STATIC static
#endif

/* ── VM helpers ──────────────────────────────────────────────────── */

static basl_status_t push_string(basl_vm_t *vm, const char *s, size_t len,
                                  basl_error_t *error) {
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_object_t *obj = NULL;
    basl_status_t st = basl_string_object_new(rt, s, len, &obj, error);
    if (st != BASL_STATUS_OK) return st;
    basl_value_t val;
    basl_value_init_object(&val, &obj);
    st = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return st;
}

static basl_status_t push_i64(basl_vm_t *vm, int64_t v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_int(v);
    return basl_vm_stack_push(vm, &val, error);
}

static int get_string_arg(basl_vm_t *vm, size_t base, size_t idx,
                          const char **out, size_t *out_len) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    const basl_object_t *obj = (const basl_object_t *)basl_nanbox_decode_ptr(v);
    if (!obj || basl_object_type(obj) != BASL_OBJECT_STRING) return 0;
    *out = basl_string_object_c_str(obj);
    *out_len = basl_string_object_length(obj);
    return 1;
}

static int64_t get_i64_arg(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    if (basl_nanbox_is_int(v)) return basl_nanbox_decode_int(v);
    return 0;
}

/* ── URL parsing ─────────────────────────────────────────────────── */

typedef struct {
    char scheme[16];
    char host[256];
    int port;
    char path[2048];
} parsed_url_t;

HTTP_STATIC int parse_url(const char *url, parsed_url_t *out) {
    memset(out, 0, sizeof(*out));
    memcpy(out->path, "/", 2);

    const char *p = url;
    const char *scheme_end = strstr(p, "://");
    if (scheme_end) {
        size_t slen = (size_t)(scheme_end - p);
        if (slen >= sizeof(out->scheme)) return 0;
        memcpy(out->scheme, p, slen);
        p = scheme_end + 3;
    } else {
        memcpy(out->scheme, "http", 5);
    }

    const char *path_start = strchr(p, '/');
    const char *port_start = strchr(p, ':');
    const char *host_end = path_start ? path_start : p + strlen(p);
    if (port_start && port_start < host_end) host_end = port_start;

    size_t hlen = (size_t)(host_end - p);
    if (hlen >= sizeof(out->host)) return 0;
    memcpy(out->host, p, hlen);

    if (port_start && port_start < (path_start ? path_start : p + strlen(p))) {
        out->port = atoi(port_start + 1);
    } else {
        out->port = (strcmp(out->scheme, "https") == 0) ? 443 : 80;
    }

    if (path_start) {
        size_t plen = strlen(path_start);
        if (plen >= sizeof(out->path)) plen = sizeof(out->path) - 1;
        memcpy(out->path, path_start, plen);
        out->path[plen] = '\0';
    }
    return 1;
}

/* ── Response structure ──────────────────────────────────────────── */

typedef struct {
    int status_code;
    char *headers;
    char *body;
    size_t body_len;
} http_response_t;

HTTP_STATIC void response_free(http_response_t *r) {
    free(r->headers);
    free(r->body);
    memset(r, 0, sizeof(*r));
}

/* ── Socket-based HTTP/1.1 fallback (no TLS) ─────────────────────── */

HTTP_STATIC int socket_request(const char *method, parsed_url_t *url,
                          const char *headers, const char *body, size_t body_len,
                          http_response_t *resp) {
    memset(resp, 0, sizeof(*resp));
    basl_platform_net_init(NULL);

    basl_socket_t sock = BASL_INVALID_SOCKET;
    if (basl_platform_tcp_connect(url->host, url->port, &sock, NULL) != BASL_STATUS_OK)
        return -1;

    char req_buf[4096];
    int req_len = snprintf(req_buf, sizeof(req_buf),
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "%s"
        "Content-Length: %zu\r\n"
        "\r\n",
        method, url->path, url->host,
        headers ? headers : "",
        body_len);

    basl_platform_tcp_send(sock, req_buf, (size_t)req_len, NULL, NULL);
    if (body && body_len > 0)
        basl_platform_tcp_send(sock, body, body_len, NULL, NULL);

    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { basl_platform_tcp_close(sock, NULL); return -1; }

    for (;;) {
        size_t n = 0;
        basl_status_t st = basl_platform_tcp_recv(sock, buf + len, cap - len - 1, &n, NULL);
        if (st != BASL_STATUS_OK || n == 0) break;
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); basl_platform_tcp_close(sock, NULL); return -1; }
            buf = nb;
        }
    }
    buf[len] = '\0';
    basl_platform_tcp_close(sock, NULL);

    char *header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) { free(buf); return -1; }

    if (strncmp(buf, "HTTP/", 5) == 0) {
        const char *sp = strchr(buf, ' ');
        if (sp) resp->status_code = atoi(sp + 1);
    }

    size_t hdr_len = (size_t)(header_end - buf);
    resp->headers = (char *)malloc(hdr_len + 1);
    if (resp->headers) { memcpy(resp->headers, buf, hdr_len); resp->headers[hdr_len] = '\0'; }

    char *body_start = header_end + 4;
    resp->body_len = len - (size_t)(body_start - buf);
    resp->body = (char *)malloc(resp->body_len + 1);
    if (resp->body) { memcpy(resp->body, body_start, resp->body_len); resp->body[resp->body_len] = '\0'; }

    free(buf);
    return 0;
}

/* ── Constants ───────────────────────────────────────────────────── */

#define HTTP_MAX_REDIRECTS 10

/* ── Unified client request ──────────────────────────────────────── */

HTTP_STATIC int do_request_once(const char *method, const char *url_str,
                      const char *headers, const char *body, size_t body_len,
                      http_response_t *resp) {
    memset(resp, 0, sizeof(*resp));

    /* Try native HTTP library first (supports HTTPS). */
    basl_http_response_t native_resp;
    basl_status_t st = basl_platform_http_request(
        method, url_str, headers, body, body_len, &native_resp, NULL);

    if (st == BASL_STATUS_OK) {
        resp->status_code = native_resp.status_code;
        resp->headers = native_resp.headers;
        resp->body = native_resp.body;
        resp->body_len = native_resp.body_len;
        return 0;
    }

    /* Native lib unavailable — fall back to sockets (HTTP only). */
    parsed_url_t url;
    if (!parse_url(url_str, &url)) return -1;

    if (strcmp(url.scheme, "https") == 0) {
        /* No TLS without native library. */
        return -1;
    }
    return socket_request(method, &url, headers, body, body_len, resp);
}

/* Extract Location header from response headers string. */
static const char *find_location_header(const char *hdrs, char *buf, size_t buf_sz) {
    if (!hdrs) return NULL;
    const char *p = hdrs;
    while (*p) {
        const char *eol = strstr(p, "\r\n");
        if (!eol) eol = p + strlen(p);
        if ((eol - p > 10) &&
            (p[0] == 'L' || p[0] == 'l') &&
            (p[1] == 'o' || p[1] == 'O') &&
            (p[8] == ':' || p[9] == ':')) {
            const char *colon = strchr(p, ':');
            if (colon && colon < eol) {
                const char *val = colon + 1;
                while (val < eol && *val == ' ') val++;
                size_t vlen = (size_t)(eol - val);
                if (vlen < buf_sz) {
                    memcpy(buf, val, vlen);
                    buf[vlen] = '\0';
                    return buf;
                }
            }
        }
        if (*eol == '\0') break;
        p = eol + 2;
    }
    return NULL;
}

HTTP_STATIC int do_request(const char *method, const char *url_str,
                      const char *headers, const char *body, size_t body_len,
                      http_response_t *resp) {
    char url_buf[4096];
    size_t ulen = strlen(url_str);
    if (ulen >= sizeof(url_buf)) ulen = sizeof(url_buf) - 1;
    memcpy(url_buf, url_str, ulen); url_buf[ulen] = '\0';

    for (int redirects = 0; redirects <= HTTP_MAX_REDIRECTS; redirects++) {
        int rc = do_request_once(method, url_buf, headers, body, body_len, resp);
        if (rc != 0) return rc;

        if (resp->status_code >= 301 && resp->status_code <= 308 &&
            resp->status_code != 304 && resp->status_code != 305) {
            char loc[4096];
            if (find_location_header(resp->headers, loc, sizeof(loc))) {
                int code = resp->status_code;
                response_free(resp);
                ulen = strlen(loc);
                if (ulen >= sizeof(url_buf)) break;
                memcpy(url_buf, loc, ulen + 1);
                /* 301/302/303 change method to GET */
                if (code == 301 || code == 302 || code == 303) {
                    method = "GET"; body = NULL; body_len = 0;
                }
                continue;
            }
        }
        return 0;
    }
    return 0; /* max redirects reached, return last response */
}

/* ── HTTP server via platform TCP sockets ────────────────────────── */

#define HTTP_MAX_SERVERS 64
#define HTTP_MAX_CLIENTS 256
#define HTTP_MAX_ROUTES 128

typedef struct {
    char *pattern;
    basl_object_t *handler; /* zero-arity function */
} http_route_t;

typedef struct {
    basl_socket_t listener;
    int in_use;
    http_route_t routes[HTTP_MAX_ROUTES];
    int route_count;
    int read_timeout_ms;
    int write_timeout_ms;
    int idle_timeout_ms;
} http_server_t;

typedef struct {
    basl_socket_t sock;
    int in_use;
    char *method;
    char *path;
    char *headers;
    char *body;
    size_t body_len;
} http_conn_t;

static http_server_t g_servers[HTTP_MAX_SERVERS];
static http_conn_t g_clients[HTTP_MAX_CLIENTS];
static int g_inited = 0;

static void http_tables_init(void) {
    if (g_inited) return;
    basl_platform_net_init(NULL);
    for (int i = 0; i < HTTP_MAX_SERVERS; i++) {
        g_servers[i].listener = BASL_INVALID_SOCKET;
        g_servers[i].in_use = 0;
    }
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        g_clients[i].sock = BASL_INVALID_SOCKET;
        g_clients[i].in_use = 0;
    }
    g_inited = 1;
}

static int64_t alloc_server(basl_socket_t s) {
    for (int i = 0; i < HTTP_MAX_SERVERS; i++) {
        if (!g_servers[i].in_use) {
            g_servers[i].listener = s; g_servers[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static void client_free_fields(http_conn_t *c) {
    free(c->method);  c->method = NULL;
    free(c->path);    c->path = NULL;
    free(c->headers);  c->headers = NULL;
    free(c->body);    c->body = NULL;
    c->body_len = 0;
}

static int64_t alloc_client(basl_socket_t s) {
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        if (!g_clients[i].in_use) {
            memset(&g_clients[i], 0, sizeof(g_clients[i]));
            g_clients[i].sock = s; g_clients[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static http_conn_t *get_client(int64_t h) {
    if (h < 0 || h >= HTTP_MAX_CLIENTS || !g_clients[h].in_use) return NULL;
    return &g_clients[h];
}

/* Parse an incoming HTTP request from a connected socket. */
static int parse_incoming_request(http_conn_t *conn) {
    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return -1;

    for (;;) {
        size_t n = 0;
        basl_status_t st = basl_platform_tcp_recv(conn->sock, buf + len, cap - len - 1, &n, NULL);
        if (st != BASL_STATUS_OK || n == 0) { free(buf); return -1; }
        len += n;
        buf[len] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return -1; }
            buf = nb;
        }
    }

    char *line_end = strstr(buf, "\r\n");
    if (!line_end) { free(buf); return -1; }

    char *sp1 = strchr(buf, ' ');
    if (!sp1 || sp1 > line_end) { free(buf); return -1; }
    conn->method = (char *)malloc((size_t)(sp1 - buf) + 1);
    memcpy(conn->method, buf, (size_t)(sp1 - buf));
    conn->method[sp1 - buf] = '\0';

    char *path_start = sp1 + 1;
    char *sp2 = strchr(path_start, ' ');
    if (!sp2 || sp2 > line_end) { free(buf); return -1; }
    conn->path = (char *)malloc((size_t)(sp2 - path_start) + 1);
    memcpy(conn->path, path_start, (size_t)(sp2 - path_start));
    conn->path[sp2 - path_start] = '\0';

    char *hdr_start = line_end + 2;
    char *hdr_end = strstr(buf, "\r\n\r\n");
    size_t hdr_len = (size_t)(hdr_end - hdr_start);
    conn->headers = (char *)malloc(hdr_len + 1);
    memcpy(conn->headers, hdr_start, hdr_len);
    conn->headers[hdr_len] = '\0';

    char *bstart = hdr_end + 4;
    size_t already = len - (size_t)(bstart - buf);

    size_t content_length = 0;
    const char *cl = strstr(conn->headers, "Content-Length:");
    if (!cl) cl = strstr(conn->headers, "content-length:");
    if (cl) content_length = (size_t)atol(cl + 15);

    if (content_length > already) {
        char *full = (char *)malloc(content_length + 1);
        if (already > 0) memcpy(full, bstart, already);
        size_t got = already;
        while (got < content_length) {
            size_t n = 0;
            if (basl_platform_tcp_recv(conn->sock, full + got, content_length - got, &n, NULL) != BASL_STATUS_OK || n == 0) break;
            got += n;
        }
        full[got] = '\0';
        conn->body = full;
        conn->body_len = got;
    } else {
        conn->body = (char *)malloc(already + 1);
        if (already > 0) memcpy(conn->body, bstart, already);
        conn->body[already] = '\0';
        conn->body_len = already;
    }

    free(buf);
    return 0;
}

/* ── Server BASL functions ───────────────────────────────────────── */

static basl_status_t http_listen(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *host; size_t host_len;
    if (!get_string_arg(vm, base, 0, &host, &host_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    int64_t port = get_i64_arg(vm, base, 1);
    char host_buf[256];
    if (host_len >= sizeof(host_buf)) host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len); host_buf[host_len] = '\0';
    basl_vm_stack_pop_n(vm, arg_count);

    http_tables_init();
    basl_socket_t sock = BASL_INVALID_SOCKET;
    if (basl_platform_tcp_listen(host_buf, (int)port, &sock, error) != BASL_STATUS_OK)
        return push_i64(vm, -1, error);

    int64_t handle = alloc_server(sock);
    if (handle < 0) { basl_platform_tcp_close(sock, NULL); }
    return push_i64(vm, handle, error);
}

static basl_status_t http_accept(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t srv = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);

    if (srv < 0 || srv >= HTTP_MAX_SERVERS || !g_servers[srv].in_use)
        return push_i64(vm, -1, error);

    basl_socket_t client = BASL_INVALID_SOCKET;
    if (basl_platform_tcp_accept(g_servers[srv].listener, &client, NULL) != BASL_STATUS_OK)
        return push_i64(vm, -1, error);

    int64_t ch = alloc_client(client);
    if (ch < 0) { basl_platform_tcp_close(client, NULL); return push_i64(vm, -1, error); }

    http_conn_t *conn = get_client(ch);
    if (parse_incoming_request(conn) != 0) {
        client_free_fields(conn);
        basl_platform_tcp_close(conn->sock, NULL);
        conn->sock = BASL_INVALID_SOCKET; conn->in_use = 0;
        return push_i64(vm, -1, error);
    }
    return push_i64(vm, ch, error);
}

static basl_status_t http_req_method(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *m = (c && c->method) ? c->method : "";
    return push_string(vm, m, strlen(m), error);
}

static basl_status_t http_req_path(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *p = (c && c->path) ? c->path : "";
    return push_string(vm, p, strlen(p), error);
}

static basl_status_t http_req_body(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *b = (c && c->body) ? c->body : "";
    return push_string(vm, b, c ? c->body_len : 0, error);
}

static basl_status_t http_req_headers(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *h = (c && c->headers) ? c->headers : "";
    return push_string(vm, h, strlen(h), error);
}

static basl_status_t http_req_header(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    const char *name; size_t name_len;
    get_string_arg(vm, base, 1, &name, &name_len);
    char nbuf[256];
    if (name_len >= sizeof(nbuf)) name_len = sizeof(nbuf) - 1;
    memcpy(nbuf, name, name_len); nbuf[name_len] = '\0';
    basl_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || !c->headers) return push_string(vm, "", 0, error);

    /* Case-insensitive search for "Name:" in headers */
    const char *p = c->headers;
    while (*p) {
        const char *eol = strstr(p, "\r\n");
        if (!eol) eol = p + strlen(p);
        const char *colon = strchr(p, ':');
        if (colon && colon < eol) {
            size_t klen = (size_t)(colon - p);
            if (klen == name_len) {
                int match = 1;
                for (size_t i = 0; i < klen; i++) {
                    char a = p[i], b = nbuf[i];
                    if (a >= 'A' && a <= 'Z') a += 32;
                    if (b >= 'A' && b <= 'Z') b += 32;
                    if (a != b) { match = 0; break; }
                }
                if (match) {
                    const char *val = colon + 1;
                    while (val < eol && *val == ' ') val++;
                    return push_string(vm, val, (size_t)(eol - val), error);
                }
            }
        }
        if (*eol == '\0') break;
        p = eol + 2;
    }
    return push_string(vm, "", 0, error);
}

static basl_status_t http_req_query(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    if (!c || !c->path) return push_string(vm, "", 0, error);
    const char *q = strchr(c->path, '?');
    if (q) return push_string(vm, q + 1, strlen(q + 1), error);
    return push_string(vm, "", 0, error);
}

static basl_status_t http_respond(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    int64_t status_code = get_i64_arg(vm, base, 1);
    const char *hdrs = NULL, *body = NULL;
    size_t hdrs_len = 0, body_len = 0;
    get_string_arg(vm, base, 2, &hdrs, &hdrs_len);
    get_string_arg(vm, base, 3, &body, &body_len);

    char *hc = NULL, *bc = NULL;
    if (hdrs_len > 0) { hc = (char *)malloc(hdrs_len + 1); memcpy(hc, hdrs, hdrs_len); hc[hdrs_len] = '\0'; }
    if (body_len > 0) { bc = (char *)malloc(body_len + 1); memcpy(bc, body, body_len); bc[body_len] = '\0'; }
    basl_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || c->sock == BASL_INVALID_SOCKET) { free(hc); free(bc); return push_i64(vm, -1, error); }

    const char *reason = "OK";
    switch ((int)status_code) {
        case 200: reason = "OK"; break;
        case 201: reason = "Created"; break;
        case 204: reason = "No Content"; break;
        case 301: reason = "Moved Permanently"; break;
        case 302: reason = "Found"; break;
        case 304: reason = "Not Modified"; break;
        case 400: reason = "Bad Request"; break;
        case 401: reason = "Unauthorized"; break;
        case 403: reason = "Forbidden"; break;
        case 404: reason = "Not Found"; break;
        case 405: reason = "Method Not Allowed"; break;
        case 500: reason = "Internal Server Error"; break;
        case 502: reason = "Bad Gateway"; break;
        case 503: reason = "Service Unavailable"; break;
        default: break;
    }

    char resp_hdr[4096];
    int hlen = snprintf(resp_hdr, sizeof(resp_hdr),
        "HTTP/1.1 %d %s\r\n%sContent-Length: %zu\r\nConnection: close\r\n\r\n",
        (int)status_code, reason, hc ? hc : "", body_len);

    int rc = 0;
    if (basl_platform_tcp_send(c->sock, resp_hdr, (size_t)hlen, NULL, NULL) != BASL_STATUS_OK) rc = -1;
    if (rc == 0 && bc && body_len > 0) {
        if (basl_platform_tcp_send(c->sock, bc, body_len, NULL, NULL) != BASL_STATUS_OK) rc = -1;
    }
    free(hc); free(bc);

    basl_platform_tcp_close(c->sock, NULL);
    c->sock = BASL_INVALID_SOCKET;
    client_free_fields(c); c->in_use = 0;
    return push_i64(vm, rc, error);
}

static void server_free_routes(http_server_t *srv) {
    for (int i = 0; i < srv->route_count; i++) {
        free(srv->routes[i].pattern);
        if (srv->routes[i].handler) basl_object_release(&srv->routes[i].handler);
    }
    srv->route_count = 0;
}

static basl_status_t http_server_close(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t h = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    if (h >= 0 && h < HTTP_MAX_SERVERS && g_servers[h].in_use) {
        basl_platform_tcp_close(g_servers[h].listener, NULL);
        server_free_routes(&g_servers[h]);
        g_servers[h].listener = BASL_INVALID_SOCKET; g_servers[h].in_use = 0;
    }
    basl_value_t nil = basl_nanbox_encode_int(0);
    return basl_vm_stack_push(vm, &nil, error);
}

static basl_status_t http_set_read_timeout(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t h = get_i64_arg(vm, base, 0);
    int64_t ms = get_i64_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    if (h >= 0 && h < HTTP_MAX_SERVERS && g_servers[h].in_use)
        g_servers[h].read_timeout_ms = (int)ms;
    return push_i64(vm, 0, error);
}

static basl_status_t http_set_write_timeout(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t h = get_i64_arg(vm, base, 0);
    int64_t ms = get_i64_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    if (h >= 0 && h < HTTP_MAX_SERVERS && g_servers[h].in_use)
        g_servers[h].write_timeout_ms = (int)ms;
    return push_i64(vm, 0, error);
}

static basl_status_t http_set_idle_timeout(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t h = get_i64_arg(vm, base, 0);
    int64_t ms = get_i64_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    if (h >= 0 && h < HTTP_MAX_SERVERS && g_servers[h].in_use)
        g_servers[h].idle_timeout_ms = (int)ms;
    return push_i64(vm, 0, error);
}

/* ── Routing and serve loop ──────────────────────────────────────── */

/* Per-thread current connection — use platform thread-local if available,
   otherwise a simple global (safe for single-threaded serve). */
static volatile int64_t g_current_conn = -1;

typedef struct {
    basl_runtime_t *runtime;
    basl_object_t *handler;
    int64_t conn_handle;
    int read_timeout_ms;
    int write_timeout_ms;
} serve_thread_ctx_t;

static void serve_thread_entry(void *arg) {
    serve_thread_ctx_t *ctx = (serve_thread_ctx_t *)arg;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};

    g_current_conn = ctx->conn_handle;

    /* Apply timeouts to the connection socket */
    http_conn_t *c = get_client(ctx->conn_handle);
    if (c && c->sock != BASL_INVALID_SOCKET) {
        int timeout = ctx->read_timeout_ms > ctx->write_timeout_ms
                      ? ctx->read_timeout_ms : ctx->write_timeout_ms;
        if (timeout > 0)
            basl_platform_tcp_set_timeout(c->sock, timeout, NULL);
    }

    if (basl_vm_open(&vm, ctx->runtime, NULL, &error) == BASL_STATUS_OK) {
        basl_value_t out = {0};
        basl_vm_execute_function(vm, ctx->handler, &out, &error);
        basl_vm_close(&vm);
    }

    /* Clean up if handler didn't call respond */
    http_conn_t *conn = get_client(ctx->conn_handle);
    if (conn && conn->sock != BASL_INVALID_SOCKET) {
        basl_platform_tcp_close(conn->sock, NULL);
        conn->sock = BASL_INVALID_SOCKET;
    }
    if (conn) { client_free_fields(conn); conn->in_use = 0; }

    basl_object_release(&ctx->handler);
    free(ctx);
}

static basl_status_t http_handle(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t srv_h = get_i64_arg(vm, base, 0);
    const char *pattern; size_t plen;
    get_string_arg(vm, base, 1, &pattern, &plen);
    basl_value_t fn_val = basl_vm_stack_get(vm, base + 2);
    basl_object_t *fn = basl_value_as_object(&fn_val);

    char *pcopy = NULL;
    if (plen > 0) { pcopy = (char *)malloc(plen + 1); memcpy(pcopy, pattern, plen); pcopy[plen] = '\0'; }
    basl_vm_stack_pop_n(vm, arg_count);

    if (srv_h < 0 || srv_h >= HTTP_MAX_SERVERS || !g_servers[srv_h].in_use ||
        !fn || g_servers[srv_h].route_count >= HTTP_MAX_ROUTES) {
        free(pcopy);
        return push_i64(vm, -1, error);
    }

    basl_object_retain(fn);
    http_route_t *r = &g_servers[srv_h].routes[g_servers[srv_h].route_count++];
    r->pattern = pcopy;
    r->handler = fn;
    return push_i64(vm, 0, error);
}

static int route_matches(const char *pattern, const char *path) {
    if (!pattern || !path) return 0;
    size_t plen = strlen(pattern);
    /* Exact match */
    if (strcmp(pattern, path) == 0) return 1;
    /* Prefix match: pattern ends with '/' and path starts with pattern */
    if (plen > 0 && pattern[plen - 1] == '/' && strncmp(path, pattern, plen) == 0) return 1;
    /* Path without query matches pattern */
    const char *q = strchr(path, '?');
    if (q) {
        size_t path_len = (size_t)(q - path);
        if (path_len == plen && strncmp(path, pattern, plen) == 0) return 1;
    }
    return 0;
}

static basl_status_t http_serve(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t srv_h = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);

    if (srv_h < 0 || srv_h >= HTTP_MAX_SERVERS || !g_servers[srv_h].in_use)
        return push_i64(vm, -1, error);

    http_server_t *srv = &g_servers[srv_h];
    basl_runtime_t *runtime = basl_vm_runtime(vm);

    for (;;) {
        basl_socket_t client = BASL_INVALID_SOCKET;
        if (basl_platform_tcp_accept(srv->listener, &client, NULL) != BASL_STATUS_OK)
            break;

        int64_t ch = alloc_client(client);
        if (ch < 0) { basl_platform_tcp_close(client, NULL); continue; }

        http_conn_t *conn = get_client(ch);
        if (parse_incoming_request(conn) != 0) {
            client_free_fields(conn);
            basl_platform_tcp_close(conn->sock, NULL);
            conn->sock = BASL_INVALID_SOCKET; conn->in_use = 0;
            continue;
        }

        /* Find matching route */
        basl_object_t *handler = NULL;
        for (int i = 0; i < srv->route_count; i++) {
            if (route_matches(srv->routes[i].pattern, conn->path)) {
                handler = srv->routes[i].handler;
                break;
            }
        }

        if (handler) {
            serve_thread_ctx_t *ctx = (serve_thread_ctx_t *)malloc(sizeof(*ctx));
            ctx->runtime = runtime;
            ctx->handler = handler;
            ctx->conn_handle = ch;
            ctx->read_timeout_ms = srv->read_timeout_ms;
            ctx->write_timeout_ms = srv->write_timeout_ms;
            basl_object_retain(handler);

            basl_platform_thread_t *t = NULL;
            if (basl_platform_thread_create(&t, serve_thread_entry, ctx, NULL) == BASL_STATUS_OK) {
                /* Detach — thread cleans up after itself */
                (void)t;
            } else {
                basl_object_release(&ctx->handler);
                free(ctx);
                basl_platform_tcp_close(conn->sock, NULL);
                conn->sock = BASL_INVALID_SOCKET;
                client_free_fields(conn); conn->in_use = 0;
            }
        } else {
            /* 404 for unmatched routes */
            const char *body_404 = "404 Not Found";
            char hdr[256];
            int hlen = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 404 Not Found\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                strlen(body_404));
            basl_platform_tcp_send(conn->sock, hdr, (size_t)hlen, NULL, NULL);
            basl_platform_tcp_send(conn->sock, body_404, strlen(body_404), NULL, NULL);
            basl_platform_tcp_close(conn->sock, NULL);
            conn->sock = BASL_INVALID_SOCKET;
            client_free_fields(conn); conn->in_use = 0;
        }
    }

    return push_i64(vm, 0, error);
}

static basl_status_t http_current_conn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, g_current_conn, error);
}

static basl_status_t http_redirect(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    const char *url; size_t url_len;
    get_string_arg(vm, base, 1, &url, &url_len);
    int64_t status_code = arg_count >= 3 ? get_i64_arg(vm, base, 2) : 302;

    char ubuf[2048];
    if (url_len >= sizeof(ubuf)) url_len = sizeof(ubuf) - 1;
    memcpy(ubuf, url, url_len); ubuf[url_len] = '\0';
    basl_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || c->sock == BASL_INVALID_SOCKET) return push_i64(vm, -1, error);

    const char *reason = "Found";
    if (status_code == 301) reason = "Moved Permanently";
    else if (status_code == 307) reason = "Temporary Redirect";
    else if (status_code == 308) reason = "Permanent Redirect";

    char hdr[4096];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\nLocation: %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
        (int)status_code, reason, ubuf);
    basl_platform_tcp_send(c->sock, hdr, (size_t)hlen, NULL, NULL);
    basl_platform_tcp_close(c->sock, NULL);
    c->sock = BASL_INVALID_SOCKET;
    client_free_fields(c); c->in_use = 0;
    return push_i64(vm, 0, error);
}

/* ── Client: redirect following ──────────────────────────────────── */

/* ── Client BASL functions ───────────────────────────────────────── */

static basl_status_t http_get(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url; size_t url_len;
    if (!get_string_arg(vm, base, 0, &url, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        push_string(vm, "", 0, error); push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }
    char *uc = (char *)malloc(url_len + 1); memcpy(uc, url, url_len); uc[url_len] = '\0';
    basl_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request("GET", uc, NULL, NULL, 0, &resp);
    free(uc);
    if (rc != 0) { push_string(vm, "", 0, error); push_string(vm, "", 0, error); return push_i64(vm, -1, error); }
    int status = resp.status_code;
    push_string(vm, resp.headers ? resp.headers : "", resp.headers ? strlen(resp.headers) : 0, error);
    basl_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    return st != BASL_STATUS_OK ? st : push_i64(vm, status, error);
}

static basl_status_t http_post(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url, *body, *ct = NULL;
    size_t url_len, body_len, ct_len = 0;
    if (!get_string_arg(vm, base, 0, &url, &url_len) ||
        !get_string_arg(vm, base, 1, &body, &body_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        push_string(vm, "", 0, error); push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }
    if (arg_count >= 3) get_string_arg(vm, base, 2, &ct, &ct_len);
    char *uc = (char *)malloc(url_len + 1); memcpy(uc, url, url_len); uc[url_len] = '\0';
    char *bc = (char *)malloc(body_len + 1); memcpy(bc, body, body_len); bc[body_len] = '\0';
    char hdrs[512] = "";
    if (ct && ct_len > 0) snprintf(hdrs, sizeof(hdrs), "Content-Type: %.*s\r\n", (int)ct_len, ct);
    basl_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request("POST", uc, hdrs, bc, body_len, &resp);
    free(uc); free(bc);
    if (rc != 0) { push_string(vm, "", 0, error); push_string(vm, "", 0, error); return push_i64(vm, -1, error); }
    int status = resp.status_code;
    push_string(vm, resp.headers ? resp.headers : "", resp.headers ? strlen(resp.headers) : 0, error);
    basl_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    return st != BASL_STATUS_OK ? st : push_i64(vm, status, error);
}

static basl_status_t http_request(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *method, *url, *headers = NULL, *body = NULL;
    size_t ml, ul, hl = 0, bl = 0;
    if (!get_string_arg(vm, base, 0, &method, &ml) ||
        !get_string_arg(vm, base, 1, &url, &ul)) {
        basl_vm_stack_pop_n(vm, arg_count);
        push_string(vm, "", 0, error); push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }
    if (arg_count >= 3) get_string_arg(vm, base, 2, &headers, &hl);
    if (arg_count >= 4) get_string_arg(vm, base, 3, &body, &bl);
    char *mc = (char *)malloc(ml + 1); memcpy(mc, method, ml); mc[ml] = '\0';
    char *uc = (char *)malloc(ul + 1); memcpy(uc, url, ul); uc[ul] = '\0';
    char *hc = hl ? (char *)malloc(hl + 1) : NULL;
    char *bc = bl ? (char *)malloc(bl + 1) : NULL;
    if (hc) { memcpy(hc, headers, hl); hc[hl] = '\0'; }
    if (bc) { memcpy(bc, body, bl); bc[bl] = '\0'; }
    basl_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request(mc, uc, hc, bc, bl, &resp);
    free(mc); free(uc); free(hc); free(bc);
    if (rc != 0) { push_string(vm, "", 0, error); push_string(vm, "", 0, error); return push_i64(vm, -1, error); }
    int status = resp.status_code;
    push_string(vm, resp.headers ? resp.headers : "", resp.headers ? strlen(resp.headers) : 0, error);
    basl_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    return st != BASL_STATUS_OK ? st : push_i64(vm, status, error);
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int http_1str[] = { BASL_TYPE_STRING };
static const int http_2str[] = { BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int http_str_i32[] = { BASL_TYPE_STRING, BASL_TYPE_I32 };
static const int http_i64[] = { BASL_TYPE_I64 };
static const int http_respond_p[] = { BASL_TYPE_I64, BASL_TYPE_I32, BASL_TYPE_STRING, BASL_TYPE_STRING };

static const int http_i64_str[] = { BASL_TYPE_I64, BASL_TYPE_STRING };
static const int http_i64_i64[] = { BASL_TYPE_I64, BASL_TYPE_I64 };
static const int http_handle_p[] = { BASL_TYPE_I64, BASL_TYPE_STRING, BASL_TYPE_OBJECT };

static const basl_native_module_function_t http_functions[] = {
    { "get", 3, http_get, 1, http_1str, BASL_TYPE_I32, 3, NULL, 0, NULL, NULL },
    { "post", 4, http_post, 2, http_2str, BASL_TYPE_I32, 3, NULL, 0, NULL, NULL },
    { "request", 7, http_request, 2, http_2str, BASL_TYPE_I32, 3, NULL, 0, NULL, NULL },
    { "listen", 6, http_listen, 2, http_str_i32, BASL_TYPE_I64, 1, NULL, 0, NULL, NULL },
    { "accept", 6, http_accept, 1, http_i64, BASL_TYPE_I64, 1, NULL, 0, NULL, NULL },
    { "handle", 6, http_handle, 3, http_handle_p, BASL_TYPE_I32, 1, NULL, 0, NULL, NULL },
    { "serve", 5, http_serve, 1, http_i64, BASL_TYPE_I32, 1, NULL, 0, NULL, NULL },
    { "current_conn", 12, http_current_conn, 0, NULL, BASL_TYPE_I64, 1, NULL, 0, NULL, NULL },
    { "req_method", 10, http_req_method, 1, http_i64, BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL },
    { "req_path", 8, http_req_path, 1, http_i64, BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL },
    { "req_body", 8, http_req_body, 1, http_i64, BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL },
    { "req_headers", 11, http_req_headers, 1, http_i64, BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL },
    { "req_header", 10, http_req_header, 2, http_i64_str, BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL },
    { "req_query", 9, http_req_query, 1, http_i64, BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL },
    { "respond", 7, http_respond, 4, http_respond_p, BASL_TYPE_I32, 1, NULL, 0, NULL, NULL },
    { "redirect", 8, http_redirect, 2, http_i64_str, BASL_TYPE_I32, 1, NULL, 0, NULL, NULL },
    { "close", 5, http_server_close, 1, http_i64, BASL_TYPE_VOID, 0, NULL, 0, NULL, NULL },
    { "set_read_timeout", 16, http_set_read_timeout, 2, http_i64_i64, BASL_TYPE_I32, 1, NULL, 0, NULL, NULL },
    { "set_write_timeout", 17, http_set_write_timeout, 2, http_i64_i64, BASL_TYPE_I32, 1, NULL, 0, NULL, NULL },
    { "set_idle_timeout", 16, http_set_idle_timeout, 2, http_i64_i64, BASL_TYPE_I32, 1, NULL, 0, NULL, NULL },
};

BASL_API const basl_native_module_t basl_stdlib_http = {
    "http", 4,
    http_functions,
    sizeof(http_functions) / sizeof(http_functions[0]),
    NULL, 0
};
