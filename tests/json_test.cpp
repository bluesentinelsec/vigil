#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

extern "C" {
#include "basl/json.h"
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static size_t g_alloc_count = 0;
static size_t g_dealloc_count = 0;

static void *tracking_alloc(void *, size_t size) {
    g_alloc_count++;
    return calloc(1, size);
}
static void *tracking_realloc(void *, void *p, size_t size) {
    return realloc(p, size);
}
static void tracking_dealloc(void *, void *p) {
    g_dealloc_count++;
    free(p);
}

static basl_allocator_t tracking_allocator() {
    basl_allocator_t a;
    a.user_data = nullptr;
    a.allocate = tracking_alloc;
    a.reallocate = tracking_realloc;
    a.deallocate = tracking_dealloc;
    return a;
}

/* Parse helper that uses default allocator. */
static basl_json_value_t *parse(const char *input) {
    basl_json_value_t *v = nullptr;
    basl_error_t error = {};
    basl_status_t s = basl_json_parse(nullptr, input, strlen(input), &v, &error);
    EXPECT_EQ(s, BASL_STATUS_OK) << basl_error_message(&error);
    return v;
}

/* Parse that expects failure. */
static void parse_fail(const char *input) {
    basl_json_value_t *v = nullptr;
    basl_error_t error = {};
    basl_status_t s = basl_json_parse(nullptr, input, strlen(input), &v, &error);
    EXPECT_NE(s, BASL_STATUS_OK);
    EXPECT_EQ(v, nullptr);
}

/* Emit helper. */
static std::string emit(const basl_json_value_t *v) {
    char *str = nullptr;
    size_t len = 0;
    basl_error_t error = {};
    EXPECT_EQ(basl_json_emit(v, &str, &len, &error), BASL_STATUS_OK);
    std::string result(str, len);
    free(str);
    return result;
}

/* ── Scalar constructors ─────────────────────────────────────────── */

TEST(BaslJsonTest, NullValue) {
    basl_json_value_t *v = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_null_new(nullptr, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_NULL);
    EXPECT_EQ(emit(v), "null");
    basl_json_free(&v);
    EXPECT_EQ(v, nullptr);
}

TEST(BaslJsonTest, BoolValues) {
    basl_json_value_t *t = nullptr, *f = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_bool_new(nullptr, 1, &t, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_json_bool_new(nullptr, 0, &f, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_type(t), BASL_JSON_BOOL);
    EXPECT_EQ(basl_json_bool_value(t), 1);
    EXPECT_EQ(basl_json_bool_value(f), 0);
    EXPECT_EQ(emit(t), "true");
    EXPECT_EQ(emit(f), "false");
    basl_json_free(&t);
    basl_json_free(&f);
}

TEST(BaslJsonTest, NumberValues) {
    basl_json_value_t *v = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_number_new(nullptr, 42.0, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_NUMBER);
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 42.0);
    EXPECT_EQ(emit(v), "42");
    basl_json_free(&v);

    ASSERT_EQ(basl_json_number_new(nullptr, 3.14, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(emit(v), "3.1400000000000001");
    basl_json_free(&v);

    ASSERT_EQ(basl_json_number_new(nullptr, -0.5, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(emit(v), "-0.5");
    basl_json_free(&v);
}

TEST(BaslJsonTest, StringValue) {
    basl_json_value_t *v = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_string_new(nullptr, "hello", 5, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_STRING);
    EXPECT_STREQ(basl_json_string_value(v), "hello");
    EXPECT_EQ(basl_json_string_length(v), 5U);
    EXPECT_EQ(emit(v), "\"hello\"");
    basl_json_free(&v);
}

TEST(BaslJsonTest, StringWithEmbeddedNull) {
    basl_json_value_t *v = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_string_new(nullptr, "a\0b", 3, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_string_length(v), 3U);
    EXPECT_EQ(memcmp(basl_json_string_value(v), "a\0b", 3), 0);
    basl_json_free(&v);
}

/* ── Array operations ────────────────────────────────────────────── */

TEST(BaslJsonTest, EmptyArray) {
    basl_json_value_t *a = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_array_new(nullptr, &a, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_array_count(a), 0U);
    EXPECT_EQ(basl_json_array_get(a, 0), nullptr);
    EXPECT_EQ(emit(a), "[]");
    basl_json_free(&a);
}

TEST(BaslJsonTest, ArrayPushAndGet) {
    basl_json_value_t *a = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_array_new(nullptr, &a, &error), BASL_STATUS_OK);

    basl_json_value_t *n = nullptr;
    basl_json_number_new(nullptr, 1.0, &n, &error);
    ASSERT_EQ(basl_json_array_push(a, n, &error), BASL_STATUS_OK);

    basl_json_number_new(nullptr, 2.0, &n, &error);
    ASSERT_EQ(basl_json_array_push(a, n, &error), BASL_STATUS_OK);

    EXPECT_EQ(basl_json_array_count(a), 2U);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_array_get(a, 0)), 1.0);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_array_get(a, 1)), 2.0);
    EXPECT_EQ(emit(a), "[1,2]");
    basl_json_free(&a);
}

/* ── Object operations ───────────────────────────────────────────── */

TEST(BaslJsonTest, EmptyObject) {
    basl_json_value_t *o = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_object_new(nullptr, &o, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_object_count(o), 0U);
    EXPECT_EQ(basl_json_object_get(o, "x"), nullptr);
    EXPECT_EQ(emit(o), "{}");
    basl_json_free(&o);
}

TEST(BaslJsonTest, ObjectSetAndGet) {
    basl_json_value_t *o = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_object_new(nullptr, &o, &error), BASL_STATUS_OK);

    basl_json_value_t *v = nullptr;
    basl_json_number_new(nullptr, 42.0, &v, &error);
    ASSERT_EQ(basl_json_object_set(o, "answer", 6, v, &error), BASL_STATUS_OK);

    EXPECT_EQ(basl_json_object_count(o), 1U);
    const basl_json_value_t *got = basl_json_object_get(o, "answer");
    ASSERT_NE(got, nullptr);
    EXPECT_DOUBLE_EQ(basl_json_number_value(got), 42.0);
    EXPECT_EQ(emit(o), "{\"answer\":42}");
    basl_json_free(&o);
}

TEST(BaslJsonTest, ObjectReplaceKey) {
    basl_json_value_t *o = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_object_new(nullptr, &o, &error), BASL_STATUS_OK);

    basl_json_value_t *v1 = nullptr, *v2 = nullptr;
    basl_json_number_new(nullptr, 1.0, &v1, &error);
    basl_json_number_new(nullptr, 2.0, &v2, &error);
    basl_json_object_set(o, "k", 1, v1, &error);
    basl_json_object_set(o, "k", 1, v2, &error);

    EXPECT_EQ(basl_json_object_count(o), 1U);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_object_get(o, "k")), 2.0);
    basl_json_free(&o);
}

TEST(BaslJsonTest, ObjectEntryIteration) {
    basl_json_value_t *o = nullptr;
    basl_error_t error = {};
    ASSERT_EQ(basl_json_object_new(nullptr, &o, &error), BASL_STATUS_OK);

    basl_json_value_t *v = nullptr;
    basl_json_string_new(nullptr, "val", 3, &v, &error);
    basl_json_object_set(o, "key", 3, v, &error);

    const char *key = nullptr;
    size_t key_len = 0;
    const basl_json_value_t *entry_val = nullptr;
    ASSERT_EQ(basl_json_object_entry(o, 0, &key, &key_len, &entry_val), BASL_STATUS_OK);
    EXPECT_EQ(key_len, 3U);
    EXPECT_EQ(memcmp(key, "key", 3), 0);
    EXPECT_STREQ(basl_json_string_value(entry_val), "val");
    basl_json_free(&o);
}

/* ── Parse: scalars ──────────────────────────────────────────────── */

TEST(BaslJsonTest, ParseNull) {
    basl_json_value_t *v = parse("null");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_NULL);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseBool) {
    basl_json_value_t *t = parse("true");
    basl_json_value_t *f = parse("false");
    EXPECT_EQ(basl_json_bool_value(t), 1);
    EXPECT_EQ(basl_json_bool_value(f), 0);
    basl_json_free(&t);
    basl_json_free(&f);
}

TEST(BaslJsonTest, ParseIntegers) {
    basl_json_value_t *v = parse("0");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 0.0);
    basl_json_free(&v);

    v = parse("42");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 42.0);
    basl_json_free(&v);

    v = parse("-7");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), -7.0);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseFloats) {
    basl_json_value_t *v = parse("3.14");
    EXPECT_NEAR(basl_json_number_value(v), 3.14, 1e-10);
    basl_json_free(&v);

    v = parse("-0.5");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), -0.5);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseExponent) {
    basl_json_value_t *v = parse("1e10");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 1e10);
    basl_json_free(&v);

    v = parse("2.5E-3");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 2.5e-3);
    basl_json_free(&v);

    v = parse("-1.5e+2");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), -150.0);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseString) {
    basl_json_value_t *v = parse("\"hello world\"");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_STRING);
    EXPECT_STREQ(basl_json_string_value(v), "hello world");
    EXPECT_EQ(basl_json_string_length(v), 11U);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseEmptyString) {
    basl_json_value_t *v = parse("\"\"");
    EXPECT_EQ(basl_json_string_length(v), 0U);
    EXPECT_STREQ(basl_json_string_value(v), "");
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringEscapes) {
    basl_json_value_t *v = parse("\"a\\nb\\tc\\\\d\\\"e\\/f\"");
    EXPECT_STREQ(basl_json_string_value(v), "a\nb\tc\\d\"e/f");
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringUnicodeEscape) {
    /* \u0041 = 'A' */
    basl_json_value_t *v = parse("\"\\u0041\"");
    EXPECT_STREQ(basl_json_string_value(v), "A");
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringUnicodeMultibyte) {
    /* \u00E9 = 'é' (2-byte UTF-8: 0xC3 0xA9) */
    basl_json_value_t *v = parse("\"\\u00e9\"");
    EXPECT_EQ(basl_json_string_length(v), 2U);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[0], 0xC3);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[1], 0xA9);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringUnicode3Byte) {
    /* \u4E16 = '世' (3-byte UTF-8) */
    basl_json_value_t *v = parse("\"\\u4e16\"");
    EXPECT_EQ(basl_json_string_length(v), 3U);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringSurrogatePair) {
    /* \uD83D\uDE00 = U+1F600 '😀' (4-byte UTF-8) */
    basl_json_value_t *v = parse("\"\\uD83D\\uDE00\"");
    EXPECT_EQ(basl_json_string_length(v), 4U);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[0], 0xF0);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[1], 0x9F);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[2], 0x98);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[3], 0x80);
    basl_json_free(&v);
}

/* ── Parse: arrays ───────────────────────────────────────────────── */

TEST(BaslJsonTest, ParseEmptyArray) {
    basl_json_value_t *v = parse("[]");
    EXPECT_EQ(basl_json_type(v), BASL_JSON_ARRAY);
    EXPECT_EQ(basl_json_array_count(v), 0U);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseArray) {
    basl_json_value_t *v = parse("[1, \"two\", true, null]");
    ASSERT_EQ(basl_json_array_count(v), 4U);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_array_get(v, 0)), 1.0);
    EXPECT_STREQ(basl_json_string_value(basl_json_array_get(v, 1)), "two");
    EXPECT_EQ(basl_json_bool_value(basl_json_array_get(v, 2)), 1);
    EXPECT_EQ(basl_json_type(basl_json_array_get(v, 3)), BASL_JSON_NULL);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseNestedArray) {
    basl_json_value_t *v = parse("[[1,2],[3]]");
    ASSERT_EQ(basl_json_array_count(v), 2U);
    EXPECT_EQ(basl_json_array_count(basl_json_array_get(v, 0)), 2U);
    EXPECT_EQ(basl_json_array_count(basl_json_array_get(v, 1)), 1U);
    basl_json_free(&v);
}

/* ── Parse: objects ──────────────────────────────────────────────── */

TEST(BaslJsonTest, ParseEmptyObject) {
    basl_json_value_t *v = parse("{}");
    EXPECT_EQ(basl_json_type(v), BASL_JSON_OBJECT);
    EXPECT_EQ(basl_json_object_count(v), 0U);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseObject) {
    basl_json_value_t *v = parse("{\"name\": \"basl\", \"version\": 1}");
    ASSERT_EQ(basl_json_object_count(v), 2U);
    EXPECT_STREQ(basl_json_string_value(basl_json_object_get(v, "name")), "basl");
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_object_get(v, "version")), 1.0);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseNestedObject) {
    basl_json_value_t *v = parse("{\"a\":{\"b\":true}}");
    const basl_json_value_t *inner = basl_json_object_get(v, "a");
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(basl_json_bool_value(basl_json_object_get(inner, "b")), 1);
    basl_json_free(&v);
}

/* ── Parse: whitespace ───────────────────────────────────────────── */

TEST(BaslJsonTest, ParseWhitespace) {
    basl_json_value_t *v = parse("  \n\t { \n \"x\" : 1 \n } \n ");
    EXPECT_EQ(basl_json_type(v), BASL_JSON_OBJECT);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_object_get(v, "x")), 1.0);
    basl_json_free(&v);
}

/* ── Parse: errors ───────────────────────────────────────────────── */

TEST(BaslJsonTest, ParseErrorEmpty) { parse_fail(""); }
TEST(BaslJsonTest, ParseErrorTrailing) { parse_fail("true false"); }
TEST(BaslJsonTest, ParseErrorBadToken) { parse_fail("undefined"); }
TEST(BaslJsonTest, ParseErrorUntermString) { parse_fail("\"hello"); }
TEST(BaslJsonTest, ParseErrorBadEscape) { parse_fail("\"\\x\""); }
TEST(BaslJsonTest, ParseErrorBadUnicode) { parse_fail("\"\\u00GG\""); }
TEST(BaslJsonTest, ParseErrorMissingSurrogate) { parse_fail("\"\\uD83D\""); }
TEST(BaslJsonTest, ParseErrorBadSurrogate) { parse_fail("\"\\uD83D\\u0041\""); }
TEST(BaslJsonTest, ParseErrorArrayNoClose) { parse_fail("[1, 2"); }
TEST(BaslJsonTest, ParseErrorObjectNoClose) { parse_fail("{\"a\": 1"); }
TEST(BaslJsonTest, ParseErrorObjectNoColon) { parse_fail("{\"a\" 1}"); }
TEST(BaslJsonTest, ParseErrorObjectBadKey) { parse_fail("{1: 2}"); }
TEST(BaslJsonTest, ParseErrorNumberLeadingZero) {
    /* Leading zeros are not valid JSON: 01 should fail. */
    /* Actually, our parser accepts "01" as "0" then trailing "1".
       That's caught by the trailing-content check. */
    parse_fail("01");
}

/* ── Roundtrip ───────────────────────────────────────────────────── */

TEST(BaslJsonTest, RoundtripComplex) {
    const char *input = "{\"name\":\"basl\",\"version\":1,\"features\":[\"vm\",\"debugger\"],\"config\":{\"debug\":true,\"opt\":null}}";
    basl_json_value_t *v = parse(input);
    ASSERT_NE(v, nullptr);
    std::string output = emit(v);
    EXPECT_EQ(output, input);
    basl_json_free(&v);
}

TEST(BaslJsonTest, RoundtripEscapes) {
    const char *input = "\"line1\\nline2\\ttab\\\\backslash\\\"quote\"";
    basl_json_value_t *v = parse(input);
    std::string output = emit(v);
    EXPECT_EQ(output, input);
    basl_json_free(&v);
}

/* ── Custom allocator ────────────────────────────────────────────── */

TEST(BaslJsonTest, CustomAllocator) {
    g_alloc_count = 0;
    g_dealloc_count = 0;
    basl_allocator_t a = tracking_allocator();
    basl_error_t error = {};

    basl_json_value_t *v = nullptr;
    const char *input = "{\"key\":[1,2,3]}";
    ASSERT_EQ(basl_json_parse(&a, input, strlen(input), &v, &error), BASL_STATUS_OK);

    /* Emit also uses the value's stored allocator. */
    char *str = nullptr;
    size_t len = 0;
    ASSERT_EQ(basl_json_emit(v, &str, &len, &error), BASL_STATUS_OK);
    EXPECT_EQ(std::string(str, len), input);

    /* Free the emitted string through the tracking allocator. */
    a.deallocate(a.user_data, str);

    size_t allocs_before_free = g_alloc_count;
    basl_json_free(&v);

    /* Verify allocator was actually used. */
    EXPECT_GT(allocs_before_free, 0U);
    EXPECT_GT(g_dealloc_count, 0U);
}

/* ── Null safety ─────────────────────────────────────────────────── */

TEST(BaslJsonTest, NullSafety) {
    /* These should not crash. */
    basl_json_free(nullptr);
    basl_json_value_t *null_val = nullptr;
    basl_json_free(&null_val);

    EXPECT_EQ(basl_json_type(nullptr), BASL_JSON_NULL);
    EXPECT_EQ(basl_json_bool_value(nullptr), 0);
    EXPECT_DOUBLE_EQ(basl_json_number_value(nullptr), 0.0);
    EXPECT_STREQ(basl_json_string_value(nullptr), "");
    EXPECT_EQ(basl_json_string_length(nullptr), 0U);
    EXPECT_EQ(basl_json_array_count(nullptr), 0U);
    EXPECT_EQ(basl_json_array_get(nullptr, 0), nullptr);
    EXPECT_EQ(basl_json_object_count(nullptr), 0U);
    EXPECT_EQ(basl_json_object_get(nullptr, "x"), nullptr);
}

TEST(BaslJsonTest, InvalidArguments) {
    basl_error_t error = {};
    EXPECT_EQ(basl_json_null_new(nullptr, nullptr, &error), BASL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(basl_json_parse(nullptr, nullptr, 0, nullptr, &error), BASL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(basl_json_emit(nullptr, nullptr, nullptr, &error), BASL_STATUS_INVALID_ARGUMENT);
}

/* ── Emit: control character escaping ────────────────────────────── */

TEST(BaslJsonTest, EmitControlCharacters) {
    basl_json_value_t *v = nullptr;
    basl_error_t error = {};
    /* String with a control character (0x01). */
    ASSERT_EQ(basl_json_string_new(nullptr, "\x01", 1, &v, &error), BASL_STATUS_OK);
    std::string out = emit(v);
    EXPECT_EQ(out, "\"\\u0001\"");
    basl_json_free(&v);
}
