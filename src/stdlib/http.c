/* BASL standard library: http module.
 *
 * HTTP/S client using native libraries (WinHTTP on Windows, libcurl on POSIX)
 * with fallback to plain socket-based HTTP/1.1 (no TLS).
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define close_socket closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#define close_socket close
#endif

/* ── Helpers ─────────────────────────────────────────────────────── */

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

/* ── URL parsing ─────────────────────────────────────────────────── */

#ifdef BASL_HTTP_TESTING
#define HTTP_STATIC
#else
#define HTTP_STATIC static
#endif

typedef struct {
    char scheme[16];
    char host[256];
    int port;
    char path[2048];
} parsed_url_t;

HTTP_STATIC int parse_url(const char *url, parsed_url_t *out) {
    memset(out, 0, sizeof(*out));
    strcpy(out->path, "/");

    const char *p = url;
    /* scheme */
    const char *scheme_end = strstr(p, "://");
    if (scheme_end) {
        size_t slen = (size_t)(scheme_end - p);
        if (slen >= sizeof(out->scheme)) return 0;
        memcpy(out->scheme, p, slen);
        p = scheme_end + 3;
    } else {
        strcpy(out->scheme, "http");
    }

    /* host and port */
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

/* ── Socket-based fallback (HTTP only, no TLS) ───────────────────── */

static void socket_init(void) {
#ifdef _WIN32
    static int inited = 0;
    if (!inited) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        inited = 1;
    }
#endif
}

HTTP_STATIC int socket_request(const char *method, parsed_url_t *url,
                          const char *headers, const char *body, size_t body_len,
                          http_response_t *resp) {
    socket_init();
    memset(resp, 0, sizeof(*resp));

    /* Resolve host */
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", url->port);
    if (getaddrinfo(url->host, port_str, &hints, &res) != 0) return -1;

    socket_t sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCK) { freeaddrinfo(res); return -1; }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) < 0) {
        close_socket(sock);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    /* Build request */
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

    send(sock, req_buf, req_len, 0);
    if (body && body_len > 0) {
        send(sock, body, (int)body_len, 0);
    }

    /* Read response */
    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { close_socket(sock); return -1; }

    int n;
    while ((n = recv(sock, buf + len, (int)(cap - len - 1), 0)) > 0) {
        len += (size_t)n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); close_socket(sock); return -1; }
            buf = nb;
        }
    }
    buf[len] = '\0';
    close_socket(sock);

    /* Parse response */
    char *header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) { free(buf); return -1; }

    /* Status code */
    if (strncmp(buf, "HTTP/", 5) == 0) {
        const char *sp = strchr(buf, ' ');
        if (sp) resp->status_code = atoi(sp + 1);
    }

    /* Headers */
    size_t hdr_len = (size_t)(header_end - buf);
    resp->headers = (char *)malloc(hdr_len + 1);
    if (resp->headers) {
        memcpy(resp->headers, buf, hdr_len);
        resp->headers[hdr_len] = '\0';
    }

    /* Body */
    char *body_start = header_end + 4;
    resp->body_len = len - (size_t)(body_start - buf);
    resp->body = (char *)malloc(resp->body_len + 1);
    if (resp->body) {
        memcpy(resp->body, body_start, resp->body_len);
        resp->body[resp->body_len] = '\0';
    }

    free(buf);
    return 0;
}

/* ── WinHTTP implementation (Windows) ────────────────────────────── */

#ifdef _WIN32
static int winhttp_request(const char *method, parsed_url_t *url,
                           const char *headers, const char *body, size_t body_len,
                           http_response_t *resp) {
    memset(resp, 0, sizeof(*resp));

    /* Convert to wide strings */
    wchar_t whost[256], wpath[2048], wmethod[16];
    MultiByteToWideChar(CP_UTF8, 0, url->host, -1, whost, 256);
    MultiByteToWideChar(CP_UTF8, 0, url->path, -1, wpath, 2048);
    MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, 16);

    HINTERNET session = WinHttpOpen(L"BASL/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return -1;

    HINTERNET conn = WinHttpConnect(session, whost, (INTERNET_PORT)url->port, 0);
    if (!conn) { WinHttpCloseHandle(session); return -1; }

    DWORD flags = (strcmp(url->scheme, "https") == 0) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, wmethod, wpath, NULL,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return -1; }

    /* Add headers */
    wchar_t wheaders[4096] = L"";
    if (headers && *headers) {
        MultiByteToWideChar(CP_UTF8, 0, headers, -1, wheaders, 4096);
    }

    BOOL ok = WinHttpSendRequest(req, wheaders[0] ? wheaders : WINHTTP_NO_ADDITIONAL_HEADERS,
                                  (DWORD)-1, (LPVOID)body, (DWORD)body_len, (DWORD)body_len, 0);
    if (!ok) goto cleanup;

    ok = WinHttpReceiveResponse(req, NULL);
    if (!ok) goto cleanup;

    /* Status code */
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    resp->status_code = (int)status;

    /* Read body */
    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    DWORD downloaded;
    while (WinHttpReadData(req, buf + len, (DWORD)(cap - len - 1), &downloaded) && downloaded > 0) {
        len += downloaded;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
    }
    buf[len] = '\0';
    resp->body = buf;
    resp->body_len = len;

cleanup:
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return resp->status_code > 0 ? 0 : -1;
}
#endif

/* ── libcurl implementation (POSIX) ──────────────────────────────── */

#ifndef _WIN32
#include <dlfcn.h>

typedef void CURL;
typedef int CURLcode;
#define CURLE_OK 0
#define CURLOPT_URL 10002
#define CURLOPT_WRITEFUNCTION 20011
#define CURLOPT_WRITEDATA 10001
#define CURLOPT_HTTPHEADER 10023
#define CURLOPT_POSTFIELDS 10015
#define CURLOPT_POSTFIELDSIZE 60
#define CURLOPT_CUSTOMREQUEST 10036
#define CURLINFO_RESPONSE_CODE 0x200002

typedef CURL *(*curl_easy_init_t)(void);
typedef void (*curl_easy_cleanup_t)(CURL *);
typedef CURLcode (*curl_easy_setopt_t)(CURL *, int, ...);
typedef CURLcode (*curl_easy_perform_t)(CURL *);
typedef CURLcode (*curl_easy_getinfo_t)(CURL *, int, ...);
typedef struct curl_slist *(*curl_slist_append_t)(struct curl_slist *, const char *);
typedef void (*curl_slist_free_all_t)(struct curl_slist *);

static void *g_curl_lib = NULL;
static curl_easy_init_t p_curl_easy_init = NULL;
static curl_easy_cleanup_t p_curl_easy_cleanup = NULL;
static curl_easy_setopt_t p_curl_easy_setopt = NULL;
static curl_easy_perform_t p_curl_easy_perform = NULL;
static curl_easy_getinfo_t p_curl_easy_getinfo = NULL;
static curl_slist_append_t p_curl_slist_append = NULL;
static curl_slist_free_all_t p_curl_slist_free_all = NULL;

static int curl_load(void) {
    if (g_curl_lib) return 1;
    const char *libs[] = {"libcurl.so.4", "libcurl.so", "libcurl.dylib", NULL};
    for (int i = 0; libs[i]; i++) {
        g_curl_lib = dlopen(libs[i], RTLD_NOW);
        if (g_curl_lib) break;
    }
    if (!g_curl_lib) return 0;

    p_curl_easy_init = (curl_easy_init_t)dlsym(g_curl_lib, "curl_easy_init");
    p_curl_easy_cleanup = (curl_easy_cleanup_t)dlsym(g_curl_lib, "curl_easy_cleanup");
    p_curl_easy_setopt = (curl_easy_setopt_t)dlsym(g_curl_lib, "curl_easy_setopt");
    p_curl_easy_perform = (curl_easy_perform_t)dlsym(g_curl_lib, "curl_easy_perform");
    p_curl_easy_getinfo = (curl_easy_getinfo_t)dlsym(g_curl_lib, "curl_easy_getinfo");
    p_curl_slist_append = (curl_slist_append_t)dlsym(g_curl_lib, "curl_slist_append");
    p_curl_slist_free_all = (curl_slist_free_all_t)dlsym(g_curl_lib, "curl_slist_free_all");

    return p_curl_easy_init && p_curl_easy_cleanup && p_curl_easy_setopt &&
           p_curl_easy_perform && p_curl_easy_getinfo;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} curl_buffer_t;

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    curl_buffer_t *buf = (curl_buffer_t *)userdata;
    size_t total = size * nmemb;
    if (buf->len + total >= buf->cap) {
        buf->cap = (buf->len + total) * 2;
        buf->data = (char *)realloc(buf->data, buf->cap);
    }
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    return total;
}

static int curl_request(const char *method, const char *url,
                        const char *headers, const char *body, size_t body_len,
                        http_response_t *resp) {
    memset(resp, 0, sizeof(*resp));
    if (!curl_load()) return -1;

    CURL *curl = p_curl_easy_init();
    if (!curl) return -1;

    curl_buffer_t buf = {(char *)malloc(4096), 0, 4096};

    p_curl_easy_setopt(curl, CURLOPT_URL, url);
    p_curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    p_curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    p_curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);

    struct curl_slist *hdr_list = NULL;
    if (headers && *headers) {
        /* Parse headers line by line */
        char *hcopy = strdup(headers);
        char *line = strtok(hcopy, "\r\n");
        while (line) {
            if (*line) hdr_list = p_curl_slist_append(hdr_list, line);
            line = strtok(NULL, "\r\n");
        }
        free(hcopy);
        if (hdr_list) p_curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
    }

    if (body && body_len > 0) {
        p_curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        p_curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    }

    CURLcode res = p_curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        p_curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp->status_code = (int)code;
        buf.data[buf.len] = '\0';
        resp->body = buf.data;
        resp->body_len = buf.len;
    } else {
        free(buf.data);
    }

    if (hdr_list) p_curl_slist_free_all(hdr_list);
    p_curl_easy_cleanup(curl);
    return res == CURLE_OK ? 0 : -1;
}
#endif

/* ── Unified request function ────────────────────────────────────── */

HTTP_STATIC int do_request(const char *method, const char *url_str,
                      const char *headers, const char *body, size_t body_len,
                      http_response_t *resp) {
    parsed_url_t url;
    if (!parse_url(url_str, &url)) return -1;

    int is_https = (strcmp(url.scheme, "https") == 0);

#ifdef _WIN32
    /* Windows: always use WinHTTP (supports HTTPS) */
    return winhttp_request(method, &url, headers, body, body_len, resp);
#else
    /* POSIX: try libcurl first (supports HTTPS), fallback to sockets (HTTP only) */
    if (curl_load()) {
        return curl_request(method, url_str, headers, body, body_len, resp);
    }
    if (is_https) {
        /* No TLS support without libcurl */
        memset(resp, 0, sizeof(*resp));
        return -1;
    }
    return socket_request(method, &url, headers, body, body_len, resp);
#endif
}

/* ── http.get(url) -> {status, body} ─────────────────────────────── */

static basl_status_t http_get(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url; size_t url_len;

    if (!get_string_arg(vm, base, 0, &url, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    char *url_copy = (char *)malloc(url_len + 1);
    memcpy(url_copy, url, url_len);
    url_copy[url_len] = '\0';
    basl_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request("GET", url_copy, NULL, NULL, 0, &resp);
    free(url_copy);

    if (rc != 0) {
        push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }

    /* Push body first, then status (caller pops status first) */
    int status = resp.status_code;
    basl_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    if (st != BASL_STATUS_OK) return st;
    return push_i64(vm, status, error);
}

/* ── http.post(url, body, content_type?) -> {status, body} ───────── */

static basl_status_t http_post(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url, *body, *ct = NULL;
    size_t url_len, body_len, ct_len = 0;

    if (!get_string_arg(vm, base, 0, &url, &url_len) ||
        !get_string_arg(vm, base, 1, &body, &body_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    if (arg_count >= 3) get_string_arg(vm, base, 2, &ct, &ct_len);

    char *url_copy = (char *)malloc(url_len + 1);
    char *body_copy = (char *)malloc(body_len + 1);
    memcpy(url_copy, url, url_len); url_copy[url_len] = '\0';
    memcpy(body_copy, body, body_len); body_copy[body_len] = '\0';

    char headers[512] = "";
    if (ct && ct_len > 0) {
        snprintf(headers, sizeof(headers), "Content-Type: %.*s\r\n", (int)ct_len, ct);
    }

    basl_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request("POST", url_copy, headers, body_copy, body_len, &resp);
    free(url_copy);
    free(body_copy);

    if (rc != 0) {
        push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }

    int status = resp.status_code;
    basl_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    if (st != BASL_STATUS_OK) return st;
    return push_i64(vm, status, error);
}

/* ── http.request(method, url, headers?, body?) -> {status, body} ── */

static basl_status_t http_request(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *method, *url, *headers = NULL, *body = NULL;
    size_t method_len, url_len, headers_len = 0, body_len = 0;

    if (!get_string_arg(vm, base, 0, &method, &method_len) ||
        !get_string_arg(vm, base, 1, &url, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    if (arg_count >= 3) get_string_arg(vm, base, 2, &headers, &headers_len);
    if (arg_count >= 4) get_string_arg(vm, base, 3, &body, &body_len);

    char *method_copy = (char *)malloc(method_len + 1);
    char *url_copy = (char *)malloc(url_len + 1);
    char *headers_copy = headers_len ? (char *)malloc(headers_len + 1) : NULL;
    char *body_copy = body_len ? (char *)malloc(body_len + 1) : NULL;

    memcpy(method_copy, method, method_len); method_copy[method_len] = '\0';
    memcpy(url_copy, url, url_len); url_copy[url_len] = '\0';
    if (headers_copy) { memcpy(headers_copy, headers, headers_len); headers_copy[headers_len] = '\0'; }
    if (body_copy) { memcpy(body_copy, body, body_len); body_copy[body_len] = '\0'; }

    basl_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request(method_copy, url_copy, headers_copy, body_copy, body_len, &resp);

    free(method_copy);
    free(url_copy);
    free(headers_copy);
    free(body_copy);

    if (rc != 0) {
        push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }

    int status = resp.status_code;
    basl_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    if (st != BASL_STATUS_OK) return st;
    return push_i64(vm, status, error);
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int http_1str_params[] = { BASL_TYPE_STRING };
static const int http_2str_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING };

static const basl_native_module_function_t http_functions[] = {
    { "get", 3, http_get, 1, http_1str_params, BASL_TYPE_I32, 2, NULL, 0, NULL, NULL },
    { "post", 4, http_post, 2, http_2str_params, BASL_TYPE_I32, 2, NULL, 0, NULL, NULL },
    { "request", 7, http_request, 2, http_2str_params, BASL_TYPE_I32, 2, NULL, 0, NULL, NULL },
};

BASL_API const basl_native_module_t basl_stdlib_http = {
    "http", 4,
    http_functions,
    sizeof(http_functions) / sizeof(http_functions[0]),
    NULL, 0
};
