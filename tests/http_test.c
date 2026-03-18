/* Unit tests for BASL http module. */
#include "basl_test.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "platform/platform.h"

/* ── Platform socket headers (mirrors http.c) ────────────────────── */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
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

/* ── Declarations for http.c internals (BASL_HTTP_TESTING) ───────── */

typedef struct {
    char scheme[16];
    char host[256];
    int port;
    char path[2048];
} parsed_url_t;

typedef struct {
    int status_code;
    char *headers;
    char *body;
    size_t body_len;
} http_response_t;

extern int parse_url(const char *url, parsed_url_t *out);
extern void response_free(http_response_t *r);
extern int socket_request(const char *method, parsed_url_t *url,
                          const char *headers, const char *body, size_t body_len,
                          http_response_t *resp);
extern int do_request(const char *method, const char *url_str,
                      const char *headers, const char *body, size_t body_len,
                      http_response_t *resp);

/* ── URL parsing tests (table-style) ─────────────────────────────── */

TEST(BaslHttpTest, ParseUrlFull) {
    parsed_url_t u;
    ASSERT_TRUE(parse_url("https://example.com:8080/api/v1", &u));
    EXPECT_STREQ(u.scheme, "https");
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_EQ(u.port, 8080);
    EXPECT_STREQ(u.path, "/api/v1");
}

TEST(BaslHttpTest, ParseUrlHttpDefault) {
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://localhost/test", &u));
    EXPECT_STREQ(u.scheme, "http");
    EXPECT_STREQ(u.host, "localhost");
    EXPECT_EQ(u.port, 80);
    EXPECT_STREQ(u.path, "/test");
}

TEST(BaslHttpTest, ParseUrlHttpsDefaultPort) {
    parsed_url_t u;
    ASSERT_TRUE(parse_url("https://example.com/secure", &u));
    EXPECT_EQ(u.port, 443);
}

TEST(BaslHttpTest, ParseUrlNoPath) {
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://example.com", &u));
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_STREQ(u.path, "/");
}

TEST(BaslHttpTest, ParseUrlNoScheme) {
    parsed_url_t u;
    ASSERT_TRUE(parse_url("example.com/foo", &u));
    EXPECT_STREQ(u.scheme, "http");
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_STREQ(u.path, "/foo");
}

TEST(BaslHttpTest, ParseUrlLoopback) {
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://127.0.0.1:9100/data", &u));
    EXPECT_STREQ(u.host, "127.0.0.1");
    EXPECT_EQ(u.port, 9100);
    EXPECT_STREQ(u.path, "/data");
}

/* ── Loopback test server ────────────────────────────────────────── */

#define TEST_PORT 18787

typedef struct {
    socket_t listener;
    volatile int ready;
    const char *response;  /* full HTTP response to send */
} test_server_t;

static void test_server_func(void *arg) {
    test_server_t *srv = (test_server_t *)arg;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(TEST_PORT);

    srv->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listener == INVALID_SOCK) return;

    int opt = 1;
    setsockopt(srv->listener, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    if (bind(srv->listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close_socket(srv->listener);
        srv->listener = INVALID_SOCK;
        return;
    }
    if (listen(srv->listener, 1) < 0) {
        close_socket(srv->listener);
        srv->listener = INVALID_SOCK;
        return;
    }

    srv->ready = 1;

    /* Accept one connection, send canned response, close. */
    socket_t client = accept(srv->listener, NULL, NULL);
    if (client == INVALID_SOCK) return;

    /* Drain the request */
    char buf[4096];
    recv(client, buf, sizeof(buf), 0);

    /* Send response */
    const char *resp = srv->response;
    send(client, resp, (int)strlen(resp), 0);
    close_socket(client);
}

static int start_test_server(test_server_t *srv, const char *response,
                             basl_platform_thread_t **thread) {
    memset(srv, 0, sizeof(*srv));
    srv->listener = INVALID_SOCK;
    srv->response = response;

    basl_status_t st = basl_platform_thread_create(thread, test_server_func,
                                                    srv, NULL);
    if (st != BASL_STATUS_OK) return 0;

    /* Wait for server to be ready */
    for (int i = 0; i < 200 && !srv->ready; i++) {
        basl_platform_thread_sleep(10);
    }
    return srv->ready;
}

static void stop_test_server(test_server_t *srv,
                             basl_platform_thread_t *thread) {
    if (srv->listener != INVALID_SOCK) close_socket(srv->listener);
    basl_platform_thread_join(thread, NULL);
}

/* ── Socket client tests against loopback server ─────────────────── */

TEST(BaslHttpTest, SocketGetBasic) {
    test_server_t srv;
    basl_platform_thread_t *thr = NULL;
    const char *canned =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    if (!start_test_server(&srv, canned, &thr)) {
        /* Port busy — skip gracefully */
        return;
    }

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/test", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_STREQ(resp.body, "hello");
    EXPECT_EQ(resp.body_len, 5);
    response_free(&resp);

    stop_test_server(&srv, thr);
}

TEST(BaslHttpTest, SocketPostBody) {
    test_server_t srv;
    basl_platform_thread_t *thr = NULL;
    const char *canned =
        "HTTP/1.1 201 Created\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "ok";

    if (!start_test_server(&srv, canned, &thr)) return;

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/submit", &url);

    http_response_t resp;
    const char *body = "{\"key\":\"val\"}";
    int rc = socket_request("POST", &url, "Content-Type: application/json\r\n",
                            body, strlen(body), &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 201);
    EXPECT_STREQ(resp.body, "ok");
    response_free(&resp);

    stop_test_server(&srv, thr);
}

TEST(BaslHttpTest, Socket404Response) {
    test_server_t srv;
    basl_platform_thread_t *thr = NULL;
    const char *canned =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 9\r\n"
        "\r\n"
        "not found";

    if (!start_test_server(&srv, canned, &thr)) return;

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/missing", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 404);
    EXPECT_STREQ(resp.body, "not found");
    response_free(&resp);

    stop_test_server(&srv, thr);
}

TEST(BaslHttpTest, SocketEmptyBody) {
    test_server_t srv;
    basl_platform_thread_t *thr = NULL;
    const char *canned =
        "HTTP/1.1 204 No Content\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    if (!start_test_server(&srv, canned, &thr)) return;

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/empty", &url);

    http_response_t resp;
    int rc = socket_request("DELETE", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 204);
    EXPECT_EQ(resp.body_len, 0);
    response_free(&resp);

    stop_test_server(&srv, thr);
}

TEST(BaslHttpTest, SocketConnectionRefused) {
    parsed_url_t url;
    parse_url("http://127.0.0.1:18788/nothing", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, -1);
}

TEST(BaslHttpTest, DoRequestHttpsFallbackFails) {
    /* Without libcurl, HTTPS via do_request should fail gracefully */
    http_response_t resp;
    int rc = do_request("GET", "https://127.0.0.1:18789/secure",
                        NULL, NULL, 0, &resp);
    /* Either libcurl handles it or it returns -1 (no TLS fallback) */
    if (rc != 0) {
        EXPECT_EQ(rc, -1);
    } else {
        response_free(&resp);
    }
}

/* ── Response free safety ────────────────────────────────────────── */

TEST(BaslHttpTest, ResponseFreeNull) {
    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    response_free(&resp); /* should not crash */
    EXPECT_EQ(resp.body, NULL);
    EXPECT_EQ(resp.headers, NULL);
}

/* ── Test Registration ───────────────────────────────────────────── */

void register_http_tests(void) {
    /* URL parsing */
    REGISTER_TEST(BaslHttpTest, ParseUrlFull);
    REGISTER_TEST(BaslHttpTest, ParseUrlHttpDefault);
    REGISTER_TEST(BaslHttpTest, ParseUrlHttpsDefaultPort);
    REGISTER_TEST(BaslHttpTest, ParseUrlNoPath);
    REGISTER_TEST(BaslHttpTest, ParseUrlNoScheme);
    REGISTER_TEST(BaslHttpTest, ParseUrlLoopback);
    /* Socket client */
    REGISTER_TEST(BaslHttpTest, SocketGetBasic);
    REGISTER_TEST(BaslHttpTest, SocketPostBody);
    REGISTER_TEST(BaslHttpTest, Socket404Response);
    REGISTER_TEST(BaslHttpTest, SocketEmptyBody);
    REGISTER_TEST(BaslHttpTest, SocketConnectionRefused);
    REGISTER_TEST(BaslHttpTest, DoRequestHttpsFallbackFails);
    /* Misc */
    REGISTER_TEST(BaslHttpTest, ResponseFreeNull);
}
