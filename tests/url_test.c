/* Unit tests for BASL URL parsing library. */
#include "basl_test.h"
#include "basl/url.h"

#include <stdlib.h>
#include <string.h>

/* ── Parse Tests ─────────────────────────────────────────────────── */

TEST(BaslUrlTest, ParseFull) {
    basl_url_t url;
    basl_status_t s = basl_url_parse(
        "https://user:pass@example.com:8080/path?query=1#frag", 52, &url, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(url.scheme, "https");
    EXPECT_STREQ(url.username, "user");
    EXPECT_STREQ(url.password, "pass");
    EXPECT_STREQ(url.host, "example.com");
    EXPECT_STREQ(url.port, "8080");
    EXPECT_STREQ(url.path, "/path");
    EXPECT_STREQ(url.raw_query, "query=1");
    EXPECT_STREQ(url.fragment, "frag");
    basl_url_free(&url);
}

TEST(BaslUrlTest, ParseSimple) {
    basl_url_t url;
    basl_status_t s = basl_url_parse("http://example.com", 18, &url, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(url.scheme, "http");
    EXPECT_STREQ(url.host, "example.com");
    EXPECT_EQ(url.port, NULL);
    basl_url_free(&url);
}

TEST(BaslUrlTest, ParsePathOnly) {
    basl_url_t url;
    basl_status_t s = basl_url_parse("/foo/bar", 8, &url, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_EQ(url.scheme, NULL);
    EXPECT_EQ(url.host, NULL);
    EXPECT_STREQ(url.path, "/foo/bar");
    basl_url_free(&url);
}

TEST(BaslUrlTest, ParseWithQuery) {
    basl_url_t url;
    basl_status_t s = basl_url_parse("http://example.com?a=1&b=2", 26, &url, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(url.raw_query, "a=1&b=2");
    basl_url_free(&url);
}

TEST(BaslUrlTest, ParseIPv6) {
    basl_url_t url;
    basl_status_t s = basl_url_parse("http://[::1]:8080/path", 22, &url, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(url.host, "::1");
    EXPECT_STREQ(url.port, "8080");
    basl_url_free(&url);
}

/* ── Encoding Tests ──────────────────────────────────────────────── */

TEST(BaslUrlTest, EncodeSpaces) {
    char *encoded;
    size_t len;
    basl_status_t s = basl_url_query_escape("hello world", 11, &encoded, &len, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(encoded, "hello+world");
    free(encoded);
}

TEST(BaslUrlTest, EncodeSpecial) {
    char *encoded;
    size_t len;
    basl_status_t s = basl_url_query_escape("a=b&c=d", 7, &encoded, &len, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(encoded, "a%3Db%26c%3Dd");
    free(encoded);
}

TEST(BaslUrlTest, DecodePercent) {
    char *decoded;
    size_t len;
    basl_status_t s = basl_url_unescape("hello%20world", 13, &decoded, &len, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(decoded, "hello world");
    free(decoded);
}

TEST(BaslUrlTest, DecodePlus) {
    char *decoded;
    size_t len;
    basl_status_t s = basl_url_unescape("hello+world", 11, &decoded, &len, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(decoded, "hello world");
    free(decoded);
}

/* ── Utility Tests ───────────────────────────────────────────────── */

TEST(BaslUrlTest, IsAbsolute) {
    basl_url_t url;
    basl_url_parse("https://example.com", 19, &url, NULL);
    EXPECT_TRUE(basl_url_is_absolute(&url));
    basl_url_free(&url);

    basl_url_parse("/path/only", 10, &url, NULL);
    EXPECT_FALSE(basl_url_is_absolute(&url));
    basl_url_free(&url);
}

TEST(BaslUrlTest, Hostname) {
    basl_url_t url;
    basl_url_parse("https://example.com:8080/path", 29, &url, NULL);
    EXPECT_STREQ(basl_url_hostname(&url), "example.com");
    basl_url_free(&url);
}

/* ── Test Registration ───────────────────────────────────────────── */

void register_url_tests(void) {
    REGISTER_TEST(BaslUrlTest, ParseFull);
    REGISTER_TEST(BaslUrlTest, ParseSimple);
    REGISTER_TEST(BaslUrlTest, ParsePathOnly);
    REGISTER_TEST(BaslUrlTest, ParseWithQuery);
    REGISTER_TEST(BaslUrlTest, ParseIPv6);
    REGISTER_TEST(BaslUrlTest, EncodeSpaces);
    REGISTER_TEST(BaslUrlTest, EncodeSpecial);
    REGISTER_TEST(BaslUrlTest, DecodePercent);
    REGISTER_TEST(BaslUrlTest, DecodePlus);
    REGISTER_TEST(BaslUrlTest, IsAbsolute);
    REGISTER_TEST(BaslUrlTest, Hostname);
}
