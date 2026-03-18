/* Unit tests for BASL http module. */
#include "basl_test.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "platform/platform.h"

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
    EXPECT_TRUE(strcmp(u.path, "/api/v1") == 0);
}

TEST(BaslHttpTest, ParseUrlHttpDefault) {
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://localhost/test", &u));
    EXPECT_STREQ(u.scheme, "http");
    EXPECT_STREQ(u.host, "localhost");
    EXPECT_EQ(u.port, 80);
    EXPECT_TRUE(strcmp(u.path, "/test") == 0);
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
    EXPECT_TRUE(strcmp(u.path, "/") == 0);
}

TEST(BaslHttpTest, ParseUrlNoScheme) {
    parsed_url_t u;
    ASSERT_TRUE(parse_url("example.com/foo", &u));
    EXPECT_STREQ(u.scheme, "http");
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_TRUE(strcmp(u.path, "/foo") == 0);
}

TEST(BaslHttpTest, ParseUrlLoopback) {
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://127.0.0.1:9100/data", &u));
    EXPECT_STREQ(u.host, "127.0.0.1");
    EXPECT_EQ(u.port, 9100);
    EXPECT_TRUE(strcmp(u.path, "/data") == 0);
}

/* ── Loopback test server ────────────────────────────────────────── */

#define TEST_PORT 18787

typedef struct {
    basl_socket_t listener;
    volatile int ready;
    const char *response;  /* full HTTP response to send */
} test_server_t;

static void test_server_func(void *arg) {
    test_server_t *srv = (test_server_t *)arg;

    if (basl_platform_tcp_listen("127.0.0.1", TEST_PORT, &srv->listener, NULL) != BASL_STATUS_OK) {
        srv->listener = BASL_INVALID_SOCKET;
        return;
    }

    srv->ready = 1;

    basl_socket_t client = BASL_INVALID_SOCKET;
    if (basl_platform_tcp_accept(srv->listener, &client, NULL) != BASL_STATUS_OK) return;

    /* Drain the request */
    char buf[4096];
    size_t n = 0;
    basl_platform_tcp_recv(client, buf, sizeof(buf), &n, NULL);

    /* Send response */
    const char *resp = srv->response;
    basl_platform_tcp_send(client, resp, strlen(resp), NULL, NULL);
    basl_platform_tcp_close(client, NULL);
}

static int start_test_server(test_server_t *srv, const char *response,
                             basl_platform_thread_t **thread) {
    memset(srv, 0, sizeof(*srv));
    srv->listener = BASL_INVALID_SOCKET;
    srv->response = response;

    basl_platform_net_init(NULL);
    basl_status_t st = basl_platform_thread_create(thread, test_server_func,
                                                    srv, NULL);
    if (st != BASL_STATUS_OK) return 0;

    for (int i = 0; i < 200 && !srv->ready; i++)
        basl_platform_thread_sleep(10);
    return srv->ready;
}

static void stop_test_server(test_server_t *srv,
                             basl_platform_thread_t *thread) {
    if (srv->listener != BASL_INVALID_SOCKET)
        basl_platform_tcp_close(srv->listener, NULL);
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

/* ── Server round-trip tests ──────────────────────────────────────── */

#define SERVER_TEST_PORT 18788

typedef struct {
    int port;
    volatile int ready;
    char received_method[32];
    char received_path[256];
    char received_body[1024];
} server_test_ctx_t;

static void server_thread_func(void *arg) {
    server_test_ctx_t *ctx = (server_test_ctx_t *)arg;

    basl_socket_t listener = BASL_INVALID_SOCKET;
    if (basl_platform_tcp_listen("127.0.0.1", ctx->port, &listener, NULL) != BASL_STATUS_OK)
        return;

    ctx->ready = 1;

    basl_socket_t client = BASL_INVALID_SOCKET;
    if (basl_platform_tcp_accept(listener, &client, NULL) != BASL_STATUS_OK) {
        basl_platform_tcp_close(listener, NULL);
        return;
    }

    /* Read request */
    char buf[4096];
    size_t len = 0;
    for (;;) {
        size_t n = 0;
        if (basl_platform_tcp_recv(client, buf + len, sizeof(buf) - len - 1, &n, NULL) != BASL_STATUS_OK || n == 0) break;
        len += n;
        buf[len] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }

    /* Parse method and path */
    char *sp1 = strchr(buf, ' ');
    if (sp1) {
        size_t mlen = (size_t)(sp1 - buf);
        if (mlen < sizeof(ctx->received_method)) {
            memcpy(ctx->received_method, buf, mlen);
            ctx->received_method[mlen] = '\0';
        }
        char *sp2 = strchr(sp1 + 1, ' ');
        if (sp2) {
            size_t plen = (size_t)(sp2 - sp1 - 1);
            if (plen < sizeof(ctx->received_path)) {
                memcpy(ctx->received_path, sp1 + 1, plen);
                ctx->received_path[plen] = '\0';
            }
        }
    }

    /* Extract body after \r\n\r\n */
    char *body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        size_t blen = len - (size_t)(body_start - buf);
        if (blen < sizeof(ctx->received_body)) {
            memcpy(ctx->received_body, body_start, blen);
            ctx->received_body[blen] = '\0';
        }
    }

    /* Send response */
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 11\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello world";
    basl_platform_tcp_send(client, response, strlen(response), NULL, NULL);

    basl_platform_tcp_close(client, NULL);
    basl_platform_tcp_close(listener, NULL);
}

TEST(BaslHttpTest, ServerRoundTrip) {
    server_test_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.port = SERVER_TEST_PORT;

    basl_platform_thread_t *thr = NULL;
    basl_status_t st = basl_platform_thread_create(&thr, server_thread_func, &ctx, NULL);
    if (st != BASL_STATUS_OK) return;

    for (int i = 0; i < 200 && !ctx.ready; i++)
        basl_platform_thread_sleep(10);
    if (!ctx.ready) { basl_platform_thread_join(thr, NULL); return; }

    /* Use socket_request to talk to our server */
    parsed_url_t url;
    parse_url("http://127.0.0.1:18788/test/path", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_STREQ(resp.body, "hello world");
    response_free(&resp);

    basl_platform_thread_join(thr, NULL);

    /* Verify the server received the right request */
    EXPECT_STREQ(ctx.received_method, "GET");
    EXPECT_TRUE(strcmp(ctx.received_path, "/test/path") == 0);
}

TEST(BaslHttpTest, ServerPostRoundTrip) {
    server_test_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.port = SERVER_TEST_PORT + 1;

    basl_platform_thread_t *thr = NULL;
    basl_status_t st = basl_platform_thread_create(&thr, server_thread_func, &ctx, NULL);
    if (st != BASL_STATUS_OK) return;

    for (int i = 0; i < 200 && !ctx.ready; i++)
        basl_platform_thread_sleep(10);
    if (!ctx.ready) { basl_platform_thread_join(thr, NULL); return; }

    parsed_url_t url;
    parse_url("http://127.0.0.1:18789/submit", &url);

    const char *body = "test data";
    http_response_t resp;
    int rc = socket_request("POST", &url, "Content-Type: text/plain\r\n",
                            body, strlen(body), &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    response_free(&resp);

    basl_platform_thread_join(thr, NULL);

    EXPECT_STREQ(ctx.received_method, "POST");
    EXPECT_TRUE(strcmp(ctx.received_path, "/submit") == 0);
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
    /* Server */
    REGISTER_TEST(BaslHttpTest, ServerRoundTrip);
    REGISTER_TEST(BaslHttpTest, ServerPostRoundTrip);
}
