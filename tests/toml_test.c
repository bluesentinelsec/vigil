#include "vigil_test.h"
#include <math.h>
#include <string.h>

#include "vigil/toml.h"

/* ── Fixture ─────────────────────────────────────────────────────── */

typedef struct TomlTest
{
    vigil_toml_value_t *root;
    vigil_error_t error;
} TomlTest;

void TomlTest_SetUp(void *p)
{
    TomlTest *self = (TomlTest *)p;
    self->root = NULL;
    memset(&self->error, 0, sizeof(self->error));
}

void TomlTest_TearDown(void *p)
{
    TomlTest *self = (TomlTest *)p;
    vigil_toml_free(&self->root);
    vigil_error_clear(&self->error);
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static void toml_parse_helper(TomlTest *self, const char *input, int *vigil_test_failed_)
{
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_parse(NULL, input, strlen(input), &self->root, &self->error));
}

static vigil_status_t toml_emit_helper(TomlTest *self, char **out, size_t *len)
{
    return vigil_toml_emit(self->root, out, len, &self->error);
}

static int toml_output_contains(char *out, const char *needle)
{
    return out != NULL && strstr(out, needle) != NULL;
}

static void toml_reset_fixture(TomlTest *self)
{
    vigil_toml_free(&self->root);
    vigil_error_clear(&self->error);
    memset(&self->error, 0, sizeof(self->error));
}

static vigil_status_t toml_parse_and_emit(TomlTest *self, const char *input, char **out, size_t *len)
{
    vigil_status_t s;

    toml_reset_fixture(self);
    s = vigil_toml_parse(NULL, input, strlen(input), &self->root, &self->error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return toml_emit_helper(self, out, len);
}

static int toml_parse_emit_contains(TomlTest *self, const char *input, const char *needle)
{
    char *out = NULL;
    size_t len = 0;
    vigil_status_t s;
    int found;

    s = toml_parse_and_emit(self, input, &out, &len);
    if (s != VIGIL_STATUS_OK)
        return 0;
    found = toml_output_contains(out, needle);
    free(out);
    return found;
}

static vigil_status_t toml_add_integer_field(vigil_toml_value_t *table, const char *key, size_t key_len, int64_t value,
                                             vigil_error_t *error)
{
    vigil_status_t s;
    vigil_toml_value_t *field = NULL;

    s = vigil_toml_integer_new(NULL, value, &field, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = vigil_toml_table_set(table, key, key_len, field, error);
    if (s != VIGIL_STATUS_OK)
        vigil_toml_free(&field);
    return s;
}

static vigil_status_t toml_add_bool_field(vigil_toml_value_t *table, const char *key, size_t key_len, int value,
                                          vigil_error_t *error)
{
    vigil_status_t s;
    vigil_toml_value_t *field = NULL;

    s = vigil_toml_bool_new(NULL, value, &field, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = vigil_toml_table_set(table, key, key_len, field, error);
    if (s != VIGIL_STATUS_OK)
        vigil_toml_free(&field);
    return s;
}

static vigil_status_t toml_add_string_field(vigil_toml_value_t *table, const char *key, size_t key_len,
                                            const char *value, size_t value_len, vigil_error_t *error)
{
    vigil_status_t s;
    vigil_toml_value_t *field = NULL;

    s = vigil_toml_string_new(NULL, value, value_len, &field, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = vigil_toml_table_set(table, key, key_len, field, error);
    if (s != VIGIL_STATUS_OK)
        vigil_toml_free(&field);
    return s;
}

static vigil_status_t toml_build_quoted_section_root(TomlTest *self)
{
    vigil_status_t s;
    vigil_toml_value_t *section = NULL;

    toml_reset_fixture(self);
    s = vigil_toml_table_new(NULL, &self->root, &self->error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = vigil_toml_table_new(NULL, &section, &self->error);
    if (s != VIGIL_STATUS_OK)
        return s;
    s = toml_add_integer_field(section, "value", 5, 1, &self->error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;
    s = toml_add_string_field(section, "name", 4, "demo", 4, &self->error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;
    s = toml_add_bool_field(section, "enabled", 7, 1, &self->error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;
    s = toml_add_integer_field(section, "count", 5, 4, &self->error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;
    s = vigil_toml_table_set(self->root, "quoted key", 10, section, &self->error);
    if (s != VIGIL_STATUS_OK)
        goto cleanup;
    section = NULL;

cleanup:
    vigil_toml_free(&section);
    return s;
}

static int toml_quoted_section_output_contains(TomlTest *self, const char *needle)
{
    char *out = NULL;
    size_t len = 0;
    vigil_status_t s;
    int found;

    s = toml_build_quoted_section_root(self);
    if (s != VIGIL_STATUS_OK)
        return 0;
    s = toml_emit_helper(self, &out, &len);
    if (s != VIGIL_STATUS_OK)
        return 0;
    found = toml_output_contains(out, needle);
    free(out);
    return found;
}

/* ── Basic key/value ─────────────────────────────────────────────── */

TEST_F(TomlTest, StringValue)
{
    toml_parse_helper(FIXTURE(TomlTest), "name = \"vigil\"", vigil_test_failed_);
    const vigil_toml_value_t *v = vigil_toml_table_get(FIXTURE(TomlTest)->root, "name");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(vigil_toml_type(v), VIGIL_TOML_STRING);
    EXPECT_STREQ(vigil_toml_string_value(v), "vigil");
}

TEST_F(TomlTest, IntegerValue)
{
    toml_parse_helper(FIXTURE(TomlTest), "port = 8080", vigil_test_failed_);
    const vigil_toml_value_t *v = vigil_toml_table_get(FIXTURE(TomlTest)->root, "port");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(vigil_toml_type(v), VIGIL_TOML_INTEGER);
    EXPECT_EQ(vigil_toml_integer_value(v), 8080);
}

TEST_F(TomlTest, NegativeInteger)
{
    toml_parse_helper(FIXTURE(TomlTest), "offset = -42", vigil_test_failed_);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "offset")), -42);
}

TEST_F(TomlTest, HexOctBin)
{
    toml_parse_helper(FIXTURE(TomlTest), "hex = 0xDEAD\noct = 0o755\nbin = 0b11010110", vigil_test_failed_);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "hex")), 0xDEAD);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "oct")), 0755);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "bin")), 0xD6);
}

TEST_F(TomlTest, IntegerUnderscores)
{
    toml_parse_helper(FIXTURE(TomlTest), "big = 1_000_000", vigil_test_failed_);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "big")), 1000000);
}

TEST_F(TomlTest, FloatValue)
{
    toml_parse_helper(FIXTURE(TomlTest), "pi = 3.14159", vigil_test_failed_);
    const vigil_toml_value_t *v = vigil_toml_table_get(FIXTURE(TomlTest)->root, "pi");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(vigil_toml_type(v), VIGIL_TOML_FLOAT);
    EXPECT_DOUBLE_EQ(vigil_toml_float_value(v), 3.14159);
}

TEST_F(TomlTest, FloatExponent)
{
    toml_parse_helper(FIXTURE(TomlTest), "big = 5e+22", vigil_test_failed_);
    EXPECT_DOUBLE_EQ(vigil_toml_float_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "big")), 5e+22);
}

TEST_F(TomlTest, FloatInfNan)
{
    toml_parse_helper(FIXTURE(TomlTest), "pos_inf = inf\nneg_inf = -inf\nnan_val = nan", vigil_test_failed_);
    EXPECT_DOUBLE_EQ(vigil_toml_float_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "pos_inf")), HUGE_VAL);
    EXPECT_DOUBLE_EQ(vigil_toml_float_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "neg_inf")), -HUGE_VAL);
    EXPECT_TRUE(isnan(vigil_toml_float_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "nan_val"))));
}

TEST_F(TomlTest, BoolValues)
{
    toml_parse_helper(FIXTURE(TomlTest), "enabled = true\ndisabled = false", vigil_test_failed_);
    EXPECT_EQ(vigil_toml_bool_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "enabled")), 1);
    EXPECT_EQ(vigil_toml_bool_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "disabled")), 0);
}

/* ── Strings ─────────────────────────────────────────────────────── */

TEST_F(TomlTest, BasicStringEscapes)
{
    toml_parse_helper(FIXTURE(TomlTest), "s = \"hello\\tworld\\n\"", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "s")), "hello\tworld\n");
}

TEST_F(TomlTest, BasicStringAdditionalEscapes)
{
    toml_parse_helper(FIXTURE(TomlTest), "s = \"\\b\\f\\r\\\"\\\\\"", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "s")), "\b\f\r\"\\");
}

TEST_F(TomlTest, UnicodeEscape)
{
    toml_parse_helper(FIXTURE(TomlTest), "s = \"\\u0041\\U00000042\"", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "s")), "AB");
}

TEST_F(TomlTest, LiteralString)
{
    toml_parse_helper(FIXTURE(TomlTest), "s = 'C:\\Users\\path'", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "s")), "C:\\Users\\path");
}

TEST_F(TomlTest, MultilineBasicString)
{
    toml_parse_helper(FIXTURE(TomlTest), "s = \"\"\"\nhello\nworld\"\"\"", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "s")), "hello\nworld");
}

TEST_F(TomlTest, MultilineBasicStringEscapes)
{
    toml_parse_helper(FIXTURE(TomlTest), "s = \"\"\"\nleft\\nright slash \\\\ tail\"\"\"", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "s")),
                 "left\nright slash \\ tail");
}

TEST_F(TomlTest, MultilineBasicStringExtraQuotes)
{
    toml_parse_helper(FIXTURE(TomlTest), "s = \"\"\"\nhello\"\"\"\"\"", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "s")), "hello\"\"");
}

TEST_F(TomlTest, MultilineLiteralString)
{
    toml_parse_helper(FIXTURE(TomlTest), "s = '''\nhello\nworld'''", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "s")), "hello\nworld");
}

TEST_F(TomlTest, MultilineLineContinuation)
{
    toml_parse_helper(FIXTURE(TomlTest), "s = \"\"\"\nhello \\\n  world\"\"\"", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "s")), "hello world");
}

TEST_F(TomlTest, EmptyStrings)
{
    toml_parse_helper(FIXTURE(TomlTest), "a = \"\"\nb = ''", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "a")), "");
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "b")), "");
}

/* ── Tables ──────────────────────────────────────────────────────── */

TEST_F(TomlTest, SimpleTable)
{
    toml_parse_helper(FIXTURE(TomlTest), "[server]\nhost = \"localhost\"\nport = 9090", vigil_test_failed_);
    const vigil_toml_value_t *server = vigil_toml_table_get(FIXTURE(TomlTest)->root, "server");
    ASSERT_NE(server, NULL);
    EXPECT_EQ(vigil_toml_type(server), VIGIL_TOML_TABLE);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(server, "host")), "localhost");
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(server, "port")), 9090);
}

TEST_F(TomlTest, NestedTable)
{
    toml_parse_helper(FIXTURE(TomlTest), "[a.b.c]\nval = 1", vigil_test_failed_);
    const vigil_toml_value_t *c = vigil_toml_table_get_path(FIXTURE(TomlTest)->root, "a.b.c");
    ASSERT_NE(c, NULL);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(c, "val")), 1);
}

TEST_F(TomlTest, DottedKeys)
{
    toml_parse_helper(FIXTURE(TomlTest), "a.b.c = 42", vigil_test_failed_);
    const vigil_toml_value_t *v = vigil_toml_table_get_path(FIXTURE(TomlTest)->root, "a.b.c");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(vigil_toml_integer_value(v), 42);
}

TEST_F(TomlTest, InlineTable)
{
    toml_parse_helper(FIXTURE(TomlTest), "point = { x = 1, y = 2 }", vigil_test_failed_);
    const vigil_toml_value_t *pt = vigil_toml_table_get(FIXTURE(TomlTest)->root, "point");
    ASSERT_NE(pt, NULL);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(pt, "x")), 1);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(pt, "y")), 2);
}

TEST_F(TomlTest, InlineTableDottedKeys)
{
    toml_parse_helper(FIXTURE(TomlTest), "point = { pos.x = 1, pos.y = 2 }", vigil_test_failed_);
    const vigil_toml_value_t *point = vigil_toml_table_get(FIXTURE(TomlTest)->root, "point");
    const vigil_toml_value_t *pos = vigil_toml_table_get(point, "pos");
    ASSERT_NE(pos, NULL);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(pos, "x")), 1);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(pos, "y")), 2);
}

TEST_F(TomlTest, QuotedKey)
{
    toml_parse_helper(FIXTURE(TomlTest), "\"key with spaces\" = true", vigil_test_failed_);
    EXPECT_EQ(vigil_toml_bool_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "key with spaces")), 1);
}

/* ── Arrays ──────────────────────────────────────────────────────── */

TEST_F(TomlTest, SimpleArray)
{
    toml_parse_helper(FIXTURE(TomlTest), "ports = [80, 443, 8080]", vigil_test_failed_);
    const vigil_toml_value_t *arr = vigil_toml_table_get(FIXTURE(TomlTest)->root, "ports");
    ASSERT_NE(arr, NULL);
    EXPECT_EQ(vigil_toml_type(arr), VIGIL_TOML_ARRAY);
    EXPECT_EQ(vigil_toml_array_count(arr), 3);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_array_get(arr, 0)), 80);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_array_get(arr, 1)), 443);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_array_get(arr, 2)), 8080);
}

TEST_F(TomlTest, StringArray)
{
    toml_parse_helper(FIXTURE(TomlTest), "names = [\"alice\", \"bob\"]", vigil_test_failed_);
    const vigil_toml_value_t *arr = vigil_toml_table_get(FIXTURE(TomlTest)->root, "names");
    EXPECT_EQ(vigil_toml_array_count(arr), 2);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_array_get(arr, 0)), "alice");
}

TEST_F(TomlTest, MultilineArray)
{
    toml_parse_helper(FIXTURE(TomlTest), "a = [\n  1,\n  2,\n  3,\n]", vigil_test_failed_);
    EXPECT_EQ(vigil_toml_array_count(vigil_toml_table_get(FIXTURE(TomlTest)->root, "a")), 3);
}

TEST_F(TomlTest, NestedArray)
{
    toml_parse_helper(FIXTURE(TomlTest), "a = [[1, 2], [3, 4]]", vigil_test_failed_);
    const vigil_toml_value_t *a = vigil_toml_table_get(FIXTURE(TomlTest)->root, "a");
    EXPECT_EQ(vigil_toml_array_count(a), 2);
    EXPECT_EQ(vigil_toml_array_count(vigil_toml_array_get(a, 0)), 2);
}

/* ── Array of tables ─────────────────────────────────────────────── */

TEST_F(TomlTest, ArrayOfTables)
{
    toml_parse_helper(FIXTURE(TomlTest), "[[products]]\nname = \"hammer\"\n\n[[products]]\nname = \"nail\"",
                      vigil_test_failed_);
    const vigil_toml_value_t *arr = vigil_toml_table_get(FIXTURE(TomlTest)->root, "products");
    ASSERT_NE(arr, NULL);
    EXPECT_EQ(vigil_toml_type(arr), VIGIL_TOML_ARRAY);
    EXPECT_EQ(vigil_toml_array_count(arr), 2);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(vigil_toml_array_get(arr, 0), "name")), "hammer");
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(vigil_toml_array_get(arr, 1), "name")), "nail");
}

/* ── DateTime ────────────────────────────────────────────────────── */

TEST_F(TomlTest, OffsetDateTime)
{
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15T10:30:00Z", vigil_test_failed_);
    const vigil_toml_value_t *v = vigil_toml_table_get(FIXTURE(TomlTest)->root, "dt");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(vigil_toml_type(v), VIGIL_TOML_DATETIME);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(v);
    EXPECT_EQ(dt->year, 2024);
    EXPECT_EQ(dt->month, 1);
    EXPECT_EQ(dt->day, 15);
    EXPECT_EQ(dt->hour, 10);
    EXPECT_EQ(dt->minute, 30);
    EXPECT_EQ(dt->second, 0);
    EXPECT_TRUE(dt->has_date);
    EXPECT_TRUE(dt->has_time);
    EXPECT_TRUE(dt->has_offset);
    EXPECT_EQ(dt->offset_minutes, 0);
}

TEST_F(TomlTest, OffsetDateTimeLowercaseSeparatorAndOffset)
{
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15t10:30:00z", vigil_test_failed_);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "dt"));
    EXPECT_TRUE(dt->has_date);
    EXPECT_TRUE(dt->has_time);
    EXPECT_TRUE(dt->has_offset);
    EXPECT_EQ(dt->offset_minutes, 0);
}

TEST_F(TomlTest, OffsetDateTimeWithOffset)
{
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15T10:30:00-05:00", vigil_test_failed_);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "dt"));
    EXPECT_EQ(dt->offset_minutes, -300);
}

TEST_F(TomlTest, LocalDateTime)
{
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15T10:30:00", vigil_test_failed_);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "dt"));
    EXPECT_TRUE(dt->has_date);
    EXPECT_TRUE(dt->has_time);
    EXPECT_FALSE(dt->has_offset);
}

TEST_F(TomlTest, LocalDateTimeWithSpaceSeparator)
{
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15 10:30:00", vigil_test_failed_);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "dt"));
    EXPECT_TRUE(dt->has_date);
    EXPECT_TRUE(dt->has_time);
    EXPECT_FALSE(dt->has_offset);
}

TEST_F(TomlTest, LocalDate)
{
    toml_parse_helper(FIXTURE(TomlTest), "d = 2024-01-15", vigil_test_failed_);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "d"));
    EXPECT_TRUE(dt->has_date);
    EXPECT_FALSE(dt->has_time);
    EXPECT_EQ(dt->year, 2024);
    EXPECT_EQ(dt->month, 1);
    EXPECT_EQ(dt->day, 15);
}

TEST_F(TomlTest, LocalTime)
{
    toml_parse_helper(FIXTURE(TomlTest), "t = 10:30:00", vigil_test_failed_);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "t"));
    EXPECT_FALSE(dt->has_date);
    EXPECT_TRUE(dt->has_time);
    EXPECT_EQ(dt->hour, 10);
    EXPECT_EQ(dt->minute, 30);
}

TEST_F(TomlTest, FractionalSeconds)
{
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15T10:30:00.123456789Z", vigil_test_failed_);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "dt"));
    EXPECT_EQ(dt->nanosecond, 123456789);
}

TEST_F(TomlTest, LocalTimeFractionalSecondsPadNanoseconds)
{
    toml_parse_helper(FIXTURE(TomlTest), "t = 10:30:00.1", vigil_test_failed_);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "t"));
    ASSERT_NE(dt, NULL);
    EXPECT_EQ(dt->nanosecond, 100000000);
}

TEST_F(TomlTest, LocalTimeFractionalSecondsTrimExtraPrecision)
{
    toml_parse_helper(FIXTURE(TomlTest), "t = 10:30:00.123456789123", vigil_test_failed_);
    const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "t"));
    ASSERT_NE(dt, NULL);
    EXPECT_EQ(dt->nanosecond, 123456789);
}

TEST_F(TomlTest, InvalidDateTimeComponents)
{
    const char *cases[] = {
        "dt = 2024-01-15T1:30:00",       "dt = 2024-01-15T10-30:00",       "dt = 2024-01-15T10:3:00",
        "dt = 2024-01-15T10:30-00",      "dt = 2024-01-15T10:30:0",        "dt = 2024-01-15T10:30:00+0a:00",
        "dt = 2024-01-15T10:30:00+0500", "dt = 2024-01-15T10:30:00+05:0a",
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        vigil_toml_value_t *root = NULL;
        vigil_error_t error;

        memset(&error, 0, sizeof(error));
        EXPECT_NE(VIGIL_STATUS_OK, vigil_toml_parse(NULL, cases[i], strlen(cases[i]), &root, &error));
        vigil_toml_free(&root);
        vigil_error_clear(&error);
    }
}

/* ── Comments ────────────────────────────────────────────────────── */

TEST_F(TomlTest, Comments)
{
    toml_parse_helper(FIXTURE(TomlTest), "# full line comment\nkey = \"value\" # inline comment\n", vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "key")), "value");
}

/* ── Error cases ─────────────────────────────────────────────────── */

TEST_F(TomlTest, DuplicateKeyError)
{
    vigil_status_t s = vigil_toml_parse(NULL, "a = 1\na = 2", 11, &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, UnterminatedString)
{
    vigil_status_t s = vigil_toml_parse(NULL, "a = \"hello", 10, &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, InvalidBasicStringEscape)
{
    const char *input = "a = \"\\q\"";
    vigil_status_t s =
        vigil_toml_parse(NULL, input, strlen(input), &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, InlineTableKeyConflictError)
{
    const char *input = "point = { pos = 1, pos.x = 2 }";
    vigil_status_t s =
        vigil_toml_parse(NULL, input, strlen(input), &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, TableHeaderMissingKey)
{
    vigil_status_t s = vigil_toml_parse(NULL, "[]\n", 3, &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, TableHeaderMissingClosingBracket)
{
    const char *input = "[table\n";
    vigil_status_t s =
        vigil_toml_parse(NULL, input, strlen(input), &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, TableHeaderConflictsWithScalar)
{
    const char *input = "a = 1\n[a]\n";
    vigil_status_t s =
        vigil_toml_parse(NULL, input, strlen(input), &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, ArrayTableParentConflict)
{
    const char *input = "a = 1\n[[a.b]]\n";
    vigil_status_t s =
        vigil_toml_parse(NULL, input, strlen(input), &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, ArrayTableConflictsWithTable)
{
    const char *input = "[a]\n[[a]]\n";
    vigil_status_t s =
        vigil_toml_parse(NULL, input, strlen(input), &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, DottedKeysReuseExistingTable)
{
    toml_parse_helper(FIXTURE(TomlTest), "a.b = 1\na.c = 2\n", vigil_test_failed_);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get_path(FIXTURE(TomlTest)->root, "a.b")), 1);
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get_path(FIXTURE(TomlTest)->root, "a.c")), 2);
}

TEST_F(TomlTest, DottedKeyConflictError)
{
    const char *input = "a = 1\na.b = 2\n";
    vigil_status_t s =
        vigil_toml_parse(NULL, input, strlen(input), &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, MissingEqualsError)
{
    vigil_status_t s = vigil_toml_parse(NULL, "a 1\n", 4, &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

TEST_F(TomlTest, InvalidKeyValueKey)
{
    vigil_status_t s = vigil_toml_parse(NULL, "= 1\n", 4, &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, VIGIL_STATUS_OK);
}

/* ── Emitter ─────────────────────────────────────────────────────── */

TEST_F(TomlTest, EmitRoundTrip)
{
    toml_parse_helper(FIXTURE(TomlTest), "name = \"vigil\"\nversion = \"0.1.0\"", vigil_test_failed_);
    char *out = NULL;
    size_t len = 0;
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_emit(FIXTURE(TomlTest)->root, &out, &len, &FIXTURE(TomlTest)->error));
    ASSERT_NE(out, NULL);
    EXPECT_NE(strstr(out, "name = \"vigil\""), NULL);
    EXPECT_NE(strstr(out, "version = \"0.1.0\""), NULL);
    free(out);
}

TEST_F(TomlTest, EmitTable)
{
    toml_parse_helper(FIXTURE(TomlTest), "[deps]\njson = \"1.0\"\nhttp = \"2.0\"\nlog = \"0.5\"\ntest = \"1.1\"",
                      vigil_test_failed_);
    char *out = NULL;
    size_t len = 0;
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_emit(FIXTURE(TomlTest)->root, &out, &len, &FIXTURE(TomlTest)->error));
    ASSERT_NE(out, NULL);
    EXPECT_NE(strstr(out, "[deps]"), NULL);
    EXPECT_NE(strstr(out, "json = \"1.0\""), NULL);
    free(out);
}

TEST_F(TomlTest, EmitFormatsNumbersAndBooleans)
{
    const char *input = "whole = 1\n"
                        "floaty = 2.0\n"
                        "nan_val = nan\n"
                        "pos_inf = inf\n"
                        "neg_inf = -inf\n"
                        "flag = true\n";

    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "whole = 1"));
    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "floaty = 2.0"));
    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "nan_val = nan"));
    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "pos_inf = inf"));
    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "neg_inf = -inf"));
    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "flag = true"));
}

TEST_F(TomlTest, EmitFormatsDateTimes)
{
    const char *input = "stamp = 2024-01-15T10:30:00.100000000Z\n"
                        "offset = 2024-01-15T10:30:00-05:30\n"
                        "clock = 10:30:00.123400000\n";

    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "stamp = 2024-01-15T10:30:00.1Z"));
    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "offset = 2024-01-15T10:30:00-05:30"));
    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "clock = 10:30:00.1234"));
}

TEST_F(TomlTest, EmitFormatsArraysAndInlineTables)
{
    const char *input = "items = [1, 2]\n"
                        "point = { x = 1, y = 2 }\n";

    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "items = [1, 2]"));
    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "point = { x = 1, y = 2 }"));
}

TEST_F(TomlTest, EmitQuotedSectionHeader)
{
    EXPECT_TRUE(toml_quoted_section_output_contains(FIXTURE(TomlTest), "[\"quoted key\"]"));
    EXPECT_TRUE(toml_quoted_section_output_contains(FIXTURE(TomlTest), "enabled = true"));
}

TEST_F(TomlTest, EmitArrayOfTablesHeader)
{
    const char *input = "[[servers]]\n"
                        "host = \"alpha\"\n"
                        "\n"
                        "[[servers]]\n"
                        "host = \"beta\"\n";

    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "[[servers]]"));
    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "host = \"alpha\""));
}

TEST_F(TomlTest, EmitArrayOfTablesSecondElement)
{
    const char *input = "[[servers]]\n"
                        "host = \"alpha\"\n"
                        "\n"
                        "[[servers]]\n"
                        "host = \"beta\"\n";

    EXPECT_TRUE(toml_parse_emit_contains(FIXTURE(TomlTest), input, "host = \"beta\""));
}

/* ── vigil.toml realistic test ────────────────────────────────────── */

TEST_F(TomlTest, VigilToml)
{
    const char *input = "name = \"myproject\"\n"
                        "version = \"0.1.0\"\n"
                        "\n"
                        "[deps]\n"
                        "json_schema = \"1.2.0\"\n";
    toml_parse_helper(FIXTURE(TomlTest), input, vigil_test_failed_);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "name")), "myproject");
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(FIXTURE(TomlTest)->root, "version")), "0.1.0");
    const vigil_toml_value_t *deps = vigil_toml_table_get(FIXTURE(TomlTest)->root, "deps");
    ASSERT_NE(deps, NULL);
    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(deps, "json_schema")), "1.2.0");
}

/* ── Constructors ────────────────────────────────────────────────── */

TEST_F(TomlTest, ManualConstruction)
{
    vigil_toml_value_t *tbl = NULL;
    vigil_toml_value_t *str = NULL;
    vigil_toml_value_t *num = NULL;

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_table_new(NULL, &tbl, &FIXTURE(TomlTest)->error));
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_string_new(NULL, "hello", 5, &str, &FIXTURE(TomlTest)->error));
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_integer_new(NULL, 42, &num, &FIXTURE(TomlTest)->error));
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_table_set(tbl, "greeting", 8, str, &FIXTURE(TomlTest)->error));
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_table_set(tbl, "count", 5, num, &FIXTURE(TomlTest)->error));

    EXPECT_STREQ(vigil_toml_string_value(vigil_toml_table_get(tbl, "greeting")), "hello");
    EXPECT_EQ(vigil_toml_integer_value(vigil_toml_table_get(tbl, "count")), 42);

    vigil_toml_free(&tbl);
}

/* ── Custom allocator ────────────────────────────────────────────── */

static size_t alloc_count = 0;
static size_t dealloc_count = 0;

static void *test_alloc(void *ctx, size_t size)
{
    (void)ctx;
    alloc_count++;
    return malloc(size);
}
static void *test_realloc(void *ctx, void *p, size_t size)
{
    (void)ctx;
    alloc_count++;
    return realloc(p, size);
}
static void test_dealloc(void *ctx, void *p)
{
    (void)ctx;
    dealloc_count++;
    free(p);
}

TEST_F(TomlTest, CustomAllocator)
{
    alloc_count = 0;
    dealloc_count = 0;
    vigil_allocator_t alloc = {NULL, test_alloc, test_realloc, test_dealloc};
    const char *input = "key = \"value\"";
    ASSERT_EQ(VIGIL_STATUS_OK,
              vigil_toml_parse(&alloc, input, strlen(input), &FIXTURE(TomlTest)->root, &FIXTURE(TomlTest)->error));
    EXPECT_GT(alloc_count, 0);
    vigil_toml_free(&FIXTURE(TomlTest)->root);
    EXPECT_GT(dealloc_count, 0);
}

/* ── Table entry iteration ───────────────────────────────────────── */

TEST_F(TomlTest, TableIteration)
{
    toml_parse_helper(FIXTURE(TomlTest), "a = 1\nb = 2\nc = 3", vigil_test_failed_);
    EXPECT_EQ(vigil_toml_table_count(FIXTURE(TomlTest)->root), 3);
    const char *key = NULL;
    size_t klen = 0;
    const vigil_toml_value_t *val = NULL;
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_toml_table_entry(FIXTURE(TomlTest)->root, 0, &key, &klen, &val));
    EXPECT_EQ(klen, 1);
    EXPECT_TRUE(strncmp(key, "a", klen) == 0);
    EXPECT_EQ(vigil_toml_integer_value(val), 1);
}

/* ── Dotted key path convenience ─────────────────────────────────── */

TEST_F(TomlTest, GetPathMissing)
{
    toml_parse_helper(FIXTURE(TomlTest), "a = 1", vigil_test_failed_);
    EXPECT_EQ(vigil_toml_table_get_path(FIXTURE(TomlTest)->root, "x.y.z"), NULL);
}

void register_toml_tests(void)
{
    REGISTER_TEST_F(TomlTest, StringValue);
    REGISTER_TEST_F(TomlTest, IntegerValue);
    REGISTER_TEST_F(TomlTest, NegativeInteger);
    REGISTER_TEST_F(TomlTest, HexOctBin);
    REGISTER_TEST_F(TomlTest, IntegerUnderscores);
    REGISTER_TEST_F(TomlTest, FloatValue);
    REGISTER_TEST_F(TomlTest, FloatExponent);
    REGISTER_TEST_F(TomlTest, FloatInfNan);
    REGISTER_TEST_F(TomlTest, BoolValues);
    REGISTER_TEST_F(TomlTest, BasicStringEscapes);
    REGISTER_TEST_F(TomlTest, BasicStringAdditionalEscapes);
    REGISTER_TEST_F(TomlTest, UnicodeEscape);
    REGISTER_TEST_F(TomlTest, LiteralString);
    REGISTER_TEST_F(TomlTest, MultilineBasicString);
    REGISTER_TEST_F(TomlTest, MultilineBasicStringEscapes);
    REGISTER_TEST_F(TomlTest, MultilineBasicStringExtraQuotes);
    REGISTER_TEST_F(TomlTest, MultilineLiteralString);
    REGISTER_TEST_F(TomlTest, MultilineLineContinuation);
    REGISTER_TEST_F(TomlTest, EmptyStrings);
    REGISTER_TEST_F(TomlTest, SimpleTable);
    REGISTER_TEST_F(TomlTest, NestedTable);
    REGISTER_TEST_F(TomlTest, DottedKeys);
    REGISTER_TEST_F(TomlTest, InlineTable);
    REGISTER_TEST_F(TomlTest, InlineTableDottedKeys);
    REGISTER_TEST_F(TomlTest, QuotedKey);
    REGISTER_TEST_F(TomlTest, SimpleArray);
    REGISTER_TEST_F(TomlTest, StringArray);
    REGISTER_TEST_F(TomlTest, MultilineArray);
    REGISTER_TEST_F(TomlTest, NestedArray);
    REGISTER_TEST_F(TomlTest, ArrayOfTables);
    REGISTER_TEST_F(TomlTest, OffsetDateTime);
    REGISTER_TEST_F(TomlTest, OffsetDateTimeLowercaseSeparatorAndOffset);
    REGISTER_TEST_F(TomlTest, OffsetDateTimeWithOffset);
    REGISTER_TEST_F(TomlTest, LocalDateTime);
    REGISTER_TEST_F(TomlTest, LocalDateTimeWithSpaceSeparator);
    REGISTER_TEST_F(TomlTest, LocalDate);
    REGISTER_TEST_F(TomlTest, LocalTime);
    REGISTER_TEST_F(TomlTest, FractionalSeconds);
    REGISTER_TEST_F(TomlTest, LocalTimeFractionalSecondsPadNanoseconds);
    REGISTER_TEST_F(TomlTest, LocalTimeFractionalSecondsTrimExtraPrecision);
    REGISTER_TEST_F(TomlTest, InvalidDateTimeComponents);
    REGISTER_TEST_F(TomlTest, Comments);
    REGISTER_TEST_F(TomlTest, DuplicateKeyError);
    REGISTER_TEST_F(TomlTest, UnterminatedString);
    REGISTER_TEST_F(TomlTest, InvalidBasicStringEscape);
    REGISTER_TEST_F(TomlTest, InlineTableKeyConflictError);
    REGISTER_TEST_F(TomlTest, TableHeaderMissingKey);
    REGISTER_TEST_F(TomlTest, TableHeaderMissingClosingBracket);
    REGISTER_TEST_F(TomlTest, TableHeaderConflictsWithScalar);
    REGISTER_TEST_F(TomlTest, ArrayTableParentConflict);
    REGISTER_TEST_F(TomlTest, ArrayTableConflictsWithTable);
    REGISTER_TEST_F(TomlTest, DottedKeysReuseExistingTable);
    REGISTER_TEST_F(TomlTest, DottedKeyConflictError);
    REGISTER_TEST_F(TomlTest, MissingEqualsError);
    REGISTER_TEST_F(TomlTest, InvalidKeyValueKey);
    REGISTER_TEST_F(TomlTest, EmitRoundTrip);
    REGISTER_TEST_F(TomlTest, EmitTable);
    REGISTER_TEST_F(TomlTest, EmitFormatsNumbersAndBooleans);
    REGISTER_TEST_F(TomlTest, EmitFormatsDateTimes);
    REGISTER_TEST_F(TomlTest, EmitFormatsArraysAndInlineTables);
    REGISTER_TEST_F(TomlTest, EmitQuotedSectionHeader);
    REGISTER_TEST_F(TomlTest, EmitArrayOfTablesHeader);
    REGISTER_TEST_F(TomlTest, EmitArrayOfTablesSecondElement);
    REGISTER_TEST_F(TomlTest, VigilToml);
    REGISTER_TEST_F(TomlTest, ManualConstruction);
    REGISTER_TEST_F(TomlTest, CustomAllocator);
    REGISTER_TEST_F(TomlTest, TableIteration);
    REGISTER_TEST_F(TomlTest, GetPathMissing);
}
