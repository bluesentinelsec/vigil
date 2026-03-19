#include "vigil_test.h"
#include <math.h>
#include <string.h>

#include "vigil/json.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static size_t g_alloc_count = 0;
static size_t g_dealloc_count = 0;

static void *tracking_alloc(void *ud, size_t size)
{
    (void)ud;
    g_alloc_count++;
    return calloc(1, size);
}
static void *tracking_realloc(void *ud, void *p, size_t size)
{
    (void)ud;
    return realloc(p, size);
}
static void tracking_dealloc(void *ud, void *p)
{
    (void)ud;
    g_dealloc_count++;
    free(p);
}

static vigil_allocator_t tracking_allocator(void)
{
    vigil_allocator_t a;
    a.user_data = NULL;
    a.allocate = tracking_alloc;
    a.reallocate = tracking_realloc;
    a.deallocate = tracking_dealloc;
    return a;
}

/* Parse helper that uses default allocator. */
static vigil_json_value_t *parse(int *vigil_test_failed_, const char *input)
{
    vigil_json_value_t *v = NULL;
    vigil_error_t error = {0};
    vigil_status_t s = vigil_json_parse(NULL, input, strlen(input), &v, &error);
    EXPECT_EQ(s, VIGIL_STATUS_OK);
    return v;
}

/* Parse that expects failure. */
static void parse_fail(int *vigil_test_failed_, const char *input)
{
    vigil_json_value_t *v = NULL;
    vigil_error_t error = {0};
    vigil_status_t s = vigil_json_parse(NULL, input, strlen(input), &v, &error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
    EXPECT_EQ(v, NULL);
}

/* Emit helper. */
static char *emit_json(int *vigil_test_failed_, const vigil_json_value_t *v)
{
    static char result[4096];
    char *str = NULL;
    size_t len = 0;
    vigil_error_t error = {0};
    EXPECT_EQ(vigil_json_emit(v, &str, &len, &error), VIGIL_STATUS_OK);
    if (len >= sizeof(result))
        len = sizeof(result) - 1;
    memcpy(result, str, len);
    result[len] = '\0';
    free(str);
    return result;
}

/* ── Scalar constructors ─────────────────────────────────────────── */

TEST(VigilJsonTest, NullValue)
{
    vigil_json_value_t *v = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_null_new(NULL, &v, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_json_type(v), VIGIL_JSON_NULL);
    EXPECT_STREQ(emit_json(vigil_test_failed_, v), "null");
    vigil_json_free(&v);
    EXPECT_EQ(v, NULL);
}

TEST(VigilJsonTest, BoolValues)
{
    vigil_json_value_t *t = NULL, *f = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_bool_new(NULL, 1, &t, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_json_bool_new(NULL, 0, &f, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_json_type(t), VIGIL_JSON_BOOL);
    EXPECT_EQ(vigil_json_bool_value(t), 1);
    EXPECT_EQ(vigil_json_bool_value(f), 0);
    EXPECT_STREQ(emit_json(vigil_test_failed_, t), "true");
    EXPECT_STREQ(emit_json(vigil_test_failed_, f), "false");
    vigil_json_free(&t);
    vigil_json_free(&f);
}

TEST(VigilJsonTest, NumberValues)
{
    vigil_json_value_t *v = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_number_new(NULL, 42.0, &v, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_json_type(v), VIGIL_JSON_NUMBER);
    EXPECT_DOUBLE_EQ(vigil_json_number_value(v), 42.0);
    EXPECT_STREQ(emit_json(vigil_test_failed_, v), "42");
    vigil_json_free(&v);

    ASSERT_EQ(vigil_json_number_new(NULL, 3.14, &v, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(emit_json(vigil_test_failed_, v), "3.1400000000000001");
    vigil_json_free(&v);

    ASSERT_EQ(vigil_json_number_new(NULL, -0.5, &v, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(emit_json(vigil_test_failed_, v), "-0.5");
    vigil_json_free(&v);
}

TEST(VigilJsonTest, StringValue)
{
    vigil_json_value_t *v = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_string_new(NULL, "hello", 5, &v, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_json_type(v), VIGIL_JSON_STRING);
    EXPECT_STREQ(vigil_json_string_value(v), "hello");
    EXPECT_EQ(vigil_json_string_length(v), 5U);
    EXPECT_STREQ(emit_json(vigil_test_failed_, v), "\"hello\"");
    vigil_json_free(&v);
}

TEST(VigilJsonTest, StringWithEmbeddedNull)
{
    vigil_json_value_t *v = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_string_new(NULL, "a\0b", 3, &v, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_json_string_length(v), 3U);
    EXPECT_EQ(memcmp(vigil_json_string_value(v), "a\0b", 3), 0);
    vigil_json_free(&v);
}

/* ── Array operations ────────────────────────────────────────────── */

TEST(VigilJsonTest, EmptyArray)
{
    vigil_json_value_t *a = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_array_new(NULL, &a, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_json_array_count(a), 0U);
    EXPECT_EQ(vigil_json_array_get(a, 0), NULL);
    EXPECT_STREQ(emit_json(vigil_test_failed_, a), "[]");
    vigil_json_free(&a);
}

TEST(VigilJsonTest, ArrayPushAndGet)
{
    vigil_json_value_t *a = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_array_new(NULL, &a, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *n = NULL;
    vigil_json_number_new(NULL, 1.0, &n, &error);
    ASSERT_EQ(vigil_json_array_push(a, n, &error), VIGIL_STATUS_OK);

    vigil_json_number_new(NULL, 2.0, &n, &error);
    ASSERT_EQ(vigil_json_array_push(a, n, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_json_array_count(a), 2U);
    EXPECT_DOUBLE_EQ(vigil_json_number_value(vigil_json_array_get(a, 0)), 1.0);
    EXPECT_DOUBLE_EQ(vigil_json_number_value(vigil_json_array_get(a, 1)), 2.0);
    EXPECT_STREQ(emit_json(vigil_test_failed_, a), "[1,2]");
    vigil_json_free(&a);
}

/* ── Object operations ───────────────────────────────────────────── */

TEST(VigilJsonTest, EmptyObject)
{
    vigil_json_value_t *o = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_object_new(NULL, &o, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_json_object_count(o), 0U);
    EXPECT_EQ(vigil_json_object_get(o, "x"), NULL);
    EXPECT_STREQ(emit_json(vigil_test_failed_, o), "{}");
    vigil_json_free(&o);
}

TEST(VigilJsonTest, ObjectSetAndGet)
{
    vigil_json_value_t *o = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_object_new(NULL, &o, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *v = NULL;
    vigil_json_number_new(NULL, 42.0, &v, &error);
    ASSERT_EQ(vigil_json_object_set(o, "answer", 6, v, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_json_object_count(o), 1U);
    const vigil_json_value_t *got = vigil_json_object_get(o, "answer");
    ASSERT_NE(got, NULL);
    EXPECT_DOUBLE_EQ(vigil_json_number_value(got), 42.0);
    EXPECT_STREQ(emit_json(vigil_test_failed_, o), "{\"answer\":42}");
    vigil_json_free(&o);
}

TEST(VigilJsonTest, ObjectReplaceKey)
{
    vigil_json_value_t *o = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_object_new(NULL, &o, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *v1 = NULL, *v2 = NULL;
    vigil_json_number_new(NULL, 1.0, &v1, &error);
    vigil_json_number_new(NULL, 2.0, &v2, &error);
    vigil_json_object_set(o, "k", 1, v1, &error);
    vigil_json_object_set(o, "k", 1, v2, &error);

    EXPECT_EQ(vigil_json_object_count(o), 1U);
    EXPECT_DOUBLE_EQ(vigil_json_number_value(vigil_json_object_get(o, "k")), 2.0);
    vigil_json_free(&o);
}

TEST(VigilJsonTest, ObjectEntryIteration)
{
    vigil_json_value_t *o = NULL;
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_json_object_new(NULL, &o, &error), VIGIL_STATUS_OK);

    vigil_json_value_t *v = NULL;
    vigil_json_string_new(NULL, "val", 3, &v, &error);
    vigil_json_object_set(o, "key", 3, v, &error);

    const char *key = NULL;
    size_t key_len = 0;
    const vigil_json_value_t *entry_val = NULL;
    ASSERT_EQ(vigil_json_object_entry(o, 0, &key, &key_len, &entry_val), VIGIL_STATUS_OK);
    EXPECT_EQ(key_len, 3U);
    EXPECT_EQ(memcmp(key, "key", 3), 0);
    EXPECT_STREQ(vigil_json_string_value(entry_val), "val");
    vigil_json_free(&o);
}

/* ── Parse: scalars ──────────────────────────────────────────────── */

TEST(VigilJsonTest, ParseNull)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "null");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(vigil_json_type(v), VIGIL_JSON_NULL);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseBool)
{
    vigil_json_value_t *t = parse(vigil_test_failed_, "true");
    vigil_json_value_t *f = parse(vigil_test_failed_, "false");
    EXPECT_EQ(vigil_json_bool_value(t), 1);
    EXPECT_EQ(vigil_json_bool_value(f), 0);
    vigil_json_free(&t);
    vigil_json_free(&f);
}

TEST(VigilJsonTest, ParseIntegers)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "0");
    EXPECT_DOUBLE_EQ(vigil_json_number_value(v), 0.0);
    vigil_json_free(&v);

    v = parse(vigil_test_failed_, "42");
    EXPECT_DOUBLE_EQ(vigil_json_number_value(v), 42.0);
    vigil_json_free(&v);

    v = parse(vigil_test_failed_, "-7");
    EXPECT_DOUBLE_EQ(vigil_json_number_value(v), -7.0);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseFloats)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "3.14");
    EXPECT_NEAR(vigil_json_number_value(v), 3.14, 1e-10);
    vigil_json_free(&v);

    v = parse(vigil_test_failed_, "-0.5");
    EXPECT_DOUBLE_EQ(vigil_json_number_value(v), -0.5);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseExponent)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "1e10");
    EXPECT_DOUBLE_EQ(vigil_json_number_value(v), 1e10);
    vigil_json_free(&v);

    v = parse(vigil_test_failed_, "2.5E-3");
    EXPECT_DOUBLE_EQ(vigil_json_number_value(v), 2.5e-3);
    vigil_json_free(&v);

    v = parse(vigil_test_failed_, "-1.5e+2");
    EXPECT_DOUBLE_EQ(vigil_json_number_value(v), -150.0);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseString)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "\"hello world\"");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(vigil_json_type(v), VIGIL_JSON_STRING);
    EXPECT_STREQ(vigil_json_string_value(v), "hello world");
    EXPECT_EQ(vigil_json_string_length(v), 11U);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseEmptyString)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "\"\"");
    EXPECT_EQ(vigil_json_string_length(v), 0U);
    EXPECT_STREQ(vigil_json_string_value(v), "");
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseStringEscapes)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "\"a\\nb\\tc\\\\d\\\"e\\/f\"");
    EXPECT_STREQ(vigil_json_string_value(v), "a\nb\tc\\d\"e/f");
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseStringUnicodeEscape)
{
    /* \u0041 = 'A' */
    vigil_json_value_t *v = parse(vigil_test_failed_, "\"\\u0041\"");
    EXPECT_STREQ(vigil_json_string_value(v), "A");
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseStringUnicodeMultibyte)
{
    /* \u00E9 = 'é' (2-byte UTF-8: 0xC3 0xA9) */
    vigil_json_value_t *v = parse(vigil_test_failed_, "\"\\u00e9\"");
    EXPECT_EQ(vigil_json_string_length(v), 2U);
    EXPECT_EQ((unsigned char)vigil_json_string_value(v)[0], 0xC3);
    EXPECT_EQ((unsigned char)vigil_json_string_value(v)[1], 0xA9);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseStringUnicode3Byte)
{
    /* \u4E16 = '世' (3-byte UTF-8) */
    vigil_json_value_t *v = parse(vigil_test_failed_, "\"\\u4e16\"");
    EXPECT_EQ(vigil_json_string_length(v), 3U);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseStringSurrogatePair)
{
    /* \uD83D\uDE00 = U+1F600 '😀' (4-byte UTF-8) */
    vigil_json_value_t *v = parse(vigil_test_failed_, "\"\\uD83D\\uDE00\"");
    EXPECT_EQ(vigil_json_string_length(v), 4U);
    EXPECT_EQ((unsigned char)vigil_json_string_value(v)[0], 0xF0);
    EXPECT_EQ((unsigned char)vigil_json_string_value(v)[1], 0x9F);
    EXPECT_EQ((unsigned char)vigil_json_string_value(v)[2], 0x98);
    EXPECT_EQ((unsigned char)vigil_json_string_value(v)[3], 0x80);
    vigil_json_free(&v);
}

/* ── Parse: arrays ───────────────────────────────────────────────── */

TEST(VigilJsonTest, ParseEmptyArray)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "[]");
    EXPECT_EQ(vigil_json_type(v), VIGIL_JSON_ARRAY);
    EXPECT_EQ(vigil_json_array_count(v), 0U);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseArray)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "[1, \"two\", true, null]");
    ASSERT_EQ(vigil_json_array_count(v), 4U);
    EXPECT_DOUBLE_EQ(vigil_json_number_value(vigil_json_array_get(v, 0)), 1.0);
    EXPECT_STREQ(vigil_json_string_value(vigil_json_array_get(v, 1)), "two");
    EXPECT_EQ(vigil_json_bool_value(vigil_json_array_get(v, 2)), 1);
    EXPECT_EQ(vigil_json_type(vigil_json_array_get(v, 3)), VIGIL_JSON_NULL);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseNestedArray)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "[[1,2],[3]]");
    ASSERT_EQ(vigil_json_array_count(v), 2U);
    EXPECT_EQ(vigil_json_array_count(vigil_json_array_get(v, 0)), 2U);
    EXPECT_EQ(vigil_json_array_count(vigil_json_array_get(v, 1)), 1U);
    vigil_json_free(&v);
}

/* ── Parse: objects ──────────────────────────────────────────────── */

TEST(VigilJsonTest, ParseEmptyObject)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "{}");
    EXPECT_EQ(vigil_json_type(v), VIGIL_JSON_OBJECT);
    EXPECT_EQ(vigil_json_object_count(v), 0U);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseObject)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "{\"name\": \"vigil\", \"version\": 1}");
    ASSERT_EQ(vigil_json_object_count(v), 2U);
    EXPECT_STREQ(vigil_json_string_value(vigil_json_object_get(v, "name")), "vigil");
    EXPECT_DOUBLE_EQ(vigil_json_number_value(vigil_json_object_get(v, "version")), 1.0);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, ParseNestedObject)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "{\"a\":{\"b\":true}}");
    const vigil_json_value_t *inner = vigil_json_object_get(v, "a");
    ASSERT_NE(inner, NULL);
    EXPECT_EQ(vigil_json_bool_value(vigil_json_object_get(inner, "b")), 1);
    vigil_json_free(&v);
}

/* ── Parse: whitespace ───────────────────────────────────────────── */

TEST(VigilJsonTest, ParseWhitespace)
{
    vigil_json_value_t *v = parse(vigil_test_failed_, "  \n\t { \n \"x\" : 1 \n } \n ");
    EXPECT_EQ(vigil_json_type(v), VIGIL_JSON_OBJECT);
    EXPECT_DOUBLE_EQ(vigil_json_number_value(vigil_json_object_get(v, "x")), 1.0);
    vigil_json_free(&v);
}

/* ── Parse: errors ───────────────────────────────────────────────── */

TEST(VigilJsonTest, ParseErrorEmpty)
{
    parse_fail(vigil_test_failed_, "");
}
TEST(VigilJsonTest, ParseErrorTrailing)
{
    parse_fail(vigil_test_failed_, "true false");
}
TEST(VigilJsonTest, ParseErrorBadToken)
{
    parse_fail(vigil_test_failed_, "undefined");
}
TEST(VigilJsonTest, ParseErrorUntermString)
{
    parse_fail(vigil_test_failed_, "\"hello");
}
TEST(VigilJsonTest, ParseErrorBadEscape)
{
    parse_fail(vigil_test_failed_, "\"\\x\"");
}
TEST(VigilJsonTest, ParseErrorBadUnicode)
{
    parse_fail(vigil_test_failed_, "\"\\u00GG\"");
}
TEST(VigilJsonTest, ParseErrorMissingSurrogate)
{
    parse_fail(vigil_test_failed_, "\"\\uD83D\"");
}
TEST(VigilJsonTest, ParseErrorBadSurrogate)
{
    parse_fail(vigil_test_failed_, "\"\\uD83D\\u0041\"");
}
TEST(VigilJsonTest, ParseErrorArrayNoClose)
{
    parse_fail(vigil_test_failed_, "[1, 2");
}
TEST(VigilJsonTest, ParseErrorObjectNoClose)
{
    parse_fail(vigil_test_failed_, "{\"a\": 1");
}
TEST(VigilJsonTest, ParseErrorObjectNoColon)
{
    parse_fail(vigil_test_failed_, "{\"a\" 1}");
}
TEST(VigilJsonTest, ParseErrorObjectBadKey)
{
    parse_fail(vigil_test_failed_, "{1: 2}");
}
TEST(VigilJsonTest, ParseErrorNumberLeadingZero)
{
    /* Leading zeros are not valid JSON: 01 should fail. */
    /* Actually, our parser accepts "01" as "0" then trailing "1".
       That's caught by the trailing-content check. */
    parse_fail(vigil_test_failed_, "01");
}

/* ── Roundtrip ───────────────────────────────────────────────────── */

TEST(VigilJsonTest, RoundtripComplex)
{
    const char *input = "{\"name\":\"vigil\",\"version\":1,\"features\":[\"vm\",\"debugger\"],\"config\":{\"debug\":"
                        "true,\"opt\":null}}";
    vigil_json_value_t *v = parse(vigil_test_failed_, input);
    ASSERT_NE(v, NULL);
    char *output = emit_json(vigil_test_failed_, v);
    EXPECT_STREQ(output, input);
    vigil_json_free(&v);
}

TEST(VigilJsonTest, RoundtripEscapes)
{
    const char *input = "\"line1\\nline2\\ttab\\\\backslash\\\"quote\"";
    vigil_json_value_t *v = parse(vigil_test_failed_, input);
    char *output = emit_json(vigil_test_failed_, v);
    EXPECT_STREQ(output, input);
    vigil_json_free(&v);
}

/* ── Custom allocator ────────────────────────────────────────────── */

TEST(VigilJsonTest, CustomAllocator)
{
    g_alloc_count = 0;
    g_dealloc_count = 0;
    vigil_allocator_t a = tracking_allocator();
    vigil_error_t error = {0};

    vigil_json_value_t *v = NULL;
    const char *input = "{\"key\":[1,2,3]}";
    ASSERT_EQ(vigil_json_parse(&a, input, strlen(input), &v, &error), VIGIL_STATUS_OK);

    /* Emit also uses the value's stored allocator. */
    char *str = NULL;
    size_t len = 0;
    ASSERT_EQ(vigil_json_emit(v, &str, &len, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(str, input);

    /* Free the emitted string through the tracking allocator. */
    a.deallocate(a.user_data, str);

    size_t allocs_before_free = g_alloc_count;
    vigil_json_free(&v);

    /* Verify allocator was actually used. */
    EXPECT_GT(allocs_before_free, 0U);
    EXPECT_GT(g_dealloc_count, 0U);
}

/* ── Null safety ─────────────────────────────────────────────────── */

TEST(VigilJsonTest, NullSafety)
{
    /* These should not crash. */
    vigil_json_free(NULL);
    vigil_json_value_t *null_val = NULL;
    vigil_json_free(&null_val);

    EXPECT_EQ(vigil_json_type(NULL), VIGIL_JSON_NULL);
    EXPECT_EQ(vigil_json_bool_value(NULL), 0);
    EXPECT_DOUBLE_EQ(vigil_json_number_value(NULL), 0.0);
    EXPECT_STREQ(vigil_json_string_value(NULL), "");
    EXPECT_EQ(vigil_json_string_length(NULL), 0U);
    EXPECT_EQ(vigil_json_array_count(NULL), 0U);
    EXPECT_EQ(vigil_json_array_get(NULL, 0), NULL);
    EXPECT_EQ(vigil_json_object_count(NULL), 0U);
    EXPECT_EQ(vigil_json_object_get(NULL, "x"), NULL);
}

TEST(VigilJsonTest, InvalidArguments)
{
    vigil_error_t error = {0};
    EXPECT_EQ(vigil_json_null_new(NULL, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_json_parse(NULL, NULL, 0, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(vigil_json_emit(NULL, NULL, NULL, &error), VIGIL_STATUS_INVALID_ARGUMENT);
}

/* ── Emit: control character escaping ────────────────────────────── */

TEST(VigilJsonTest, EmitControlCharacters)
{
    vigil_json_value_t *v = NULL;
    vigil_error_t error = {0};
    /* String with a control character (0x01). */
    ASSERT_EQ(vigil_json_string_new(NULL, "\x01", 1, &v, &error), VIGIL_STATUS_OK);
    char *out = emit_json(vigil_test_failed_, v);
    EXPECT_STREQ(out, "\"\\u0001\"");
    vigil_json_free(&v);
}

void register_json_tests(void)
{
    REGISTER_TEST(VigilJsonTest, NullValue);
    REGISTER_TEST(VigilJsonTest, BoolValues);
    REGISTER_TEST(VigilJsonTest, NumberValues);
    REGISTER_TEST(VigilJsonTest, StringValue);
    REGISTER_TEST(VigilJsonTest, StringWithEmbeddedNull);
    REGISTER_TEST(VigilJsonTest, EmptyArray);
    REGISTER_TEST(VigilJsonTest, ArrayPushAndGet);
    REGISTER_TEST(VigilJsonTest, EmptyObject);
    REGISTER_TEST(VigilJsonTest, ObjectSetAndGet);
    REGISTER_TEST(VigilJsonTest, ObjectReplaceKey);
    REGISTER_TEST(VigilJsonTest, ObjectEntryIteration);
    REGISTER_TEST(VigilJsonTest, ParseNull);
    REGISTER_TEST(VigilJsonTest, ParseBool);
    REGISTER_TEST(VigilJsonTest, ParseIntegers);
    REGISTER_TEST(VigilJsonTest, ParseFloats);
    REGISTER_TEST(VigilJsonTest, ParseExponent);
    REGISTER_TEST(VigilJsonTest, ParseString);
    REGISTER_TEST(VigilJsonTest, ParseEmptyString);
    REGISTER_TEST(VigilJsonTest, ParseStringEscapes);
    REGISTER_TEST(VigilJsonTest, ParseStringUnicodeEscape);
    REGISTER_TEST(VigilJsonTest, ParseStringUnicodeMultibyte);
    REGISTER_TEST(VigilJsonTest, ParseStringUnicode3Byte);
    REGISTER_TEST(VigilJsonTest, ParseStringSurrogatePair);
    REGISTER_TEST(VigilJsonTest, ParseEmptyArray);
    REGISTER_TEST(VigilJsonTest, ParseArray);
    REGISTER_TEST(VigilJsonTest, ParseNestedArray);
    REGISTER_TEST(VigilJsonTest, ParseEmptyObject);
    REGISTER_TEST(VigilJsonTest, ParseObject);
    REGISTER_TEST(VigilJsonTest, ParseNestedObject);
    REGISTER_TEST(VigilJsonTest, ParseWhitespace);
    REGISTER_TEST(VigilJsonTest, ParseErrorEmpty);
    REGISTER_TEST(VigilJsonTest, ParseErrorTrailing);
    REGISTER_TEST(VigilJsonTest, ParseErrorBadToken);
    REGISTER_TEST(VigilJsonTest, ParseErrorUntermString);
    REGISTER_TEST(VigilJsonTest, ParseErrorBadEscape);
    REGISTER_TEST(VigilJsonTest, ParseErrorBadUnicode);
    REGISTER_TEST(VigilJsonTest, ParseErrorMissingSurrogate);
    REGISTER_TEST(VigilJsonTest, ParseErrorBadSurrogate);
    REGISTER_TEST(VigilJsonTest, ParseErrorArrayNoClose);
    REGISTER_TEST(VigilJsonTest, ParseErrorObjectNoClose);
    REGISTER_TEST(VigilJsonTest, ParseErrorObjectNoColon);
    REGISTER_TEST(VigilJsonTest, ParseErrorObjectBadKey);
    REGISTER_TEST(VigilJsonTest, ParseErrorNumberLeadingZero);
    REGISTER_TEST(VigilJsonTest, RoundtripComplex);
    REGISTER_TEST(VigilJsonTest, RoundtripEscapes);
    REGISTER_TEST(VigilJsonTest, CustomAllocator);
    REGISTER_TEST(VigilJsonTest, NullSafety);
    REGISTER_TEST(VigilJsonTest, InvalidArguments);
    REGISTER_TEST(VigilJsonTest, EmitControlCharacters);
}
