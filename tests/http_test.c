/* Unit tests for VIGIL http module. */
#include "vigil_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

/* ── Declarations for http.c internals (VIGIL_HTTP_TESTING) ───────── */

typedef struct
{
    char scheme[16];
    char host[256];
    int port;
    char path[2048];
} parsed_url_t;

typedef struct
{
    int status_code;
    char *headers;
    char *body;
    size_t body_len;
} http_response_t;

extern int parse_url(const char *url, parsed_url_t *out);
extern void response_free(http_response_t *r);
extern int socket_request(const char *method, parsed_url_t *url, const char *headers, const char *body, size_t body_len,
                          http_response_t *resp);
extern int do_request(const char *method, const char *url_str, const char *headers, const char *body, size_t body_len,
                      http_response_t *resp);

/* ── URL parsing tests (table-style) ─────────────────────────────── */

TEST(VigilHttpTest, ParseUrlFull)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("https://example.com:8080/api/v1", &u));
    EXPECT_STREQ(u.scheme, "https");
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_EQ(u.port, 8080);
    EXPECT_TRUE(strcmp(u.path, "/api/v1") == 0);
}

TEST(VigilHttpTest, ParseUrlHttpDefault)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://localhost/test", &u));
    EXPECT_STREQ(u.scheme, "http");
    EXPECT_STREQ(u.host, "localhost");
    EXPECT_EQ(u.port, 80);
    EXPECT_TRUE(strcmp(u.path, "/test") == 0);
}

TEST(VigilHttpTest, ParseUrlHttpsDefaultPort)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("https://example.com/secure", &u));
    EXPECT_EQ(u.port, 443);
}

TEST(VigilHttpTest, ParseUrlNoPath)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://example.com", &u));
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_TRUE(strcmp(u.path, "/") == 0);
}

TEST(VigilHttpTest, ParseUrlNoScheme)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("example.com/foo", &u));
    EXPECT_STREQ(u.scheme, "http");
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_TRUE(strcmp(u.path, "/foo") == 0);
}

TEST(VigilHttpTest, ParseUrlLoopback)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://127.0.0.1:9100/data", &u));
    EXPECT_STREQ(u.host, "127.0.0.1");
    EXPECT_EQ(u.port, 9100);
    EXPECT_TRUE(strcmp(u.path, "/data") == 0);
}

/* ── Loopback test server ────────────────────────────────────────── */

#define TEST_PORT 18787

typedef struct
{
    vigil_socket_t listener;
    volatile int ready;
    const char *response; /* full HTTP response to send */
} test_server_t;

static void test_server_func(void *arg)
{
    test_server_t *srv = (test_server_t *)arg;

    if (vigil_platform_tcp_listen("127.0.0.1", TEST_PORT, &srv->listener, NULL) != VIGIL_STATUS_OK)
    {
        srv->listener = VIGIL_INVALID_SOCKET;
        return;
    }

    srv->ready = 1;

    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_accept(srv->listener, &client, NULL) != VIGIL_STATUS_OK)
        return;

    /* Drain the request */
    char buf[4096];
    size_t n = 0;
    vigil_platform_tcp_recv(client, buf, sizeof(buf), &n, NULL);

    /* Send response */
    const char *resp = srv->response;
    vigil_platform_tcp_send(client, resp, strlen(resp), NULL, NULL);
    vigil_platform_tcp_close(client, NULL);
}

static int start_test_server(test_server_t *srv, const char *response, vigil_platform_thread_t **thread)
{
    memset(srv, 0, sizeof(*srv));
    srv->listener = VIGIL_INVALID_SOCKET;
    srv->response = response;

    vigil_platform_net_init(NULL);
    vigil_status_t st = vigil_platform_thread_create(thread, test_server_func, srv, NULL);
    if (st != VIGIL_STATUS_OK)
        return 0;

    for (int i = 0; i < 200 && !srv->ready; i++)
        vigil_platform_thread_sleep(10);
    return srv->ready;
}

static void stop_test_server(test_server_t *srv, vigil_platform_thread_t *thread)
{
    if (srv->listener != VIGIL_INVALID_SOCKET)
        vigil_platform_tcp_close(srv->listener, NULL);
    vigil_platform_thread_join(thread, NULL);
}

/* ── Socket client tests against loopback server ─────────────────── */

TEST(VigilHttpTest, SocketGetBasic)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 200 OK\r\n"
                         "Content-Length: 5\r\n"
                         "\r\n"
                         "hello";

    if (!start_test_server(&srv, canned, &thr))
    {
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

TEST(VigilHttpTest, SocketPostBody)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 201 Created\r\n"
                         "Content-Length: 2\r\n"
                         "\r\n"
                         "ok";

    if (!start_test_server(&srv, canned, &thr))
        return;

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/submit", &url);

    http_response_t resp;
    const char *body = "{\"key\":\"val\"}";
    int rc = socket_request("POST", &url, "Content-Type: application/json\r\n", body, strlen(body), &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 201);
    EXPECT_STREQ(resp.body, "ok");
    response_free(&resp);

    stop_test_server(&srv, thr);
}

TEST(VigilHttpTest, Socket404Response)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 404 Not Found\r\n"
                         "Content-Length: 9\r\n"
                         "\r\n"
                         "not found";

    if (!start_test_server(&srv, canned, &thr))
        return;

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

TEST(VigilHttpTest, SocketEmptyBody)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 204 No Content\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n";

    if (!start_test_server(&srv, canned, &thr))
        return;

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

TEST(VigilHttpTest, SocketConnectionRefused)
{
    parsed_url_t url;
    parse_url("http://127.0.0.1:18788/nothing", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, -1);
}

TEST(VigilHttpTest, SocketRequestLargeHeadersSucceed)
{
    /* Dynamic request buffer accommodates large custom headers. */
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 200 OK\r\n"
                         "Content-Length: 2\r\n"
                         "\r\n"
                         "ok";
    char *headers = NULL;

    if (!start_test_server(&srv, canned, &thr))
        return;

    headers = (char *)malloc(5001);
    ASSERT_TRUE(headers != NULL);
    memset(headers, 'a', 5000);
    headers[0] = 'X';
    headers[1] = ':';
    headers[4998] = '\r';
    headers[4999] = '\n';
    headers[5000] = '\0';

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/large-headers", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, headers, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    if (rc == 0)
        response_free(&resp);

    free(headers);
    stop_test_server(&srv, thr);
}

TEST(VigilHttpTest, ParseUrlUppercaseScheme)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("HTTPS://example.com/path", &u));
    EXPECT_STREQ(u.scheme, "https");
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_EQ(u.port, 443);
}

TEST(VigilHttpTest, ParseUrlPathTooLong)
{
    /* Path longer than 2047 chars must return 0 (error). */
    char url[4096];
    size_t i;
    memcpy(url, "http://example.com/", 19);
    for (i = 19; i < sizeof(url) - 1; i++)
        url[i] = 'a';
    url[sizeof(url) - 1] = '\0';

    parsed_url_t u;
    EXPECT_EQ(parse_url(url, &u), 0);
}

TEST(VigilHttpTest, DoRequestHttpsFallbackFails)
{
    /* Without libcurl, HTTPS via do_request should fail gracefully */
    http_response_t resp;
    int rc = do_request("GET", "https://127.0.0.1:18789/secure", NULL, NULL, 0, &resp);
    /* Either libcurl handles it or it returns -1 (no TLS fallback) */
    if (rc != 0)
    {
        EXPECT_EQ(rc, -1);
    }
    else
    {
        response_free(&resp);
    }
}

/* ── Response free safety ────────────────────────────────────────── */

TEST(VigilHttpTest, ResponseFreeNull)
{
    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    response_free(&resp); /* should not crash */
    EXPECT_EQ(resp.body, NULL);
    EXPECT_EQ(resp.headers, NULL);
}

/* ── Server round-trip tests ──────────────────────────────────────── */

#define SERVER_TEST_PORT 18788

typedef struct
{
    int port;
    volatile int ready;
    char received_method[32];
    char received_path[256];
    char received_body[1024];
} server_test_ctx_t;

static void server_thread_func(void *arg)
{
    server_test_ctx_t *ctx = (server_test_ctx_t *)arg;

    vigil_socket_t listener = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_listen("127.0.0.1", ctx->port, &listener, NULL) != VIGIL_STATUS_OK)
        return;

    ctx->ready = 1;

    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_accept(listener, &client, NULL) != VIGIL_STATUS_OK)
    {
        vigil_platform_tcp_close(listener, NULL);
        return;
    }

    /* Read request */
    char buf[4096];
    size_t len = 0;
    for (;;)
    {
        size_t n = 0;
        if (vigil_platform_tcp_recv(client, buf + len, sizeof(buf) - len - 1, &n, NULL) != VIGIL_STATUS_OK || n == 0)
            break;
        len += n;
        buf[len] = '\0';
        if (strstr(buf, "\r\n\r\n"))
            break;
    }

    /* Parse method and path */
    char *sp1 = strchr(buf, ' ');
    if (sp1)
    {
        size_t mlen = (size_t)(sp1 - buf);
        if (mlen < sizeof(ctx->received_method))
        {
            memcpy(ctx->received_method, buf, mlen);
            ctx->received_method[mlen] = '\0';
        }
        char *sp2 = strchr(sp1 + 1, ' ');
        if (sp2)
        {
            size_t plen = (size_t)(sp2 - sp1 - 1);
            if (plen < sizeof(ctx->received_path))
            {
                memcpy(ctx->received_path, sp1 + 1, plen);
                ctx->received_path[plen] = '\0';
            }
        }
    }

    /* Extract body after \r\n\r\n */
    char *body_start = strstr(buf, "\r\n\r\n");
    if (body_start)
    {
        body_start += 4;
        size_t blen = len - (size_t)(body_start - buf);
        if (blen < sizeof(ctx->received_body))
        {
            memcpy(ctx->received_body, body_start, blen);
            ctx->received_body[blen] = '\0';
        }
    }

    /* Send response */
    const char *response = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: 11\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "hello world";
    vigil_platform_tcp_send(client, response, strlen(response), NULL, NULL);

    vigil_platform_tcp_close(client, NULL);
    vigil_platform_tcp_close(listener, NULL);
}

TEST(VigilHttpTest, ServerRoundTrip)
{
    server_test_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.port = SERVER_TEST_PORT;

    vigil_platform_thread_t *thr = NULL;
    vigil_status_t st = vigil_platform_thread_create(&thr, server_thread_func, &ctx, NULL);
    if (st != VIGIL_STATUS_OK)
        return;

    for (int i = 0; i < 200 && !ctx.ready; i++)
        vigil_platform_thread_sleep(10);
    if (!ctx.ready)
    {
        vigil_platform_thread_join(thr, NULL);
        return;
    }

    /* Use socket_request to talk to our server */
    parsed_url_t url;
    parse_url("http://127.0.0.1:18788/test/path", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_STREQ(resp.body, "hello world");
    response_free(&resp);

    vigil_platform_thread_join(thr, NULL);

    /* Verify the server received the right request */
    EXPECT_STREQ(ctx.received_method, "GET");
    EXPECT_TRUE(strcmp(ctx.received_path, "/test/path") == 0);
}

TEST(VigilHttpTest, ServerPostRoundTrip)
{
    server_test_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.port = SERVER_TEST_PORT + 1;

    vigil_platform_thread_t *thr = NULL;
    vigil_status_t st = vigil_platform_thread_create(&thr, server_thread_func, &ctx, NULL);
    if (st != VIGIL_STATUS_OK)
        return;

    for (int i = 0; i < 200 && !ctx.ready; i++)
        vigil_platform_thread_sleep(10);
    if (!ctx.ready)
    {
        vigil_platform_thread_join(thr, NULL);
        return;
    }

    parsed_url_t url;
    parse_url("http://127.0.0.1:18789/submit", &url);

    const char *body = "test data";
    http_response_t resp;
    int rc = socket_request("POST", &url, "Content-Type: text/plain\r\n", body, strlen(body), &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    response_free(&resp);

    vigil_platform_thread_join(thr, NULL);

    EXPECT_STREQ(ctx.received_method, "POST");
    EXPECT_TRUE(strcmp(ctx.received_path, "/submit") == 0);
}

/* ── Test Registration ───────────────────────────────────────────── */

void register_http_tests(void)
{
    /* URL parsing */
    REGISTER_TEST(VigilHttpTest, ParseUrlFull);
    REGISTER_TEST(VigilHttpTest, ParseUrlHttpDefault);
    REGISTER_TEST(VigilHttpTest, ParseUrlHttpsDefaultPort);
    REGISTER_TEST(VigilHttpTest, ParseUrlNoPath);
    REGISTER_TEST(VigilHttpTest, ParseUrlNoScheme);
    REGISTER_TEST(VigilHttpTest, ParseUrlLoopback);
    /* Socket client */
    REGISTER_TEST(VigilHttpTest, SocketGetBasic);
    REGISTER_TEST(VigilHttpTest, SocketPostBody);
    REGISTER_TEST(VigilHttpTest, Socket404Response);
    REGISTER_TEST(VigilHttpTest, SocketEmptyBody);
    REGISTER_TEST(VigilHttpTest, SocketConnectionRefused);
    REGISTER_TEST(VigilHttpTest, SocketRequestLargeHeadersSucceed);
    REGISTER_TEST(VigilHttpTest, DoRequestHttpsFallbackFails);
    /* URL parsing extras */
    REGISTER_TEST(VigilHttpTest, ParseUrlUppercaseScheme);
    REGISTER_TEST(VigilHttpTest, ParseUrlPathTooLong);
    /* Misc */
    REGISTER_TEST(VigilHttpTest, ResponseFreeNull);
    /* Server */
    REGISTER_TEST(VigilHttpTest, ServerRoundTrip);
    REGISTER_TEST(VigilHttpTest, ServerPostRoundTrip);
}
