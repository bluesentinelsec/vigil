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
extern void cookie_jar_append(char **jar, const char *val, size_t vlen);
extern int hdr_name_matches(const char *line, const char *name, size_t namelen);
extern void collect_cookies(const char *hdrs, char **jar);
extern char *build_request_headers(const char *cookie_jar, const char *existing);
extern int redirect_changes_method(int code);

#ifdef VIGIL_ENABLE_BEARSSL_TLS
extern int bearssl_https_insecure_request(const char *method, const parsed_url_t *url,
                                          const char *headers, const char *body,
                                          size_t body_len, http_response_t *resp);
#endif

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
    int port;             /* 0 = use TEST_PORT */
} test_server_t;

static void test_server_func(void *arg)
{
    test_server_t *srv = (test_server_t *)arg;
    int listen_port = srv->port > 0 ? srv->port : TEST_PORT;

    if (vigil_platform_tcp_listen("127.0.0.1", listen_port, &srv->listener, NULL) != VIGIL_STATUS_OK)
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

static int start_test_server_port(test_server_t *srv, const char *response, int port,
                                   vigil_platform_thread_t **thread)
{
    memset(srv, 0, sizeof(*srv));
    srv->listener = VIGIL_INVALID_SOCKET;
    srv->response = response;
    srv->port = port;

    vigil_platform_net_init(NULL);
    vigil_status_t st = vigil_platform_thread_create(thread, test_server_func, srv, NULL);
    if (st != VIGIL_STATUS_OK)
        return 0;

    for (int i = 0; i < 200 && !srv->ready; i++)
        vigil_platform_thread_sleep(10);
    return srv->ready;
}

static int start_test_server(test_server_t *srv, const char *response, vigil_platform_thread_t **thread)
{
    return start_test_server_port(srv, response, TEST_PORT, thread);
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
    EXPECT_TRUE(parse_url("HTTPS://example.com/path", &u));
    EXPECT_STREQ(u.scheme, "https");
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

/* ── Cookie jar / helper unit tests ──────────────────────────────── */

TEST(VigilHttpTest, CookieJarAppend)
{
    char *jar = NULL;
    cookie_jar_append(&jar, "a=1", 3);
    EXPECT_TRUE(jar != NULL);
    EXPECT_STREQ(jar, "a=1");
    /* Second append adds "; " separator; trailing spaces trimmed */
    cookie_jar_append(&jar, "b=2  ", 5);
    EXPECT_TRUE(strstr(jar, "a=1; b=2") != NULL);
    free(jar);
}

TEST(VigilHttpTest, CookieJarAppendNoOp)
{
    /* Zero-length value leaves jar unchanged */
    char *jar = NULL;
    cookie_jar_append(&jar, "", 0);
    EXPECT_EQ(jar, NULL);
    cookie_jar_append(&jar, "a=1", 3);
    char *prev = jar;
    cookie_jar_append(&jar, "", 0);
    EXPECT_EQ(jar, prev);
    free(jar);
}

TEST(VigilHttpTest, HdrNameMatches)
{
    EXPECT_EQ(hdr_name_matches("Set-Cookie: a=1", "set-cookie", 10), 1);
    EXPECT_EQ(hdr_name_matches("set-cookie: b=2", "set-cookie", 10), 1);
    EXPECT_EQ(hdr_name_matches("Content-Type: text/plain", "set-cookie", 10), 0);
    EXPECT_EQ(hdr_name_matches("Short", "set-cookie", 10), 0);
}

TEST(VigilHttpTest, CollectCookiesFromHeaders)
{
    const char *hdrs = "HTTP/1.1 200 OK\r\n"
                       "Set-Cookie: session=abc\r\n"
                       "Content-Type: text/plain\r\n"
                       "Set-Cookie: user=bob; Path=/; HttpOnly\r\n"
                       "\r\n";
    char *jar = NULL;
    collect_cookies(hdrs, &jar);
    EXPECT_TRUE(jar != NULL);
    EXPECT_TRUE(strstr(jar, "session=abc") != NULL);
    EXPECT_TRUE(strstr(jar, "user=bob") != NULL);
    /* Directive stripped — no "Path" in jar */
    EXPECT_EQ(strstr(jar, "Path"), NULL);
    free(jar);
}

TEST(VigilHttpTest, CollectCookiesEmpty)
{
    char *jar = NULL;
    collect_cookies(NULL, &jar);
    EXPECT_EQ(jar, NULL);
    collect_cookies("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", &jar);
    EXPECT_EQ(jar, NULL); /* no Set-Cookie headers */
}

TEST(VigilHttpTest, BuildRequestHeadersWithCookies)
{
    char *hdrs = build_request_headers("session=abc", "X-Custom: val\r\n");
    ASSERT_TRUE(hdrs != NULL);
    EXPECT_TRUE(strstr(hdrs, "Cookie: session=abc") != NULL);
    EXPECT_TRUE(strstr(hdrs, "X-Custom: val") != NULL);
    free(hdrs);
}

TEST(VigilHttpTest, BuildRequestHeadersNoCookies)
{
    /* NULL or empty cookie jar returns NULL */
    EXPECT_EQ(build_request_headers(NULL, "X-Header: foo\r\n"), NULL);
    EXPECT_EQ(build_request_headers("", "X-Header: foo\r\n"), NULL);
}

TEST(VigilHttpTest, RedirectChangesMethodTrue)
{
    EXPECT_EQ(redirect_changes_method(301), 1);
    EXPECT_EQ(redirect_changes_method(302), 1);
    EXPECT_EQ(redirect_changes_method(303), 1);
}

TEST(VigilHttpTest, RedirectChangesMethodFalse)
{
    EXPECT_EQ(redirect_changes_method(307), 0);
    EXPECT_EQ(redirect_changes_method(308), 0);
    EXPECT_EQ(redirect_changes_method(200), 0);
}

/* ── do_request loopback tests ───────────────────────────────────── */

#define DO_REQ_PORT 18792
#define DO_REQ_REDIR_PORT 18793
#define DO_REQ_DEST_PORT 18794

TEST(VigilHttpTest, DoRequestLoopback)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 200 OK\r\n"
                         "Set-Cookie: tok=xyz\r\n"
                         "Content-Length: 5\r\n"
                         "\r\n"
                         "hello";

    if (!start_test_server_port(&srv, canned, DO_REQ_PORT, &thr))
        return;

    http_response_t resp;
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/test", DO_REQ_PORT);
    int rc = do_request("GET", url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    if (rc == 0)
    {
        EXPECT_EQ(resp.status_code, 200);
        response_free(&resp);
    }
    stop_test_server(&srv, thr);
}

TEST(VigilHttpTest, DoRequestFollowsRedirect)
{
    test_server_t srv_redir, srv_dest;
    vigil_platform_thread_t *thr_redir = NULL, *thr_dest = NULL;

    char redir_body[256];
    snprintf(redir_body, sizeof(redir_body),
             "HTTP/1.1 302 Found\r\n"
             "Location: http://127.0.0.1:%d/dest\r\n"
             "Set-Cookie: redir=1\r\n"
             "Content-Length: 0\r\n"
             "\r\n",
             DO_REQ_DEST_PORT);

    const char *dest_resp = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 2\r\n"
                            "\r\n"
                            "ok";

    if (!start_test_server_port(&srv_redir, redir_body, DO_REQ_REDIR_PORT, &thr_redir))
        return;
    if (!start_test_server_port(&srv_dest, dest_resp, DO_REQ_DEST_PORT, &thr_dest))
    {
        stop_test_server(&srv_redir, thr_redir);
        return;
    }

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/start", DO_REQ_REDIR_PORT);

    http_response_t resp;
    int rc = do_request("POST", url, NULL, "data", 4, &resp);
    EXPECT_EQ(rc, 0);
    if (rc == 0)
    {
        EXPECT_EQ(resp.status_code, 200);
        response_free(&resp);
    }

    stop_test_server(&srv_redir, thr_redir);
    stop_test_server(&srv_dest, thr_dest);
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

/* ── BearSSL TLS loopback tests ──────────────────────────────────── */

#if defined(VIGIL_ENABLE_BEARSSL_TLS) && defined(VIGIL_TLS_TEST_CERT_AVAILABLE)
#include "bearssl.h"
#include "http_tls_test_cert.h"

#define TLS_TEST_PORT 18790

/* Socket callbacks reused by the in-process TLS test server. */
static int tls_srv_read_cb(void *ctx, unsigned char *buf, size_t len)
{
    vigil_socket_t sock = *(vigil_socket_t *)ctx;
    size_t n = 0;
    vigil_status_t st = vigil_platform_tcp_recv(sock, buf, len, &n, NULL);
    return (st != VIGIL_STATUS_OK || n == 0) ? -1 : (int)n;
}

static int tls_srv_write_cb(void *ctx, const unsigned char *buf, size_t len)
{
    vigil_socket_t sock = *(vigil_socket_t *)ctx;
    size_t n = 0;
    return vigil_platform_tcp_send(sock, buf, len, &n, NULL) != VIGIL_STATUS_OK ? -1 : (int)n;
}

typedef struct
{
    vigil_socket_t listener;
    volatile int ready;
    const char *response;
    int port;
    br_skey_decoder_context kctx; /* keeps private key memory alive */
} tls_test_server_t;

static void tls_server_func(void *arg)
{
    tls_test_server_t *srv = (tls_test_server_t *)arg;

    if (vigil_platform_tcp_listen("127.0.0.1", srv->port, &srv->listener, NULL) != VIGIL_STATUS_OK)
        return;
    srv->ready = 1;

    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_accept(srv->listener, &client, NULL) != VIGIL_STATUS_OK)
        return;

    br_skey_decoder_init(&srv->kctx);
    br_skey_decoder_push(&srv->kctx, vigil_test_tls_key_der,
                         vigil_test_tls_key_der_len);
    if (br_skey_decoder_last_error(&srv->kctx) != 0 ||
        br_skey_decoder_key_type(&srv->kctx) != BR_KEYTYPE_EC)
    {
        vigil_platform_tcp_close(client, NULL);
        return;
    }
    const br_ec_private_key *sk = br_skey_decoder_get_ec(&srv->kctx);

    br_x509_certificate chain[1];
    chain[0].data = (unsigned char *)(uintptr_t)vigil_test_tls_cert_der;
    chain[0].data_len = vigil_test_tls_cert_der_len;

    br_ssl_server_context sc;
    /* minf2g: ECDHE_ECDSA with AES-128-GCM-SHA256 — matches our EC P-256 key. */
    br_ssl_server_init_minf2g(&sc, chain, 1, sk);

    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof(iobuf), 1);
    br_ssl_server_reset(&sc);

    br_sslio_context ioc;
    br_sslio_init(&ioc, &sc.eng, tls_srv_read_cb, &client,
                  tls_srv_write_cb, &client);

    /* Drain the request (enough to unblock the client write). */
    char req[4096];
    br_sslio_read(&ioc, req, sizeof(req) - 1);

    br_sslio_write_all(&ioc, srv->response, strlen(srv->response));
    br_sslio_flush(&ioc);
    br_sslio_close(&ioc);
    vigil_platform_tcp_close(client, NULL);
}

static int start_tls_server(tls_test_server_t *srv, const char *response,
                            int port, vigil_platform_thread_t **thread)
{
    memset(srv, 0, sizeof(*srv));
    srv->listener = VIGIL_INVALID_SOCKET;
    srv->response = response;
    srv->port = port;

    vigil_platform_net_init(NULL);
    vigil_status_t st = vigil_platform_thread_create(thread, tls_server_func, srv, NULL);
    if (st != VIGIL_STATUS_OK)
        return 0;

    for (int i = 0; i < 200 && !srv->ready; i++)
        vigil_platform_thread_sleep(10);
    return srv->ready;
}

static void stop_tls_server(tls_test_server_t *srv, vigil_platform_thread_t *thread)
{
    if (srv->listener != VIGIL_INVALID_SOCKET)
        vigil_platform_tcp_close(srv->listener, NULL);
    vigil_platform_thread_join(thread, NULL);
}

/* Client-side: verify the BearSSL TLS transport carries a plain HTTP GET. */
TEST(VigilHttpTest, BearSslHttpsGet)
{
    const char *canned = "HTTP/1.1 200 OK\r\n"
                         "Content-Length: 2\r\n"
                         "Connection: close\r\n"
                         "\r\nOK";
    tls_test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    if (!start_tls_server(&srv, canned, TLS_TEST_PORT, &thr))
        return;

    parsed_url_t url;
    char url_str[64];
    snprintf(url_str, sizeof(url_str), "https://127.0.0.1:%d/hello", TLS_TEST_PORT);
    parse_url(url_str, &url);

    http_response_t resp;
    int rc = bearssl_https_insecure_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    response_free(&resp);

    stop_tls_server(&srv, thr);
}

/* Verify BearSSL TLS carries a POST with a request body. */
TEST(VigilHttpTest, BearSslHttpsPost)
{
    const char *canned = "HTTP/1.1 201 Created\r\n"
                         "Content-Length: 0\r\n"
                         "Connection: close\r\n"
                         "\r\n";
    tls_test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    if (!start_tls_server(&srv, canned, TLS_TEST_PORT + 1, &thr))
        return;

    parsed_url_t url;
    char url_str[64];
    snprintf(url_str, sizeof(url_str), "https://127.0.0.1:%d/upload", TLS_TEST_PORT + 1);
    parse_url(url_str, &url);

    const char *body = "payload";
    http_response_t resp;
    int rc = bearssl_https_insecure_request("POST", &url,
                                            "Content-Type: text/plain\r\n",
                                            body, strlen(body), &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 201);
    response_free(&resp);

    stop_tls_server(&srv, thr);
}

#endif /* VIGIL_ENABLE_BEARSSL_TLS && VIGIL_TLS_TEST_CERT_AVAILABLE */

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
    /* Cookie jar helpers */
    REGISTER_TEST(VigilHttpTest, CookieJarAppend);
    REGISTER_TEST(VigilHttpTest, CookieJarAppendNoOp);
    REGISTER_TEST(VigilHttpTest, HdrNameMatches);
    REGISTER_TEST(VigilHttpTest, CollectCookiesFromHeaders);
    REGISTER_TEST(VigilHttpTest, CollectCookiesEmpty);
    REGISTER_TEST(VigilHttpTest, BuildRequestHeadersWithCookies);
    REGISTER_TEST(VigilHttpTest, BuildRequestHeadersNoCookies);
    REGISTER_TEST(VigilHttpTest, RedirectChangesMethodTrue);
    REGISTER_TEST(VigilHttpTest, RedirectChangesMethodFalse);
    /* do_request integration */
    REGISTER_TEST(VigilHttpTest, DoRequestLoopback);
    REGISTER_TEST(VigilHttpTest, DoRequestFollowsRedirect);
    REGISTER_TEST(VigilHttpTest, DoRequestHttpsFallbackFails);
    /* URL parsing extras */
    REGISTER_TEST(VigilHttpTest, ParseUrlUppercaseScheme);
    REGISTER_TEST(VigilHttpTest, ParseUrlPathTooLong);
    /* Misc */
    REGISTER_TEST(VigilHttpTest, ResponseFreeNull);
    /* Server */
    REGISTER_TEST(VigilHttpTest, ServerRoundTrip);
    REGISTER_TEST(VigilHttpTest, ServerPostRoundTrip);
#if defined(VIGIL_ENABLE_BEARSSL_TLS) && defined(VIGIL_TLS_TEST_CERT_AVAILABLE)
    /* BearSSL TLS */
    REGISTER_TEST(VigilHttpTest, BearSslHttpsGet);
    REGISTER_TEST(VigilHttpTest, BearSslHttpsPost);
#endif
}
