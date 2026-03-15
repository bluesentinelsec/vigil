#include "basl_test.h"
#include <math.h>
#include <string.h>

#include "basl/toml.h"

/* ── Fixture ─────────────────────────────────────────────────────── */

typedef struct TomlTest {
    basl_toml_value_t *root;
    basl_error_t error;
} TomlTest;

void TomlTest_SetUp(void *p) {
    TomlTest *self = (TomlTest *)p;
    self->root = NULL;
    memset(&self->error, 0, sizeof(self->error));
}

void TomlTest_TearDown(void *p) {
    TomlTest *self = (TomlTest *)p;
    basl_toml_free(&self->root);
    basl_error_clear(&self->error);
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static void toml_parse_helper(TomlTest *self, const char *input,
                              int *basl_test_failed_) {
    ASSERT_EQ(BASL_STATUS_OK,
              basl_toml_parse(NULL, input, strlen(input),
                              &self->root, &self->error));
}

/* ── Basic key/value ─────────────────────────────────────────────── */

TEST_F(TomlTest, StringValue) {
    toml_parse_helper(FIXTURE(TomlTest), "name = \"basl\"", basl_test_failed_);
    const basl_toml_value_t *v = basl_toml_table_get(FIXTURE(TomlTest)->root, "name");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(basl_toml_type(v), BASL_TOML_STRING);
    EXPECT_STREQ(basl_toml_string_value(v), "basl");
}

TEST_F(TomlTest, IntegerValue) {
    toml_parse_helper(FIXTURE(TomlTest), "port = 8080", basl_test_failed_);
    const basl_toml_value_t *v = basl_toml_table_get(FIXTURE(TomlTest)->root, "port");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(basl_toml_type(v), BASL_TOML_INTEGER);
    EXPECT_EQ(basl_toml_integer_value(v), 8080);
}

TEST_F(TomlTest, NegativeInteger) {
    toml_parse_helper(FIXTURE(TomlTest), "offset = -42", basl_test_failed_);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "offset")), -42);
}

TEST_F(TomlTest, HexOctBin) {
    toml_parse_helper(FIXTURE(TomlTest), "hex = 0xDEAD\noct = 0o755\nbin = 0b11010110", basl_test_failed_);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "hex")), 0xDEAD);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "oct")), 0755);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "bin")), 0xD6);
}

TEST_F(TomlTest, IntegerUnderscores) {
    toml_parse_helper(FIXTURE(TomlTest), "big = 1_000_000", basl_test_failed_);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "big")), 1000000);
}

TEST_F(TomlTest, FloatValue) {
    toml_parse_helper(FIXTURE(TomlTest), "pi = 3.14159", basl_test_failed_);
    const basl_toml_value_t *v = basl_toml_table_get(FIXTURE(TomlTest)->root, "pi");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(basl_toml_type(v), BASL_TOML_FLOAT);
    EXPECT_DOUBLE_EQ(basl_toml_float_value(v), 3.14159);
}

TEST_F(TomlTest, FloatExponent) {
    toml_parse_helper(FIXTURE(TomlTest), "big = 5e+22", basl_test_failed_);
    EXPECT_DOUBLE_EQ(basl_toml_float_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "big")), 5e+22);
}

TEST_F(TomlTest, FloatInfNan) {
    toml_parse_helper(FIXTURE(TomlTest), "pos_inf = inf\nneg_inf = -inf\nnan_val = nan", basl_test_failed_);
    EXPECT_DOUBLE_EQ(basl_toml_float_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "pos_inf")), HUGE_VAL);
    EXPECT_DOUBLE_EQ(basl_toml_float_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "neg_inf")), -HUGE_VAL);
    EXPECT_TRUE(isnan(basl_toml_float_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "nan_val"))));
}

TEST_F(TomlTest, BoolValues) {
    toml_parse_helper(FIXTURE(TomlTest), "enabled = true\ndisabled = false", basl_test_failed_);
    EXPECT_EQ(basl_toml_bool_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "enabled")), 1);
    EXPECT_EQ(basl_toml_bool_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "disabled")), 0);
}

/* ── Strings ─────────────────────────────────────────────────────── */

TEST_F(TomlTest, BasicStringEscapes) {
    toml_parse_helper(FIXTURE(TomlTest), "s = \"hello\\tworld\\n\"", basl_test_failed_);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "s")), "hello\tworld\n");
}

TEST_F(TomlTest, UnicodeEscape) {
    toml_parse_helper(FIXTURE(TomlTest), "s = \"\\u0041\\U00000042\"", basl_test_failed_);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "s")), "AB");
}

TEST_F(TomlTest, LiteralString) {
    toml_parse_helper(FIXTURE(TomlTest), "s = 'C:\\Users\\path'", basl_test_failed_);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "s")), "C:\\Users\\path");
}

TEST_F(TomlTest, MultilineBasicString) {
    toml_parse_helper(FIXTURE(TomlTest), "s = \"\"\"\nhello\nworld\"\"\"", basl_test_failed_);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "s")), "hello\nworld");
}

TEST_F(TomlTest, MultilineLiteralString) {
    toml_parse_helper(FIXTURE(TomlTest), "s = '''\nhello\nworld'''", basl_test_failed_);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "s")), "hello\nworld");
}

TEST_F(TomlTest, MultilineLineContinuation) {
    toml_parse_helper(FIXTURE(TomlTest), "s = \"\"\"\nhello \\\n  world\"\"\"", basl_test_failed_);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "s")), "hello world");
}

TEST_F(TomlTest, EmptyStrings) {
    toml_parse_helper(FIXTURE(TomlTest), "a = \"\"\nb = ''", basl_test_failed_);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "a")), "");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "b")), "");
}

/* ── Tables ──────────────────────────────────────────────────────── */

TEST_F(TomlTest, SimpleTable) {
    toml_parse_helper(FIXTURE(TomlTest), "[server]\nhost = \"localhost\"\nport = 9090", basl_test_failed_);
    const basl_toml_value_t *server = basl_toml_table_get(FIXTURE(TomlTest)->root, "server");
    ASSERT_NE(server, NULL);
    EXPECT_EQ(basl_toml_type(server), BASL_TOML_TABLE);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(server, "host")), "localhost");
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(server, "port")), 9090);
}

TEST_F(TomlTest, NestedTable) {
    toml_parse_helper(FIXTURE(TomlTest), "[a.b.c]\nval = 1", basl_test_failed_);
    const basl_toml_value_t *c = basl_toml_table_get_path(FIXTURE(TomlTest)->root, "a.b.c");
    ASSERT_NE(c, NULL);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(c, "val")), 1);
}

TEST_F(TomlTest, DottedKeys) {
    toml_parse_helper(FIXTURE(TomlTest), "a.b.c = 42", basl_test_failed_);
    const basl_toml_value_t *v = basl_toml_table_get_path(FIXTURE(TomlTest)->root, "a.b.c");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(basl_toml_integer_value(v), 42);
}

TEST_F(TomlTest, InlineTable) {
    toml_parse_helper(FIXTURE(TomlTest), "point = { x = 1, y = 2 }", basl_test_failed_);
    const basl_toml_value_t *pt = basl_toml_table_get(FIXTURE(TomlTest)->root, "point");
    ASSERT_NE(pt, NULL);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(pt, "x")), 1);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(pt, "y")), 2);
}

TEST_F(TomlTest, QuotedKey) {
    toml_parse_helper(FIXTURE(TomlTest), "\"key with spaces\" = true", basl_test_failed_);
    EXPECT_EQ(basl_toml_bool_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "key with spaces")), 1);
}

/* ── Arrays ──────────────────────────────────────────────────────── */

TEST_F(TomlTest, SimpleArray) {
    toml_parse_helper(FIXTURE(TomlTest), "ports = [80, 443, 8080]", basl_test_failed_);
    const basl_toml_value_t *arr = basl_toml_table_get(FIXTURE(TomlTest)->root, "ports");
    ASSERT_NE(arr, NULL);
    EXPECT_EQ(basl_toml_type(arr), BASL_TOML_ARRAY);
    EXPECT_EQ(basl_toml_array_count(arr), 3);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_array_get(arr, 0)), 80);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_array_get(arr, 1)), 443);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_array_get(arr, 2)), 8080);
}

TEST_F(TomlTest, StringArray) {
    toml_parse_helper(FIXTURE(TomlTest), "names = [\"alice\", \"bob\"]", basl_test_failed_);
    const basl_toml_value_t *arr = basl_toml_table_get(FIXTURE(TomlTest)->root, "names");
    EXPECT_EQ(basl_toml_array_count(arr), 2);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_array_get(arr, 0)), "alice");
}

TEST_F(TomlTest, MultilineArray) {
    toml_parse_helper(FIXTURE(TomlTest), "a = [\n  1,\n  2,\n  3,\n]", basl_test_failed_);
    EXPECT_EQ(basl_toml_array_count(basl_toml_table_get(FIXTURE(TomlTest)->root, "a")), 3);
}

TEST_F(TomlTest, NestedArray) {
    toml_parse_helper(FIXTURE(TomlTest), "a = [[1, 2], [3, 4]]", basl_test_failed_);
    const basl_toml_value_t *a = basl_toml_table_get(FIXTURE(TomlTest)->root, "a");
    EXPECT_EQ(basl_toml_array_count(a), 2);
    EXPECT_EQ(basl_toml_array_count(basl_toml_array_get(a, 0)), 2);
}

/* ── Array of tables ─────────────────────────────────────────────── */

TEST_F(TomlTest, ArrayOfTables) {
    toml_parse_helper(FIXTURE(TomlTest), "[[products]]\nname = \"hammer\"\n\n[[products]]\nname = \"nail\"", basl_test_failed_);
    const basl_toml_value_t *arr = basl_toml_table_get(FIXTURE(TomlTest)->root, "products");
    ASSERT_NE(arr, NULL);
    EXPECT_EQ(basl_toml_type(arr), BASL_TOML_ARRAY);
    EXPECT_EQ(basl_toml_array_count(arr), 2);
    EXPECT_STREQ(basl_toml_string_value(
        basl_toml_table_get(basl_toml_array_get(arr, 0), "name")), "hammer");
    EXPECT_STREQ(basl_toml_string_value(
        basl_toml_table_get(basl_toml_array_get(arr, 1), "name")), "nail");
}

/* ── DateTime ────────────────────────────────────────────────────── */

TEST_F(TomlTest, OffsetDateTime) {
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15T10:30:00Z", basl_test_failed_);
    const basl_toml_value_t *v = basl_toml_table_get(FIXTURE(TomlTest)->root, "dt");
    ASSERT_NE(v, NULL);
    EXPECT_EQ(basl_toml_type(v), BASL_TOML_DATETIME);
    const basl_toml_datetime_t *dt = basl_toml_datetime_value(v);
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

TEST_F(TomlTest, OffsetDateTimeWithOffset) {
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15T10:30:00-05:00", basl_test_failed_);
    const basl_toml_datetime_t *dt = basl_toml_datetime_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "dt"));
    EXPECT_EQ(dt->offset_minutes, -300);
}

TEST_F(TomlTest, LocalDateTime) {
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15T10:30:00", basl_test_failed_);
    const basl_toml_datetime_t *dt = basl_toml_datetime_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "dt"));
    EXPECT_TRUE(dt->has_date);
    EXPECT_TRUE(dt->has_time);
    EXPECT_FALSE(dt->has_offset);
}

TEST_F(TomlTest, LocalDate) {
    toml_parse_helper(FIXTURE(TomlTest), "d = 2024-01-15", basl_test_failed_);
    const basl_toml_datetime_t *dt = basl_toml_datetime_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "d"));
    EXPECT_TRUE(dt->has_date);
    EXPECT_FALSE(dt->has_time);
    EXPECT_EQ(dt->year, 2024);
    EXPECT_EQ(dt->month, 1);
    EXPECT_EQ(dt->day, 15);
}

TEST_F(TomlTest, LocalTime) {
    toml_parse_helper(FIXTURE(TomlTest), "t = 10:30:00", basl_test_failed_);
    const basl_toml_datetime_t *dt = basl_toml_datetime_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "t"));
    EXPECT_FALSE(dt->has_date);
    EXPECT_TRUE(dt->has_time);
    EXPECT_EQ(dt->hour, 10);
    EXPECT_EQ(dt->minute, 30);
}

TEST_F(TomlTest, FractionalSeconds) {
    toml_parse_helper(FIXTURE(TomlTest), "dt = 2024-01-15T10:30:00.123456789Z", basl_test_failed_);
    const basl_toml_datetime_t *dt = basl_toml_datetime_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "dt"));
    EXPECT_EQ(dt->nanosecond, 123456789);
}

/* ── Comments ────────────────────────────────────────────────────── */

TEST_F(TomlTest, Comments) {
    toml_parse_helper(FIXTURE(TomlTest), "# full line comment\nkey = \"value\" # inline comment\n", basl_test_failed_);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "key")), "value");
}

/* ── Error cases ─────────────────────────────────────────────────── */

TEST_F(TomlTest, DuplicateKeyError) {
    basl_status_t s = basl_toml_parse(NULL, "a = 1\na = 2", 11,
                                      &FIXTURE(TomlTest)->root,
                                      &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, BASL_STATUS_OK);
}

TEST_F(TomlTest, UnterminatedString) {
    basl_status_t s = basl_toml_parse(NULL, "a = \"hello", 10,
                                      &FIXTURE(TomlTest)->root,
                                      &FIXTURE(TomlTest)->error);
    EXPECT_NE(s, BASL_STATUS_OK);
}

/* ── Emitter ─────────────────────────────────────────────────────── */

TEST_F(TomlTest, EmitRoundTrip) {
    toml_parse_helper(FIXTURE(TomlTest), "name = \"basl\"\nversion = \"0.1.0\"", basl_test_failed_);
    char *out = NULL;
    size_t len = 0;
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_emit(FIXTURE(TomlTest)->root, &out, &len, &FIXTURE(TomlTest)->error));
    ASSERT_NE(out, NULL);
    EXPECT_NE(strstr(out, "name = \"basl\""), NULL);
    EXPECT_NE(strstr(out, "version = \"0.1.0\""), NULL);
    free(out);
}

TEST_F(TomlTest, EmitTable) {
    toml_parse_helper(FIXTURE(TomlTest), "[deps]\njson = \"1.0\"\nhttp = \"2.0\"\nlog = \"0.5\"\ntest = \"1.1\"", basl_test_failed_);
    char *out = NULL;
    size_t len = 0;
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_emit(FIXTURE(TomlTest)->root, &out, &len, &FIXTURE(TomlTest)->error));
    ASSERT_NE(out, NULL);
    EXPECT_NE(strstr(out, "[deps]"), NULL);
    EXPECT_NE(strstr(out, "json = \"1.0\""), NULL);
    free(out);
}

/* ── basl.toml realistic test ────────────────────────────────────── */

TEST_F(TomlTest, BaslToml) {
    const char *input =
        "name = \"myproject\"\n"
        "version = \"0.1.0\"\n"
        "\n"
        "[deps]\n"
        "json_schema = \"1.2.0\"\n";
    toml_parse_helper(FIXTURE(TomlTest), input, basl_test_failed_);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "name")), "myproject");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(FIXTURE(TomlTest)->root, "version")), "0.1.0");
    const basl_toml_value_t *deps = basl_toml_table_get(FIXTURE(TomlTest)->root, "deps");
    ASSERT_NE(deps, NULL);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(deps, "json_schema")), "1.2.0");
}

/* ── Constructors ────────────────────────────────────────────────── */

TEST_F(TomlTest, ManualConstruction) {
    basl_toml_value_t *tbl = NULL;
    basl_toml_value_t *str = NULL;
    basl_toml_value_t *num = NULL;

    ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_new(NULL, &tbl, &FIXTURE(TomlTest)->error));
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_string_new(NULL, "hello", 5, &str, &FIXTURE(TomlTest)->error));
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_integer_new(NULL, 42, &num, &FIXTURE(TomlTest)->error));
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_set(tbl, "greeting", 8, str, &FIXTURE(TomlTest)->error));
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_set(tbl, "count", 5, num, &FIXTURE(TomlTest)->error));

    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(tbl, "greeting")), "hello");
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(tbl, "count")), 42);

    basl_toml_free(&tbl);
}

/* ── Custom allocator ────────────────────────────────────────────── */

static size_t alloc_count = 0;
static size_t dealloc_count = 0;

static void *test_alloc(void *ctx, size_t size) { (void)ctx; alloc_count++; return malloc(size); }
static void *test_realloc(void *ctx, void *p, size_t size) { (void)ctx; alloc_count++; return realloc(p, size); }
static void test_dealloc(void *ctx, void *p) { (void)ctx; dealloc_count++; free(p); }

TEST_F(TomlTest, CustomAllocator) {
    alloc_count = 0;
    dealloc_count = 0;
    basl_allocator_t alloc = {NULL, test_alloc, test_realloc, test_dealloc};
    const char *input = "key = \"value\"";
    ASSERT_EQ(BASL_STATUS_OK,
              basl_toml_parse(&alloc, input, strlen(input),
                              &FIXTURE(TomlTest)->root,
                              &FIXTURE(TomlTest)->error));
    EXPECT_GT(alloc_count, 0);
    basl_toml_free(&FIXTURE(TomlTest)->root);
    EXPECT_GT(dealloc_count, 0);
}

/* ── Table entry iteration ───────────────────────────────────────── */

TEST_F(TomlTest, TableIteration) {
    toml_parse_helper(FIXTURE(TomlTest), "a = 1\nb = 2\nc = 3", basl_test_failed_);
    EXPECT_EQ(basl_toml_table_count(FIXTURE(TomlTest)->root), 3);
    const char *key = NULL;
    size_t klen = 0;
    const basl_toml_value_t *val = NULL;
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_entry(FIXTURE(TomlTest)->root, 0, &key, &klen, &val));
    EXPECT_EQ(klen, 1);
    EXPECT_TRUE(strncmp(key, "a", klen) == 0);
    EXPECT_EQ(basl_toml_integer_value(val), 1);
}

/* ── Dotted key path convenience ─────────────────────────────────── */

TEST_F(TomlTest, GetPathMissing) {
    toml_parse_helper(FIXTURE(TomlTest), "a = 1", basl_test_failed_);
    EXPECT_EQ(basl_toml_table_get_path(FIXTURE(TomlTest)->root, "x.y.z"), NULL);
}

void register_toml_tests(void) {
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
    REGISTER_TEST_F(TomlTest, UnicodeEscape);
    REGISTER_TEST_F(TomlTest, LiteralString);
    REGISTER_TEST_F(TomlTest, MultilineBasicString);
    REGISTER_TEST_F(TomlTest, MultilineLiteralString);
    REGISTER_TEST_F(TomlTest, MultilineLineContinuation);
    REGISTER_TEST_F(TomlTest, EmptyStrings);
    REGISTER_TEST_F(TomlTest, SimpleTable);
    REGISTER_TEST_F(TomlTest, NestedTable);
    REGISTER_TEST_F(TomlTest, DottedKeys);
    REGISTER_TEST_F(TomlTest, InlineTable);
    REGISTER_TEST_F(TomlTest, QuotedKey);
    REGISTER_TEST_F(TomlTest, SimpleArray);
    REGISTER_TEST_F(TomlTest, StringArray);
    REGISTER_TEST_F(TomlTest, MultilineArray);
    REGISTER_TEST_F(TomlTest, NestedArray);
    REGISTER_TEST_F(TomlTest, ArrayOfTables);
    REGISTER_TEST_F(TomlTest, OffsetDateTime);
    REGISTER_TEST_F(TomlTest, OffsetDateTimeWithOffset);
    REGISTER_TEST_F(TomlTest, LocalDateTime);
    REGISTER_TEST_F(TomlTest, LocalDate);
    REGISTER_TEST_F(TomlTest, LocalTime);
    REGISTER_TEST_F(TomlTest, FractionalSeconds);
    REGISTER_TEST_F(TomlTest, Comments);
    REGISTER_TEST_F(TomlTest, DuplicateKeyError);
    REGISTER_TEST_F(TomlTest, UnterminatedString);
    REGISTER_TEST_F(TomlTest, EmitRoundTrip);
    REGISTER_TEST_F(TomlTest, EmitTable);
    REGISTER_TEST_F(TomlTest, BaslToml);
    REGISTER_TEST_F(TomlTest, ManualConstruction);
    REGISTER_TEST_F(TomlTest, CustomAllocator);
    REGISTER_TEST_F(TomlTest, TableIteration);
    REGISTER_TEST_F(TomlTest, GetPathMissing);
}
