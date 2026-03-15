#include "basl_test.h"
#include <math.h>
#include <string.h>


#include "basl/json.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static size_t g_alloc_count = 0;
static size_t g_dealloc_count = 0;

static void *tracking_alloc(void *ud, size_t size) {
    (void)ud; g_alloc_count++;
    return calloc(1, size);
}
static void *tracking_realloc(void *ud, void *p, size_t size) {
    (void)ud; return realloc(p, size);
}
static void tracking_dealloc(void *ud, void *p) {
    (void)ud; g_dealloc_count++;
    free(p);
}

static basl_allocator_t tracking_allocator(void) {
    basl_allocator_t a;
    a.user_data = NULL;
    a.allocate = tracking_alloc;
    a.reallocate = tracking_realloc;
    a.deallocate = tracking_dealloc;
    return a;
}

/* Parse helper that uses default allocator. */
static basl_json_value_t *parse(int *basl_test_failed_, const char *input) {
    basl_json_value_t *v = NULL;
    basl_error_t error = {0};
    basl_status_t s = basl_json_parse(NULL, input, strlen(input), &v, &error);
    EXPECT_EQ(s, BASL_STATUS_OK);
    return v;
}

/* Parse that expects failure. */
static void parse_fail(int *basl_test_failed_, const char *input) {
    basl_json_value_t *v = NULL;
    basl_error_t error = {0};
    basl_status_t s = basl_json_parse(NULL, input, strlen(input), &v, &error);
    EXPECT_NE(s, BASL_STATUS_OK);
    EXPECT_EQ(v, NULL);
}

/* Emit helper. */
static char *emit_json(int *basl_test_failed_, const basl_json_value_t *v) {
    char *str = NULL;
    size_t len = 0;
    basl_error_t error = {0};
    EXPECT_EQ(basl_json_emit(v, &str, &len, &error), BASL_STATUS_OK);
    char *result = (char *)malloc(len + 1); memcpy(result, str, len); result[len] = 0;
    free(str);
    return result;
}

/* ── Scalar constructors ─────────────────────────────────────────── */

TEST(BaslJsonTest, NullValue) {
    basl_json_value_t *v = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_null_new(NULL, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_NULL);
    EXPECT_STREQ(emit_json(basl_test_failed_, v), "null");
    basl_json_free(&v);
    EXPECT_EQ(v, NULL);
}

TEST(BaslJsonTest, BoolValues) {
    basl_json_value_t *t = NULL, *f = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_bool_new(NULL, 1, &t, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_json_bool_new(NULL, 0, &f, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_type(t), BASL_JSON_BOOL);
    EXPECT_EQ(basl_json_bool_value(t), 1);
    EXPECT_EQ(basl_json_bool_value(f), 0);
    EXPECT_STREQ(emit_json(basl_test_failed_, t), "true");
    EXPECT_STREQ(emit_json(basl_test_failed_, f), "false");
    basl_json_free(&t);
    basl_json_free(&f);
}

TEST(BaslJsonTest, NumberValues) {
    basl_json_value_t *v = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_number_new(NULL, 42.0, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_NUMBER);
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 42.0);
    EXPECT_STREQ(emit_json(basl_test_failed_, v), "42");
    basl_json_free(&v);

    ASSERT_EQ(basl_json_number_new(NULL, 3.14, &v, &error), BASL_STATUS_OK);
    EXPECT_STREQ(emit_json(basl_test_failed_, v), "3.1400000000000001");
    basl_json_free(&v);

    ASSERT_EQ(basl_json_number_new(NULL, -0.5, &v, &error), BASL_STATUS_OK);
    EXPECT_STREQ(emit_json(basl_test_failed_, v), "-0.5");
    basl_json_free(&v);
}

TEST(BaslJsonTest, StringValue) {
    basl_json_value_t *v = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_string_new(NULL, "hello", 5, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_STRING);
    EXPECT_STREQ(basl_json_string_value(v), "hello");
    EXPECT_EQ(basl_json_string_length(v), 5U);
    EXPECT_STREQ(emit_json(basl_test_failed_, v), "\"hello\"");
    basl_json_free(&v);
}

TEST(BaslJsonTest, StringWithEmbeddedNull) {
    basl_json_value_t *v = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_string_new(NULL, "a\0b", 3, &v, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_string_length(v), 3U);
    EXPECT_EQ(memcmp(basl_json_string_value(v), "a\0b", 3), 0);
    basl_json_free(&v);
}

/* ── Array operations ────────────────────────────────────────────── */

TEST(BaslJsonTest, EmptyArray) {
    basl_json_value_t *a = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_array_new(NULL, &a, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_array_count(a), 0U);
    EXPECT_EQ(basl_json_array_get(a, 0), NULL);
    EXPECT_STREQ(emit_json(basl_test_failed_, a), "[]");
    basl_json_free(&a);
}

TEST(BaslJsonTest, ArrayPushAndGet) {
    basl_json_value_t *a = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_array_new(NULL, &a, &error), BASL_STATUS_OK);

    basl_json_value_t *n = NULL;
    basl_json_number_new(NULL, 1.0, &n, &error);
    ASSERT_EQ(basl_json_array_push(a, n, &error), BASL_STATUS_OK);

    basl_json_number_new(NULL, 2.0, &n, &error);
    ASSERT_EQ(basl_json_array_push(a, n, &error), BASL_STATUS_OK);

    EXPECT_EQ(basl_json_array_count(a), 2U);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_array_get(a, 0)), 1.0);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_array_get(a, 1)), 2.0);
    EXPECT_STREQ(emit_json(basl_test_failed_, a), "[1,2]");
    basl_json_free(&a);
}

/* ── Object operations ───────────────────────────────────────────── */

TEST(BaslJsonTest, EmptyObject) {
    basl_json_value_t *o = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_object_new(NULL, &o, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_json_object_count(o), 0U);
    EXPECT_EQ(basl_json_object_get(o, "x"), NULL);
    EXPECT_STREQ(emit_json(basl_test_failed_, o), "{}");
    basl_json_free(&o);
}

TEST(BaslJsonTest, ObjectSetAndGet) {
    basl_json_value_t *o = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_object_new(NULL, &o, &error), BASL_STATUS_OK);

    basl_json_value_t *v = NULL;
    basl_json_number_new(NULL, 42.0, &v, &error);
    ASSERT_EQ(basl_json_object_set(o, "answer", 6, v, &error), BASL_STATUS_OK);

    EXPECT_EQ(basl_json_object_count(o), 1U);
    const basl_json_value_t *got = basl_json_object_get(o, "answer");
    ASSERT_NE(got, NULL);
    EXPECT_DOUBLE_EQ(basl_json_number_value(got), 42.0);
    EXPECT_STREQ(emit_json(basl_test_failed_, o), "{\"answer\":42}");
    basl_json_free(&o);
}

TEST(BaslJsonTest, ObjectReplaceKey) {
    basl_json_value_t *o = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_object_new(NULL, &o, &error), BASL_STATUS_OK);

    basl_json_value_t *v1 = NULL, *v2 = NULL;
    basl_json_number_new(NULL, 1.0, &v1, &error);
    basl_json_number_new(NULL, 2.0, &v2, &error);
    basl_json_object_set(o, "k", 1, v1, &error);
    basl_json_object_set(o, "k", 1, v2, &error);

    EXPECT_EQ(basl_json_object_count(o), 1U);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_object_get(o, "k")), 2.0);
    basl_json_free(&o);
}

TEST(BaslJsonTest, ObjectEntryIteration) {
    basl_json_value_t *o = NULL;
    basl_error_t error = {0};
    ASSERT_EQ(basl_json_object_new(NULL, &o, &error), BASL_STATUS_OK);

    basl_json_value_t *v = NULL;
    basl_json_string_new(NULL, "val", 3, &v, &error);
    basl_json_object_set(o, "key", 3, v, &error);

    const char *key = NULL;
    size_t key_len = 0;
    const basl_json_value_t *entry_val = NULL;
    ASSERT_EQ(basl_json_object_entry(o, 0, &key, &key_len, &entry_val), BASL_STATUS_OK);
    EXPECT_EQ(key_len, 3U);
    EXPECT_EQ(memcmp(key, "key", 3), 0);
    EXPECT_STREQ(basl_json_string_value(entry_val), "val");
    basl_json_free(&o);
}

/* ── Parse: scalars ──────────────────────────────────────────────── */

TEST(BaslJsonTest, ParseNull) {
    basl_json_value_t *v = parse(basl_test_failed_, "null");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_NULL);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseBool) {
    basl_json_value_t *t = parse(basl_test_failed_, "true");
    basl_json_value_t *f = parse(basl_test_failed_, "false");
    EXPECT_EQ(basl_json_bool_value(t), 1);
    EXPECT_EQ(basl_json_bool_value(f), 0);
    basl_json_free(&t);
    basl_json_free(&f);
}

TEST(BaslJsonTest, ParseIntegers) {
    basl_json_value_t *v = parse(basl_test_failed_, "0");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 0.0);
    basl_json_free(&v);

    v = parse(basl_test_failed_, "42");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 42.0);
    basl_json_free(&v);

    v = parse(basl_test_failed_, "-7");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), -7.0);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseFloats) {
    basl_json_value_t *v = parse(basl_test_failed_, "3.14");
    EXPECT_NEAR(basl_json_number_value(v), 3.14, 1e-10);
    basl_json_free(&v);

    v = parse(basl_test_failed_, "-0.5");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), -0.5);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseExponent) {
    basl_json_value_t *v = parse(basl_test_failed_, "1e10");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 1e10);
    basl_json_free(&v);

    v = parse(basl_test_failed_, "2.5E-3");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), 2.5e-3);
    basl_json_free(&v);

    v = parse(basl_test_failed_, "-1.5e+2");
    EXPECT_DOUBLE_EQ(basl_json_number_value(v), -150.0);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseString) {
    basl_json_value_t *v = parse(basl_test_failed_, "\"hello world\"");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(basl_json_type(v), BASL_JSON_STRING);
    EXPECT_STREQ(basl_json_string_value(v), "hello world");
    EXPECT_EQ(basl_json_string_length(v), 11U);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseEmptyString) {
    basl_json_value_t *v = parse(basl_test_failed_, "\"\"");
    EXPECT_EQ(basl_json_string_length(v), 0U);
    EXPECT_STREQ(basl_json_string_value(v), "");
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringEscapes) {
    basl_json_value_t *v = parse(basl_test_failed_, "\"a\\nb\\tc\\\\d\\\"e\\/f\"");
    EXPECT_STREQ(basl_json_string_value(v), "a\nb\tc\\d\"e/f");
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringUnicodeEscape) {
    /* \u0041 = 'A' */
    basl_json_value_t *v = parse(basl_test_failed_, "\"\\u0041\"");
    EXPECT_STREQ(basl_json_string_value(v), "A");
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringUnicodeMultibyte) {
    /* \u00E9 = 'é' (2-byte UTF-8: 0xC3 0xA9) */
    basl_json_value_t *v = parse(basl_test_failed_, "\"\\u00e9\"");
    EXPECT_EQ(basl_json_string_length(v), 2U);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[0], 0xC3);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[1], 0xA9);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringUnicode3Byte) {
    /* \u4E16 = '世' (3-byte UTF-8) */
    basl_json_value_t *v = parse(basl_test_failed_, "\"\\u4e16\"");
    EXPECT_EQ(basl_json_string_length(v), 3U);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseStringSurrogatePair) {
    /* \uD83D\uDE00 = U+1F600 '😀' (4-byte UTF-8) */
    basl_json_value_t *v = parse(basl_test_failed_, "\"\\uD83D\\uDE00\"");
    EXPECT_EQ(basl_json_string_length(v), 4U);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[0], 0xF0);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[1], 0x9F);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[2], 0x98);
    EXPECT_EQ((unsigned char)basl_json_string_value(v)[3], 0x80);
    basl_json_free(&v);
}

/* ── Parse: arrays ───────────────────────────────────────────────── */

TEST(BaslJsonTest, ParseEmptyArray) {
    basl_json_value_t *v = parse(basl_test_failed_, "[]");
    EXPECT_EQ(basl_json_type(v), BASL_JSON_ARRAY);
    EXPECT_EQ(basl_json_array_count(v), 0U);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseArray) {
    basl_json_value_t *v = parse(basl_test_failed_, "[1, \"two\", true, null]");
    ASSERT_EQ(basl_json_array_count(v), 4U);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_array_get(v, 0)), 1.0);
    EXPECT_STREQ(basl_json_string_value(basl_json_array_get(v, 1)), "two");
    EXPECT_EQ(basl_json_bool_value(basl_json_array_get(v, 2)), 1);
    EXPECT_EQ(basl_json_type(basl_json_array_get(v, 3)), BASL_JSON_NULL);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseNestedArray) {
    basl_json_value_t *v = parse(basl_test_failed_, "[[1,2],[3]]");
    ASSERT_EQ(basl_json_array_count(v), 2U);
    EXPECT_EQ(basl_json_array_count(basl_json_array_get(v, 0)), 2U);
    EXPECT_EQ(basl_json_array_count(basl_json_array_get(v, 1)), 1U);
    basl_json_free(&v);
}

/* ── Parse: objects ──────────────────────────────────────────────── */

TEST(BaslJsonTest, ParseEmptyObject) {
    basl_json_value_t *v = parse(basl_test_failed_, "{}");
    EXPECT_EQ(basl_json_type(v), BASL_JSON_OBJECT);
    EXPECT_EQ(basl_json_object_count(v), 0U);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseObject) {
    basl_json_value_t *v = parse(basl_test_failed_, "{\"name\": \"basl\", \"version\": 1}");
    ASSERT_EQ(basl_json_object_count(v), 2U);
    EXPECT_STREQ(basl_json_string_value(basl_json_object_get(v, "name")), "basl");
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_object_get(v, "version")), 1.0);
    basl_json_free(&v);
}

TEST(BaslJsonTest, ParseNestedObject) {
    basl_json_value_t *v = parse(basl_test_failed_, "{\"a\":{\"b\":true}}");
    const basl_json_value_t *inner = basl_json_object_get(v, "a");
    ASSERT_NE(inner, NULL);
    EXPECT_EQ(basl_json_bool_value(basl_json_object_get(inner, "b")), 1);
    basl_json_free(&v);
}

/* ── Parse: whitespace ───────────────────────────────────────────── */

TEST(BaslJsonTest, ParseWhitespace) {
    basl_json_value_t *v = parse(basl_test_failed_, "  \n\t { \n \"x\" : 1 \n } \n ");
    EXPECT_EQ(basl_json_type(v), BASL_JSON_OBJECT);
    EXPECT_DOUBLE_EQ(basl_json_number_value(basl_json_object_get(v, "x")), 1.0);
    basl_json_free(&v);
}

/* ── Parse: errors ───────────────────────────────────────────────── */

TEST(BaslJsonTest, ParseErrorEmpty) { parse_fail(basl_test_failed_, ""); }
TEST(BaslJsonTest, ParseErrorTrailing) { parse_fail(basl_test_failed_, "true false"); }
TEST(BaslJsonTest, ParseErrorBadToken) { parse_fail(basl_test_failed_, "undefined"); }
TEST(BaslJsonTest, ParseErrorUntermString) { parse_fail(basl_test_failed_, "\"hello"); }
TEST(BaslJsonTest, ParseErrorBadEscape) { parse_fail(basl_test_failed_, "\"\\x\""); }
TEST(BaslJsonTest, ParseErrorBadUnicode) { parse_fail(basl_test_failed_, "\"\\u00GG\""); }
TEST(BaslJsonTest, ParseErrorMissingSurrogate) { parse_fail(basl_test_failed_, "\"\\uD83D\""); }
TEST(BaslJsonTest, ParseErrorBadSurrogate) { parse_fail(basl_test_failed_, "\"\\uD83D\\u0041\""); }
TEST(BaslJsonTest, ParseErrorArrayNoClose) { parse_fail(basl_test_failed_, "[1, 2"); }
TEST(BaslJsonTest, ParseErrorObjectNoClose) { parse_fail(basl_test_failed_, "{\"a\": 1"); }
TEST(BaslJsonTest, ParseErrorObjectNoColon) { parse_fail(basl_test_failed_, "{\"a\" 1}"); }
TEST(BaslJsonTest, ParseErrorObjectBadKey) { parse_fail(basl_test_failed_, "{1: 2}"); }
TEST(BaslJsonTest, ParseErrorNumberLeadingZero) {
    /* Leading zeros are not valid JSON: 01 should fail. */
    /* Actually, our parser accepts "01" as "0" then trailing "1".
       That's caught by the trailing-content check. */
    parse_fail(basl_test_failed_, "01");
}

/* ── Roundtrip ───────────────────────────────────────────────────── */

TEST(BaslJsonTest, RoundtripComplex) {
    const char *input = "{\"name\":\"basl\",\"version\":1,\"features\":[\"vm\",\"debugger\"],\"config\":{\"debug\":true,\"opt\":null}}";
    basl_json_value_t *v = parse(basl_test_failed_, input);
    ASSERT_NE(v, NULL);
    char *output = emit_json(basl_test_failed_, v);
    EXPECT_STREQ(output, input);
    basl_json_free(&v);
}

TEST(BaslJsonTest, RoundtripEscapes) {
    const char *input = "\"line1\\nline2\\ttab\\\\backslash\\\"quote\"";
    basl_json_value_t *v = parse(basl_test_failed_, input);
    char *output = emit_json(basl_test_failed_, v);
    EXPECT_STREQ(output, input);
    basl_json_free(&v);
}

/* ── Custom allocator ────────────────────────────────────────────── */

TEST(BaslJsonTest, CustomAllocator) {
    g_alloc_count = 0;
    g_dealloc_count = 0;
    basl_allocator_t a = tracking_allocator();
    basl_error_t error = {0};

    basl_json_value_t *v = NULL;
    const char *input = "{\"key\":[1,2,3]}";
    ASSERT_EQ(basl_json_parse(&a, input, strlen(input), &v, &error), BASL_STATUS_OK);

    /* Emit also uses the value's stored allocator. */
    char *str = NULL;
    size_t len = 0;
    ASSERT_EQ(basl_json_emit(v, &str, &len, &error), BASL_STATUS_OK);
    EXPECT_STREQ(str, input);

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
    basl_json_free(NULL);
    basl_json_value_t *null_val = NULL;
    basl_json_free(&null_val);

    EXPECT_EQ(basl_json_type(NULL), BASL_JSON_NULL);
    EXPECT_EQ(basl_json_bool_value(NULL), 0);
    EXPECT_DOUBLE_EQ(basl_json_number_value(NULL), 0.0);
    EXPECT_STREQ(basl_json_string_value(NULL), "");
    EXPECT_EQ(basl_json_string_length(NULL), 0U);
    EXPECT_EQ(basl_json_array_count(NULL), 0U);
    EXPECT_EQ(basl_json_array_get(NULL, 0), NULL);
    EXPECT_EQ(basl_json_object_count(NULL), 0U);
    EXPECT_EQ(basl_json_object_get(NULL, "x"), NULL);
}

TEST(BaslJsonTest, InvalidArguments) {
    basl_error_t error = {0};
    EXPECT_EQ(basl_json_null_new(NULL, NULL, &error), BASL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(basl_json_parse(NULL, NULL, 0, NULL, &error), BASL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(basl_json_emit(NULL, NULL, NULL, &error), BASL_STATUS_INVALID_ARGUMENT);
}

/* ── Emit: control character escaping ────────────────────────────── */

TEST(BaslJsonTest, EmitControlCharacters) {
    basl_json_value_t *v = NULL;
    basl_error_t error = {0};
    /* String with a control character (0x01). */
    ASSERT_EQ(basl_json_string_new(NULL, "\x01", 1, &v, &error), BASL_STATUS_OK);
    char *out = emit_json(basl_test_failed_, v);
    EXPECT_STREQ(out, "\"\\u0001\"");
    basl_json_free(&v);
}

void register_json_tests(void) {
    REGISTER_TEST(BaslJsonTest, NullValue);
    REGISTER_TEST(BaslJsonTest, BoolValues);
    REGISTER_TEST(BaslJsonTest, NumberValues);
    REGISTER_TEST(BaslJsonTest, StringValue);
    REGISTER_TEST(BaslJsonTest, StringWithEmbeddedNull);
    REGISTER_TEST(BaslJsonTest, EmptyArray);
    REGISTER_TEST(BaslJsonTest, ArrayPushAndGet);
    REGISTER_TEST(BaslJsonTest, EmptyObject);
    REGISTER_TEST(BaslJsonTest, ObjectSetAndGet);
    REGISTER_TEST(BaslJsonTest, ObjectReplaceKey);
    REGISTER_TEST(BaslJsonTest, ObjectEntryIteration);
    REGISTER_TEST(BaslJsonTest, ParseNull);
    REGISTER_TEST(BaslJsonTest, ParseBool);
    REGISTER_TEST(BaslJsonTest, ParseIntegers);
    REGISTER_TEST(BaslJsonTest, ParseFloats);
    REGISTER_TEST(BaslJsonTest, ParseExponent);
    REGISTER_TEST(BaslJsonTest, ParseString);
    REGISTER_TEST(BaslJsonTest, ParseEmptyString);
    REGISTER_TEST(BaslJsonTest, ParseStringEscapes);
    REGISTER_TEST(BaslJsonTest, ParseStringUnicodeEscape);
    REGISTER_TEST(BaslJsonTest, ParseStringUnicodeMultibyte);
    REGISTER_TEST(BaslJsonTest, ParseStringUnicode3Byte);
    REGISTER_TEST(BaslJsonTest, ParseStringSurrogatePair);
    REGISTER_TEST(BaslJsonTest, ParseEmptyArray);
    REGISTER_TEST(BaslJsonTest, ParseArray);
    REGISTER_TEST(BaslJsonTest, ParseNestedArray);
    REGISTER_TEST(BaslJsonTest, ParseEmptyObject);
    REGISTER_TEST(BaslJsonTest, ParseObject);
    REGISTER_TEST(BaslJsonTest, ParseNestedObject);
    REGISTER_TEST(BaslJsonTest, ParseWhitespace);
    REGISTER_TEST(BaslJsonTest, ParseErrorEmpty);
    REGISTER_TEST(BaslJsonTest, ParseErrorTrailing);
    REGISTER_TEST(BaslJsonTest, ParseErrorBadToken);
    REGISTER_TEST(BaslJsonTest, ParseErrorUntermString);
    REGISTER_TEST(BaslJsonTest, ParseErrorBadEscape);
    REGISTER_TEST(BaslJsonTest, ParseErrorBadUnicode);
    REGISTER_TEST(BaslJsonTest, ParseErrorMissingSurrogate);
    REGISTER_TEST(BaslJsonTest, ParseErrorBadSurrogate);
    REGISTER_TEST(BaslJsonTest, ParseErrorArrayNoClose);
    REGISTER_TEST(BaslJsonTest, ParseErrorObjectNoClose);
    REGISTER_TEST(BaslJsonTest, ParseErrorObjectNoColon);
    REGISTER_TEST(BaslJsonTest, ParseErrorObjectBadKey);
    REGISTER_TEST(BaslJsonTest, ParseErrorNumberLeadingZero);
    REGISTER_TEST(BaslJsonTest, RoundtripComplex);
    REGISTER_TEST(BaslJsonTest, RoundtripEscapes);
    REGISTER_TEST(BaslJsonTest, CustomAllocator);
    REGISTER_TEST(BaslJsonTest, NullSafety);
    REGISTER_TEST(BaslJsonTest, InvalidArguments);
    REGISTER_TEST(BaslJsonTest, EmitControlCharacters);
}
