/* BASL standard library: http module.
 *
 * HTTP/S client using native libraries loaded at runtime:
 *   - WinHTTP via LoadLibrary() on Windows
 *   - libcurl via dlopen() on POSIX
 * Fallback to plain TCP socket-based HTTP/1.1 client and server (no TLS).
 *
 * All dynamic library loading goes through the platform abstraction layer
 * (basl_platform_dlopen / basl_platform_dlsym).
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
    strcpy(out->path, "/");

    const char *p = url;
    const char *scheme_end = strstr(p, "://");
    if (scheme_end) {
        size_t slen = (size_t)(scheme_end - p);
        if (slen >= sizeof(out->scheme)) return 0;
        memcpy(out->scheme, p, slen);
        p = scheme_end + 3;
    } else {
        strcpy(out->scheme, "http");
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

/* ── Socket init and TCP fallback (HTTP only, no TLS) ────────────── */

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

    char *header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) { free(buf); return -1; }

    if (strncmp(buf, "HTTP/", 5) == 0) {
        const char *sp = strchr(buf, ' ');
        if (sp) resp->status_code = atoi(sp + 1);
    }

    size_t hdr_len = (size_t)(header_end - buf);
    resp->headers = (char *)malloc(hdr_len + 1);
    if (resp->headers) {
        memcpy(resp->headers, buf, hdr_len);
        resp->headers[hdr_len] = '\0';
    }

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

/* ── WinHTTP via runtime loading (Windows) ───────────────────────── */

#ifdef _WIN32
#include <winhttp.h>

typedef HINTERNET (WINAPI *pWinHttpOpen_t)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef HINTERNET (WINAPI *pWinHttpConnect_t)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET (WINAPI *pWinHttpOpenRequest_t)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
typedef BOOL (WINAPI *pWinHttpSendRequest_t)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *pWinHttpReceiveResponse_t)(HINTERNET, LPVOID);
typedef BOOL (WINAPI *pWinHttpQueryHeaders_t)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL (WINAPI *pWinHttpReadData_t)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL (WINAPI *pWinHttpCloseHandle_t)(HINTERNET);

static void *g_winhttp_lib = NULL;
static pWinHttpOpen_t          p_WinHttpOpen = NULL;
static pWinHttpConnect_t       p_WinHttpConnect = NULL;
static pWinHttpOpenRequest_t   p_WinHttpOpenRequest = NULL;
static pWinHttpSendRequest_t   p_WinHttpSendRequest = NULL;
static pWinHttpReceiveResponse_t p_WinHttpReceiveResponse = NULL;
static pWinHttpQueryHeaders_t  p_WinHttpQueryHeaders = NULL;
static pWinHttpReadData_t      p_WinHttpReadData = NULL;
static pWinHttpCloseHandle_t   p_WinHttpCloseHandle = NULL;

static int winhttp_load(void) {
    if (g_winhttp_lib) return 1;
    if (basl_platform_dlopen("winhttp.dll", &g_winhttp_lib, NULL) != BASL_STATUS_OK)
        return 0;

    basl_platform_dlsym(g_winhttp_lib, "WinHttpOpen", (void **)&p_WinHttpOpen, NULL);
    basl_platform_dlsym(g_winhttp_lib, "WinHttpConnect", (void **)&p_WinHttpConnect, NULL);
    basl_platform_dlsym(g_winhttp_lib, "WinHttpOpenRequest", (void **)&p_WinHttpOpenRequest, NULL);
    basl_platform_dlsym(g_winhttp_lib, "WinHttpSendRequest", (void **)&p_WinHttpSendRequest, NULL);
    basl_platform_dlsym(g_winhttp_lib, "WinHttpReceiveResponse", (void **)&p_WinHttpReceiveResponse, NULL);
    basl_platform_dlsym(g_winhttp_lib, "WinHttpQueryHeaders", (void **)&p_WinHttpQueryHeaders, NULL);
    basl_platform_dlsym(g_winhttp_lib, "WinHttpReadData", (void **)&p_WinHttpReadData, NULL);
    basl_platform_dlsym(g_winhttp_lib, "WinHttpCloseHandle", (void **)&p_WinHttpCloseHandle, NULL);

    return p_WinHttpOpen && p_WinHttpConnect && p_WinHttpOpenRequest &&
           p_WinHttpSendRequest && p_WinHttpReceiveResponse &&
           p_WinHttpQueryHeaders && p_WinHttpReadData && p_WinHttpCloseHandle;
}

static int winhttp_request(const char *method, parsed_url_t *url,
                           const char *headers, const char *body, size_t body_len,
                           http_response_t *resp) {
    memset(resp, 0, sizeof(*resp));
    if (!winhttp_load()) return -1;

    wchar_t whost[256], wpath[2048], wmethod[16];
    MultiByteToWideChar(CP_UTF8, 0, url->host, -1, whost, 256);
    MultiByteToWideChar(CP_UTF8, 0, url->path, -1, wpath, 2048);
    MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, 16);

    HINTERNET session = p_WinHttpOpen(L"BASL/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return -1;

    HINTERNET conn = p_WinHttpConnect(session, whost, (INTERNET_PORT)url->port, 0);
    if (!conn) { p_WinHttpCloseHandle(session); return -1; }

    DWORD flags = (strcmp(url->scheme, "https") == 0) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = p_WinHttpOpenRequest(conn, wmethod, wpath, NULL,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { p_WinHttpCloseHandle(conn); p_WinHttpCloseHandle(session); return -1; }

    wchar_t wheaders[4096] = L"";
    if (headers && *headers) {
        MultiByteToWideChar(CP_UTF8, 0, headers, -1, wheaders, 4096);
    }

    BOOL ok = p_WinHttpSendRequest(req, wheaders[0] ? wheaders : WINHTTP_NO_ADDITIONAL_HEADERS,
                                    (DWORD)-1, (LPVOID)body, (DWORD)body_len, (DWORD)body_len, 0);
    if (!ok) goto cleanup;

    ok = p_WinHttpReceiveResponse(req, NULL);
    if (!ok) goto cleanup;

    DWORD status = 0, sz = sizeof(status);
    p_WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    resp->status_code = (int)status;

    {
        size_t cap = 8192, len = 0;
        char *buf = (char *)malloc(cap);
        DWORD downloaded;
        while (p_WinHttpReadData(req, buf + len, (DWORD)(cap - len - 1), &downloaded) && downloaded > 0) {
            len += downloaded;
            if (len + 1 >= cap) {
                cap *= 2;
                buf = (char *)realloc(buf, cap);
            }
        }
        buf[len] = '\0';
        resp->body = buf;
        resp->body_len = len;
    }

cleanup:
    p_WinHttpCloseHandle(req);
    p_WinHttpCloseHandle(conn);
    p_WinHttpCloseHandle(session);
    return resp->status_code > 0 ? 0 : -1;
}
#endif

/* ── libcurl via runtime loading (POSIX) ─────────────────────────── */

#ifndef _WIN32

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
        if (basl_platform_dlopen(libs[i], &g_curl_lib, NULL) == BASL_STATUS_OK)
            break;
    }
    if (!g_curl_lib) return 0;

    basl_platform_dlsym(g_curl_lib, "curl_easy_init", (void **)&p_curl_easy_init, NULL);
    basl_platform_dlsym(g_curl_lib, "curl_easy_cleanup", (void **)&p_curl_easy_cleanup, NULL);
    basl_platform_dlsym(g_curl_lib, "curl_easy_setopt", (void **)&p_curl_easy_setopt, NULL);
    basl_platform_dlsym(g_curl_lib, "curl_easy_perform", (void **)&p_curl_easy_perform, NULL);
    basl_platform_dlsym(g_curl_lib, "curl_easy_getinfo", (void **)&p_curl_easy_getinfo, NULL);
    basl_platform_dlsym(g_curl_lib, "curl_slist_append", (void **)&p_curl_slist_append, NULL);
    basl_platform_dlsym(g_curl_lib, "curl_slist_free_all", (void **)&p_curl_slist_free_all, NULL);

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

/* ── Unified client request ──────────────────────────────────────── */

HTTP_STATIC int do_request(const char *method, const char *url_str,
                      const char *headers, const char *body, size_t body_len,
                      http_response_t *resp) {
    parsed_url_t url;
    if (!parse_url(url_str, &url)) return -1;

#ifdef _WIN32
    if (winhttp_load()) {
        return winhttp_request(method, &url, headers, body, body_len, resp);
    }
    /* Fallback to sockets (HTTP only) */
    if (strcmp(url.scheme, "https") == 0) {
        memset(resp, 0, sizeof(*resp));
        return -1;
    }
    return socket_request(method, &url, headers, body, body_len, resp);
#else
    int is_https = (strcmp(url.scheme, "https") == 0);

    if (curl_load()) {
        return curl_request(method, url_str, headers, body, body_len, resp);
    }
    if (is_https) {
        memset(resp, 0, sizeof(*resp));
        return -1;
    }
    return socket_request(method, &url, headers, body, body_len, resp);
#endif
}

/* ── HTTP server via TCP sockets ─────────────────────────────────── */

#define HTTP_MAX_SERVERS 64

typedef struct {
    socket_t listener;
    int in_use;
} http_server_t;

static http_server_t g_servers[HTTP_MAX_SERVERS];
static int g_servers_inited = 0;

static void servers_init(void) {
    if (g_servers_inited) return;
    socket_init();
    for (int i = 0; i < HTTP_MAX_SERVERS; i++) {
        g_servers[i].listener = INVALID_SOCK;
        g_servers[i].in_use = 0;
    }
    g_servers_inited = 1;
}

static int64_t alloc_server(socket_t s) {
    for (int i = 0; i < HTTP_MAX_SERVERS; i++) {
        if (!g_servers[i].in_use) {
            g_servers[i].listener = s;
            g_servers[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static socket_t get_server(int64_t handle) {
    if (handle < 0 || handle >= HTTP_MAX_SERVERS) return INVALID_SOCK;
    if (!g_servers[handle].in_use) return INVALID_SOCK;
    return g_servers[handle].listener;
}

#define HTTP_MAX_CLIENTS 256

typedef struct {
    socket_t sock;
    int in_use;
    char *method;
    char *path;
    char *headers;
    char *body;
    size_t body_len;
} http_conn_t;

static http_conn_t g_clients[HTTP_MAX_CLIENTS];
static int g_clients_inited = 0;

static void clients_init(void) {
    if (g_clients_inited) return;
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        g_clients[i].sock = INVALID_SOCK;
        g_clients[i].in_use = 0;
        g_clients[i].method = NULL;
        g_clients[i].path = NULL;
        g_clients[i].headers = NULL;
        g_clients[i].body = NULL;
    }
    g_clients_inited = 1;
}

static void client_free_fields(http_conn_t *c) {
    free(c->method);  c->method = NULL;
    free(c->path);    c->path = NULL;
    free(c->headers);  c->headers = NULL;
    free(c->body);    c->body = NULL;
    c->body_len = 0;
}

static int64_t alloc_client(socket_t s) {
    clients_init();
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        if (!g_clients[i].in_use) {
            g_clients[i].sock = s;
            g_clients[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static http_conn_t *get_client(int64_t handle) {
    if (handle < 0 || handle >= HTTP_MAX_CLIENTS) return NULL;
    if (!g_clients[handle].in_use) return NULL;
    return &g_clients[handle];
}

/* Parse an incoming HTTP request from a connected socket. */
static int parse_incoming_request(http_conn_t *conn) {
    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return -1;

    /* Read until we have the full header block. */
    for (;;) {
        int n = recv(conn->sock, buf + len, (int)(cap - len - 1), 0);
        if (n <= 0) { free(buf); return -1; }
        len += (size_t)n;
        buf[len] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return -1; }
            buf = nb;
        }
    }

    /* Parse request line: METHOD PATH HTTP/1.x */
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

    /* Headers (between first \r\n and \r\n\r\n) */
    char *hdr_start = line_end + 2;
    char *hdr_end = strstr(buf, "\r\n\r\n");
    size_t hdr_len = (size_t)(hdr_end - hdr_start);
    conn->headers = (char *)malloc(hdr_len + 1);
    memcpy(conn->headers, hdr_start, hdr_len);
    conn->headers[hdr_len] = '\0';

    /* Body (after \r\n\r\n) */
    char *body_start = hdr_end + 4;
    size_t already = len - (size_t)(body_start - buf);

    /* Check Content-Length for remaining body data */
    size_t content_length = 0;
    const char *cl = strstr(conn->headers, "Content-Length:");
    if (!cl) cl = strstr(conn->headers, "content-length:");
    if (cl) content_length = (size_t)atol(cl + 15);

    if (content_length > already) {
        size_t need = content_length;
        char *full_body = (char *)malloc(need + 1);
        if (already > 0) memcpy(full_body, body_start, already);
        size_t got = already;
        while (got < need) {
            int n = recv(conn->sock, full_body + got, (int)(need - got), 0);
            if (n <= 0) break;
            got += (size_t)n;
        }
        full_body[got] = '\0';
        conn->body = full_body;
        conn->body_len = got;
    } else {
        conn->body = (char *)malloc(already + 1);
        if (already > 0) memcpy(conn->body, body_start, already);
        conn->body[already] = '\0';
        conn->body_len = already;
    }

    free(buf);
    return 0;
}

/* ── http.listen(host, port) -> i64 handle ───────────────────────── */

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
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';
    basl_vm_stack_pop_n(vm, arg_count);

    servers_init();

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (strcmp(host_buf, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        addr.sin_addr.s_addr = inet_addr(host_buf);
    }

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) return push_i64(vm, -1, error);

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }
    if (listen(sock, 128) < 0) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }

    int64_t handle = alloc_server(sock);
    if (handle < 0) { close_socket(sock); }
    return push_i64(vm, handle, error);
}

/* ── http.accept(server_handle) -> i64 conn_handle ───────────────── */

static basl_status_t http_accept(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t srv_handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);

    socket_t listener = get_server(srv_handle);
    if (listener == INVALID_SOCK) return push_i64(vm, -1, error);

    socket_t client = accept(listener, NULL, NULL);
    if (client == INVALID_SOCK) return push_i64(vm, -1, error);

    int64_t ch = alloc_client(client);
    if (ch < 0) { close_socket(client); return push_i64(vm, -1, error); }

    http_conn_t *conn = get_client(ch);
    if (parse_incoming_request(conn) != 0) {
        client_free_fields(conn);
        close_socket(conn->sock);
        conn->sock = INVALID_SOCK;
        conn->in_use = 0;
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, ch, error);
}

/* ── http.req_method(conn) -> string ─────────────────────────────── */

static basl_status_t http_req_method(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *m = (c && c->method) ? c->method : "";
    return push_string(vm, m, strlen(m), error);
}

/* ── http.req_path(conn) -> string ───────────────────────────────── */

static basl_status_t http_req_path(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *p = (c && c->path) ? c->path : "";
    return push_string(vm, p, strlen(p), error);
}

/* ── http.req_body(conn) -> string ───────────────────────────────── */

static basl_status_t http_req_body(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *b = (c && c->body) ? c->body : "";
    size_t blen = c ? c->body_len : 0;
    return push_string(vm, b, blen, error);
}

/* ── http.respond(conn, status, headers, body) -> i32 ────────────── */

static basl_status_t http_respond(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    int64_t status_code = get_i64_arg(vm, base, 1);
    const char *hdrs = NULL, *body = NULL;
    size_t hdrs_len = 0, body_len = 0;
    get_string_arg(vm, base, 2, &hdrs, &hdrs_len);
    get_string_arg(vm, base, 3, &body, &body_len);

    /* Copy strings before popping */
    char *hdrs_copy = NULL, *body_copy = NULL;
    if (hdrs_len > 0) {
        hdrs_copy = (char *)malloc(hdrs_len + 1);
        memcpy(hdrs_copy, hdrs, hdrs_len);
        hdrs_copy[hdrs_len] = '\0';
    }
    if (body_len > 0) {
        body_copy = (char *)malloc(body_len + 1);
        memcpy(body_copy, body, body_len);
        body_copy[body_len] = '\0';
    }
    basl_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || c->sock == INVALID_SOCK) {
        free(hdrs_copy); free(body_copy);
        return push_i64(vm, -1, error);
    }

    char resp_hdr[4096];
    int hlen = snprintf(resp_hdr, sizeof(resp_hdr),
        "HTTP/1.1 %d OK\r\n"
        "%s"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        (int)status_code,
        hdrs_copy ? hdrs_copy : "",
        body_len);

    int rc = 0;
    if (send(c->sock, resp_hdr, hlen, 0) < 0) rc = -1;
    if (rc == 0 && body_copy && body_len > 0) {
        if (send(c->sock, body_copy, (int)body_len, 0) < 0) rc = -1;
    }

    free(hdrs_copy);
    free(body_copy);

    /* Close the connection after responding */
    close_socket(c->sock);
    c->sock = INVALID_SOCK;
    client_free_fields(c);
    c->in_use = 0;

    return push_i64(vm, rc, error);
}

/* ── http.close(handle) ──────────────────────────────────────────── */

static basl_status_t http_server_close(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);

    if (handle >= 0 && handle < HTTP_MAX_SERVERS && g_servers[handle].in_use) {
        close_socket(g_servers[handle].listener);
        g_servers[handle].listener = INVALID_SOCK;
        g_servers[handle].in_use = 0;
    }

    basl_value_t nil = basl_nanbox_encode_int(0);
    return basl_vm_stack_push(vm, &nil, error);
}

/* ── Client BASL functions ───────────────────────────────────────── */

static basl_status_t http_get(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url; size_t url_len;
    if (!get_string_arg(vm, base, 0, &url, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    char *url_copy = (char *)malloc(url_len + 1);
    memcpy(url_copy, url, url_len); url_copy[url_len] = '\0';
    basl_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request("GET", url_copy, NULL, NULL, 0, &resp);
    free(url_copy);
    if (rc != 0) { push_string(vm, "", 0, error); return push_i64(vm, -1, error); }

    int status = resp.status_code;
    basl_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    if (st != BASL_STATUS_OK) return st;
    return push_i64(vm, status, error);
}

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
    if (ct && ct_len > 0)
        snprintf(headers, sizeof(headers), "Content-Type: %.*s\r\n", (int)ct_len, ct);
    basl_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request("POST", url_copy, headers, body_copy, body_len, &resp);
    free(url_copy); free(body_copy);
    if (rc != 0) { push_string(vm, "", 0, error); return push_i64(vm, -1, error); }

    int status = resp.status_code;
    basl_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    if (st != BASL_STATUS_OK) return st;
    return push_i64(vm, status, error);
}

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

    char *mc = (char *)malloc(method_len + 1);
    char *uc = (char *)malloc(url_len + 1);
    char *hc = headers_len ? (char *)malloc(headers_len + 1) : NULL;
    char *bc = body_len ? (char *)malloc(body_len + 1) : NULL;
    memcpy(mc, method, method_len); mc[method_len] = '\0';
    memcpy(uc, url, url_len); uc[url_len] = '\0';
    if (hc) { memcpy(hc, headers, headers_len); hc[headers_len] = '\0'; }
    if (bc) { memcpy(bc, body, body_len); bc[body_len] = '\0'; }
    basl_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request(mc, uc, hc, bc, body_len, &resp);
    free(mc); free(uc); free(hc); free(bc);
    if (rc != 0) { push_string(vm, "", 0, error); return push_i64(vm, -1, error); }

    int status = resp.status_code;
    basl_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    if (st != BASL_STATUS_OK) return st;
    return push_i64(vm, status, error);
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int http_1str_params[] = { BASL_TYPE_STRING };
static const int http_2str_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int http_str_i32_params[] = { BASL_TYPE_STRING, BASL_TYPE_I32 };
static const int http_i64_params[] = { BASL_TYPE_I64 };
static const int http_respond_params[] = { BASL_TYPE_I64, BASL_TYPE_I32, BASL_TYPE_STRING, BASL_TYPE_STRING };

static const basl_native_module_function_t http_functions[] = {
    /* Client */
    { "get", 3, http_get, 1, http_1str_params, BASL_TYPE_I32, 2, NULL, 0, NULL, NULL },
    { "post", 4, http_post, 2, http_2str_params, BASL_TYPE_I32, 2, NULL, 0, NULL, NULL },
    { "request", 7, http_request, 2, http_2str_params, BASL_TYPE_I32, 2, NULL, 0, NULL, NULL },
    /* Server */
    { "listen", 6, http_listen, 2, http_str_i32_params, BASL_TYPE_I64, 1, NULL, 0, NULL, NULL },
    { "accept", 6, http_accept, 1, http_i64_params, BASL_TYPE_I64, 1, NULL, 0, NULL, NULL },
    { "req_method", 10, http_req_method, 1, http_i64_params, BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL },
    { "req_path", 8, http_req_path, 1, http_i64_params, BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL },
    { "req_body", 8, http_req_body, 1, http_i64_params, BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL },
    { "respond", 7, http_respond, 4, http_respond_params, BASL_TYPE_I32, 1, NULL, 0, NULL, NULL },
    { "close", 5, http_server_close, 1, http_i64_params, BASL_TYPE_VOID, 0, NULL, 0, NULL, NULL },
};

BASL_API const basl_native_module_t basl_stdlib_http = {
    "http", 4,
    http_functions,
    sizeof(http_functions) / sizeof(http_functions[0]),
    NULL, 0
};
