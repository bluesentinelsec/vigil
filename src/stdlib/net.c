/* BASL standard library: net module.
 *
 * TCP and UDP socket support with cross-platform compatibility.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define SOCK_ERR SOCKET_ERROR
#define close_socket closesocket
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#define SOCK_ERR (-1)
#define close_socket close
#endif

/* ── Socket storage ──────────────────────────────────────────────── */

#define MAX_SOCKETS 256
static socket_t g_sockets[MAX_SOCKETS];
static int g_socket_types[MAX_SOCKETS]; /* 0=unused, 1=tcp, 2=udp */
static int g_initialized = 0;

static void net_init(void) {
    if (g_initialized) return;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    for (int i = 0; i < MAX_SOCKETS; i++) {
        g_sockets[i] = INVALID_SOCK;
        g_socket_types[i] = 0;
    }
    g_initialized = 1;
}

static int64_t alloc_socket(socket_t s, int type) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_socket_types[i] == 0) {
            g_sockets[i] = s;
            g_socket_types[i] = type;
            return i;
        }
    }
    return -1;
}

static socket_t get_socket(int64_t handle) {
    if (handle < 0 || handle >= MAX_SOCKETS) return INVALID_SOCK;
    if (g_socket_types[handle] == 0) return INVALID_SOCK;
    return g_sockets[handle];
}

static void free_socket(int64_t handle) {
    if (handle < 0 || handle >= MAX_SOCKETS) return;
    if (g_socket_types[handle] != 0) {
        close_socket(g_sockets[handle]);
        g_sockets[handle] = INVALID_SOCK;
        g_socket_types[handle] = 0;
    }
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool get_string_arg(basl_vm_t *vm, size_t base, size_t idx,
                           const char **out, size_t *out_len) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    if (!basl_nanbox_is_object(v)) return false;
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(v);
    if (!obj || basl_object_type(obj) != BASL_OBJECT_STRING) return false;
    *out = basl_string_object_c_str(obj);
    *out_len = basl_string_object_length(obj);
    return true;
}

static int64_t get_i64_arg(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    if (basl_nanbox_is_int(v)) return basl_nanbox_decode_int(v);
    return 0;
}

static int32_t get_i32_arg(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    return basl_nanbox_decode_i32(v);
}

static basl_status_t push_i64(basl_vm_t *vm, int64_t val, basl_error_t *error) {
    basl_value_t v = basl_nanbox_encode_int(val);
    return basl_vm_stack_push(vm, &v, error);
}

static basl_status_t push_i32(basl_vm_t *vm, int32_t val, basl_error_t *error) {
    basl_value_t v = basl_nanbox_encode_i32(val);
    return basl_vm_stack_push(vm, &v, error);
}

static basl_status_t push_string(basl_vm_t *vm, const char *str, size_t len,
                                  basl_error_t *error) {
    basl_object_t *obj;
    basl_value_t v;
    basl_status_t s = basl_string_object_new(basl_vm_runtime(vm), str, len, &obj, error);
    if (s != BASL_STATUS_OK) return s;
    v = basl_nanbox_encode_object(obj);
    return basl_vm_stack_push(vm, &v, error);
}

/* ── TCP Functions ───────────────────────────────────────────────── */

/* net.tcp_listen(host: string, port: i32) -> i64 */
static basl_status_t net_tcp_listen(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *host;
    size_t host_len;
    int32_t port;
    socket_t sock;
    struct sockaddr_in addr;
    int opt = 1;
    char host_buf[256];

    net_init();

    if (!get_string_arg(vm, base, 0, &host, &host_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    port = get_i32_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) return push_i64(vm, -1, error);

#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (host_len >= sizeof(host_buf)) host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';

    if (host_len == 0 || strcmp(host_buf, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        addr.sin_addr.s_addr = inet_addr(host_buf);
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCK_ERR) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }

    if (listen(sock, 128) == SOCK_ERR) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }

    int64_t handle = alloc_socket(sock, 1);
    if (handle < 0) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.tcp_accept(listener: i64) -> i64 */
static basl_status_t net_tcp_accept(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t listener = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);

    socket_t listen_sock = get_socket(listener);
    if (listen_sock == INVALID_SOCK) return push_i64(vm, -1, error);

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    socket_t client = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
    if (client == INVALID_SOCK) return push_i64(vm, -1, error);

    int64_t handle = alloc_socket(client, 1);
    if (handle < 0) {
        close_socket(client);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.tcp_connect(host: string, port: i32) -> i64 */
static basl_status_t net_tcp_connect(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *host;
    size_t host_len;
    int32_t port;
    socket_t sock;
    struct sockaddr_in addr;
    struct hostent *he;
    char host_buf[256];

    net_init();

    if (!get_string_arg(vm, base, 0, &host, &host_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    port = get_i32_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);

    if (host_len >= sizeof(host_buf)) host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) return push_i64(vm, -1, error);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    /* Try as IP address first */
    addr.sin_addr.s_addr = inet_addr(host_buf);
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        /* Resolve hostname */
        he = gethostbyname(host_buf);
        if (!he) {
            close_socket(sock);
            return push_i64(vm, -1, error);
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCK_ERR) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }

    int64_t handle = alloc_socket(sock, 1);
    if (handle < 0) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.read(sock: i64, max_bytes: i32) -> string */
static basl_status_t net_read(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int32_t max_bytes = get_i32_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);

    socket_t sock = get_socket(handle);
    if (sock == INVALID_SOCK) return push_string(vm, "", 0, error);

    if (max_bytes <= 0) max_bytes = 4096;
    if (max_bytes > 1024 * 1024) max_bytes = 1024 * 1024;

    char *buf = (char *)malloc((size_t)max_bytes);
    if (!buf) return push_string(vm, "", 0, error);

    int n = recv(sock, buf, (size_t)max_bytes, 0);
    if (n <= 0) {
        free(buf);
        return push_string(vm, "", 0, error);
    }

    basl_status_t s = push_string(vm, buf, (size_t)n, error);
    free(buf);
    return s;
}

/* net.write(sock: i64, data: string) -> i32 */
static basl_status_t net_write(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    const char *data;
    size_t data_len;

    if (!get_string_arg(vm, base, 1, &data, &data_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i32(vm, -1, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    socket_t sock = get_socket(handle);
    if (sock == INVALID_SOCK) return push_i32(vm, -1, error);

    int n = send(sock, data, (int)data_len, 0);
    return push_i32(vm, n, error);
}

/* net.close(sock: i64) */
static basl_status_t net_close(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    free_socket(handle);
    (void)error;
    return BASL_STATUS_OK;
}

/* ── UDP Functions ───────────────────────────────────────────────── */

/* net.udp_bind(host: string, port: i32) -> i64 */
static basl_status_t net_udp_bind(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *host;
    size_t host_len;
    int32_t port;
    socket_t sock;
    struct sockaddr_in addr;
    char host_buf[256];

    net_init();

    if (!get_string_arg(vm, base, 0, &host, &host_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    port = get_i32_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCK) return push_i64(vm, -1, error);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (host_len >= sizeof(host_buf)) host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';

    if (host_len == 0 || strcmp(host_buf, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        addr.sin_addr.s_addr = inet_addr(host_buf);
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCK_ERR) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }

    int64_t handle = alloc_socket(sock, 2);
    if (handle < 0) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.udp_new() -> i64 */
static basl_status_t net_udp_new(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    (void)arg_count;
    net_init();

    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCK) return push_i64(vm, -1, error);

    int64_t handle = alloc_socket(sock, 2);
    if (handle < 0) {
        close_socket(sock);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.udp_send(sock: i64, host: string, port: i32, data: string) -> i32 */
static basl_status_t net_udp_send(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    const char *host, *data;
    size_t host_len, data_len;
    int32_t port;
    struct sockaddr_in addr;
    char host_buf[256];

    if (!get_string_arg(vm, base, 1, &host, &host_len) ||
        !get_string_arg(vm, base, 3, &data, &data_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i32(vm, -1, error);
    }
    port = get_i32_arg(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);

    socket_t sock = get_socket(handle);
    if (sock == INVALID_SOCK) return push_i32(vm, -1, error);

    if (host_len >= sizeof(host_buf)) host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr(host_buf);

    if (addr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *he = gethostbyname(host_buf);
        if (!he) return push_i32(vm, -1, error);
        memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }

    int n = sendto(sock, data, (int)data_len, 0, (struct sockaddr *)&addr, sizeof(addr));
    return push_i32(vm, n, error);
}

/* net.udp_recv(sock: i64, max_bytes: i32) -> string */
static basl_status_t net_udp_recv(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int32_t max_bytes = get_i32_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);

    socket_t sock = get_socket(handle);
    if (sock == INVALID_SOCK) return push_string(vm, "", 0, error);

    if (max_bytes <= 0) max_bytes = 4096;
    if (max_bytes > 65536) max_bytes = 65536;

    char *buf = (char *)malloc((size_t)max_bytes);
    if (!buf) return push_string(vm, "", 0, error);

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int n = recvfrom(sock, buf, (size_t)max_bytes, 0, (struct sockaddr *)&from, &from_len);
    if (n <= 0) {
        free(buf);
        return push_string(vm, "", 0, error);
    }

    basl_status_t s = push_string(vm, buf, (size_t)n, error);
    free(buf);
    return s;
}

/* net.set_timeout(sock: i64, ms: i32) -> bool */
static basl_status_t net_set_timeout(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int32_t ms = get_i32_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);

    socket_t sock = get_socket(handle);
    if (sock == INVALID_SOCK) {
        basl_value_t v;
        basl_value_init_bool(&v, 0);
        return basl_vm_stack_push(vm, &v, error);
    }

#ifdef _WIN32
    DWORD timeout = (DWORD)ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    basl_value_t v;
    basl_value_init_bool(&v, 1);
    return basl_vm_stack_push(vm, &v, error);
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_i32_param[] = {BASL_TYPE_STRING, BASL_TYPE_I32};
static const int i64_param[] = {BASL_TYPE_I64};
static const int i64_i32_param[] = {BASL_TYPE_I64, BASL_TYPE_I32};
static const int i64_str_param[] = {BASL_TYPE_I64, BASL_TYPE_STRING};
static const int udp_send_param[] = {BASL_TYPE_I64, BASL_TYPE_STRING, BASL_TYPE_I32, BASL_TYPE_STRING};

static const basl_native_module_function_t net_functions[] = {
    {"tcp_listen", 10U, net_tcp_listen, 2U, str_i32_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"tcp_accept", 10U, net_tcp_accept, 1U, i64_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"tcp_connect", 11U, net_tcp_connect, 2U, str_i32_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"read", 4U, net_read, 2U, i64_i32_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"write", 5U, net_write, 2U, i64_str_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"close", 5U, net_close, 1U, i64_param, BASL_TYPE_VOID, 0U, NULL, 0, NULL, NULL},
    {"udp_bind", 8U, net_udp_bind, 2U, str_i32_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"udp_new", 7U, net_udp_new, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"udp_send", 8U, net_udp_send, 4U, udp_send_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"udp_recv", 8U, net_udp_recv, 2U, i64_i32_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"set_timeout", 11U, net_set_timeout, 2U, i64_i32_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
};

#define NET_FUNCTION_COUNT (sizeof(net_functions) / sizeof(net_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_net = {
    "net", 3U,
    net_functions, NET_FUNCTION_COUNT,
    NULL, 0U
};
