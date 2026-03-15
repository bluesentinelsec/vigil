#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

extern "C" {
#include "basl/toml.h"
}

/* ── Helpers ─────────────────────────────────────────────────────── */

class TomlTest : public ::testing::Test {
protected:
    basl_toml_value_t *root = nullptr;
    basl_error_t error{};

    void TearDown() override {
        basl_toml_free(&root);
        basl_error_clear(&error);
    }

    void parse(const char *input) {
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_toml_parse(nullptr, input, strlen(input), &root, &error))
            << basl_error_message(&error);
    }
};

/* ── Basic key/value ─────────────────────────────────────────────── */

TEST_F(TomlTest, StringValue) {
    parse("name = \"basl\"");
    auto *v = basl_toml_table_get(root, "name");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(basl_toml_type(v), BASL_TOML_STRING);
    EXPECT_STREQ(basl_toml_string_value(v), "basl");
}

TEST_F(TomlTest, IntegerValue) {
    parse("port = 8080");
    auto *v = basl_toml_table_get(root, "port");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(basl_toml_type(v), BASL_TOML_INTEGER);
    EXPECT_EQ(basl_toml_integer_value(v), 8080);
}

TEST_F(TomlTest, NegativeInteger) {
    parse("offset = -42");
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(root, "offset")), -42);
}

TEST_F(TomlTest, HexOctBin) {
    parse("hex = 0xDEAD\noct = 0o755\nbin = 0b11010110");
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(root, "hex")), 0xDEAD);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(root, "oct")), 0755);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(root, "bin")), 0xD6);
}

TEST_F(TomlTest, IntegerUnderscores) {
    parse("big = 1_000_000");
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(root, "big")), 1000000);
}

TEST_F(TomlTest, FloatValue) {
    parse("pi = 3.14159");
    auto *v = basl_toml_table_get(root, "pi");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(basl_toml_type(v), BASL_TOML_FLOAT);
    EXPECT_DOUBLE_EQ(basl_toml_float_value(v), 3.14159);
}

TEST_F(TomlTest, FloatExponent) {
    parse("big = 5e+22");
    EXPECT_DOUBLE_EQ(basl_toml_float_value(basl_toml_table_get(root, "big")), 5e+22);
}

TEST_F(TomlTest, FloatInfNan) {
    parse("pos_inf = inf\nneg_inf = -inf\nnan_val = nan");
    EXPECT_EQ(basl_toml_float_value(basl_toml_table_get(root, "pos_inf")), HUGE_VAL);
    EXPECT_EQ(basl_toml_float_value(basl_toml_table_get(root, "neg_inf")), -HUGE_VAL);
    EXPECT_TRUE(std::isnan(basl_toml_float_value(basl_toml_table_get(root, "nan_val"))));
}

TEST_F(TomlTest, BoolValues) {
    parse("enabled = true\ndisabled = false");
    EXPECT_EQ(basl_toml_bool_value(basl_toml_table_get(root, "enabled")), 1);
    EXPECT_EQ(basl_toml_bool_value(basl_toml_table_get(root, "disabled")), 0);
}

/* ── Strings ─────────────────────────────────────────────────────── */

TEST_F(TomlTest, BasicStringEscapes) {
    parse("s = \"hello\\tworld\\n\"");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "s")), "hello\tworld\n");
}

TEST_F(TomlTest, UnicodeEscape) {
    parse("s = \"\\u0041\\U00000042\"");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "s")), "AB");
}

TEST_F(TomlTest, LiteralString) {
    parse("s = 'C:\\Users\\path'");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "s")), "C:\\Users\\path");
}

TEST_F(TomlTest, MultilineBasicString) {
    parse("s = \"\"\"\nhello\nworld\"\"\"");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "s")), "hello\nworld");
}

TEST_F(TomlTest, MultilineLiteralString) {
    parse("s = '''\nhello\nworld'''");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "s")), "hello\nworld");
}

TEST_F(TomlTest, MultilineLineContinuation) {
    parse("s = \"\"\"\nhello \\\n  world\"\"\"");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "s")), "hello world");
}

TEST_F(TomlTest, EmptyStrings) {
    parse("a = \"\"\nb = ''");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "a")), "");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "b")), "");
}

/* ── Tables ──────────────────────────────────────────────────────── */

TEST_F(TomlTest, SimpleTable) {
    parse("[server]\nhost = \"localhost\"\nport = 9090");
    auto *server = basl_toml_table_get(root, "server");
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(basl_toml_type(server), BASL_TOML_TABLE);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(server, "host")), "localhost");
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(server, "port")), 9090);
}

TEST_F(TomlTest, NestedTable) {
    parse("[a.b.c]\nval = 1");
    auto *c = basl_toml_table_get_path(root, "a.b.c");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(c, "val")), 1);
}

TEST_F(TomlTest, DottedKeys) {
    parse("a.b.c = 42");
    auto *v = basl_toml_table_get_path(root, "a.b.c");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(basl_toml_integer_value(v), 42);
}

TEST_F(TomlTest, InlineTable) {
    parse("point = { x = 1, y = 2 }");
    auto *pt = basl_toml_table_get(root, "point");
    ASSERT_NE(pt, nullptr);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(pt, "x")), 1);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(pt, "y")), 2);
}

TEST_F(TomlTest, QuotedKey) {
    parse("\"key with spaces\" = true");
    EXPECT_EQ(basl_toml_bool_value(basl_toml_table_get(root, "key with spaces")), 1);
}

/* ── Arrays ──────────────────────────────────────────────────────── */

TEST_F(TomlTest, SimpleArray) {
    parse("ports = [80, 443, 8080]");
    auto *arr = basl_toml_table_get(root, "ports");
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(basl_toml_type(arr), BASL_TOML_ARRAY);
    EXPECT_EQ(basl_toml_array_count(arr), 3u);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_array_get(arr, 0)), 80);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_array_get(arr, 1)), 443);
    EXPECT_EQ(basl_toml_integer_value(basl_toml_array_get(arr, 2)), 8080);
}

TEST_F(TomlTest, StringArray) {
    parse("names = [\"alice\", \"bob\"]");
    auto *arr = basl_toml_table_get(root, "names");
    EXPECT_EQ(basl_toml_array_count(arr), 2u);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_array_get(arr, 0)), "alice");
}

TEST_F(TomlTest, MultilineArray) {
    parse("a = [\n  1,\n  2,\n  3,\n]");
    EXPECT_EQ(basl_toml_array_count(basl_toml_table_get(root, "a")), 3u);
}

TEST_F(TomlTest, NestedArray) {
    parse("a = [[1, 2], [3, 4]]");
    auto *a = basl_toml_table_get(root, "a");
    EXPECT_EQ(basl_toml_array_count(a), 2u);
    EXPECT_EQ(basl_toml_array_count(basl_toml_array_get(a, 0)), 2u);
}

/* ── Array of tables ─────────────────────────────────────────────── */

TEST_F(TomlTest, ArrayOfTables) {
    parse("[[products]]\nname = \"hammer\"\n\n[[products]]\nname = \"nail\"");
    auto *arr = basl_toml_table_get(root, "products");
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(basl_toml_type(arr), BASL_TOML_ARRAY);
    EXPECT_EQ(basl_toml_array_count(arr), 2u);
    EXPECT_STREQ(basl_toml_string_value(
        basl_toml_table_get(basl_toml_array_get(arr, 0), "name")), "hammer");
    EXPECT_STREQ(basl_toml_string_value(
        basl_toml_table_get(basl_toml_array_get(arr, 1), "name")), "nail");
}

/* ── DateTime ────────────────────────────────────────────────────── */

TEST_F(TomlTest, OffsetDateTime) {
    parse("dt = 2024-01-15T10:30:00Z");
    auto *v = basl_toml_table_get(root, "dt");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(basl_toml_type(v), BASL_TOML_DATETIME);
    auto *dt = basl_toml_datetime_value(v);
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
    parse("dt = 2024-01-15T10:30:00-05:00");
    auto *dt = basl_toml_datetime_value(basl_toml_table_get(root, "dt"));
    EXPECT_EQ(dt->offset_minutes, -300);
}

TEST_F(TomlTest, LocalDateTime) {
    parse("dt = 2024-01-15T10:30:00");
    auto *dt = basl_toml_datetime_value(basl_toml_table_get(root, "dt"));
    EXPECT_TRUE(dt->has_date);
    EXPECT_TRUE(dt->has_time);
    EXPECT_FALSE(dt->has_offset);
}

TEST_F(TomlTest, LocalDate) {
    parse("d = 2024-01-15");
    auto *dt = basl_toml_datetime_value(basl_toml_table_get(root, "d"));
    EXPECT_TRUE(dt->has_date);
    EXPECT_FALSE(dt->has_time);
    EXPECT_EQ(dt->year, 2024);
    EXPECT_EQ(dt->month, 1);
    EXPECT_EQ(dt->day, 15);
}

TEST_F(TomlTest, LocalTime) {
    parse("t = 10:30:00");
    auto *dt = basl_toml_datetime_value(basl_toml_table_get(root, "t"));
    EXPECT_FALSE(dt->has_date);
    EXPECT_TRUE(dt->has_time);
    EXPECT_EQ(dt->hour, 10);
    EXPECT_EQ(dt->minute, 30);
}

TEST_F(TomlTest, FractionalSeconds) {
    parse("dt = 2024-01-15T10:30:00.123456789Z");
    auto *dt = basl_toml_datetime_value(basl_toml_table_get(root, "dt"));
    EXPECT_EQ(dt->nanosecond, 123456789);
}

/* ── Comments ────────────────────────────────────────────────────── */

TEST_F(TomlTest, Comments) {
    parse("# full line comment\nkey = \"value\" # inline comment\n");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "key")), "value");
}

/* ── Error cases ─────────────────────────────────────────────────── */

TEST_F(TomlTest, DuplicateKeyError) {
    auto s = basl_toml_parse(nullptr, "a = 1\na = 2", 11, &root, &error);
    EXPECT_NE(s, BASL_STATUS_OK);
}

TEST_F(TomlTest, UnterminatedString) {
    auto s = basl_toml_parse(nullptr, "a = \"hello", 10, &root, &error);
    EXPECT_NE(s, BASL_STATUS_OK);
}

/* ── Emitter ─────────────────────────────────────────────────────── */

TEST_F(TomlTest, EmitRoundTrip) {
    parse("name = \"basl\"\nversion = \"0.1.0\"");
    char *out = nullptr;
    size_t len = 0;
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_emit(root, &out, &len, &error));
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "name = \"basl\""), nullptr);
    EXPECT_NE(strstr(out, "version = \"0.1.0\""), nullptr);
    free(out);
}

TEST_F(TomlTest, EmitTable) {
    parse("[deps]\njson = \"1.0\"\nhttp = \"2.0\"\nlog = \"0.5\"\ntest = \"1.1\"");
    char *out = nullptr;
    size_t len = 0;
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_emit(root, &out, &len, &error));
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "[deps]"), nullptr);
    EXPECT_NE(strstr(out, "json = \"1.0\""), nullptr);
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
    parse(input);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "name")), "myproject");
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(root, "version")), "0.1.0");
    auto *deps = basl_toml_table_get(root, "deps");
    ASSERT_NE(deps, nullptr);
    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(deps, "json_schema")), "1.2.0");
}

/* ── Constructors ────────────────────────────────────────────────── */

TEST_F(TomlTest, ManualConstruction) {
    basl_toml_value_t *tbl = nullptr;
    basl_toml_value_t *str = nullptr;
    basl_toml_value_t *num = nullptr;

    ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_new(nullptr, &tbl, &error));
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_string_new(nullptr, "hello", 5, &str, &error));
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_integer_new(nullptr, 42, &num, &error));
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_set(tbl, "greeting", 8, str, &error));
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_set(tbl, "count", 5, num, &error));

    EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(tbl, "greeting")), "hello");
    EXPECT_EQ(basl_toml_integer_value(basl_toml_table_get(tbl, "count")), 42);

    basl_toml_free(&tbl);
}

/* ── Custom allocator ────────────────────────────────────────────── */

static size_t alloc_count = 0;
static size_t dealloc_count = 0;

static void *test_alloc(void *, size_t size) { alloc_count++; return malloc(size); }
static void *test_realloc(void *, void *p, size_t size) { alloc_count++; return realloc(p, size); }
static void test_dealloc(void *, void *p) { dealloc_count++; free(p); }

TEST_F(TomlTest, CustomAllocator) {
    alloc_count = 0;
    dealloc_count = 0;
    basl_allocator_t alloc = {nullptr, test_alloc, test_realloc, test_dealloc};
    const char *input = "key = \"value\"";
    ASSERT_EQ(BASL_STATUS_OK,
              basl_toml_parse(&alloc, input, strlen(input), &root, &error));
    EXPECT_GT(alloc_count, 0u);
    basl_toml_free(&root);
    EXPECT_GT(dealloc_count, 0u);
}

/* ── Table entry iteration ───────────────────────────────────────── */

TEST_F(TomlTest, TableIteration) {
    parse("a = 1\nb = 2\nc = 3");
    EXPECT_EQ(basl_toml_table_count(root), 3u);
    const char *key = nullptr;
    size_t klen = 0;
    const basl_toml_value_t *val = nullptr;
    ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_entry(root, 0, &key, &klen, &val));
    EXPECT_EQ(std::string(key, klen), "a");
    EXPECT_EQ(basl_toml_integer_value(val), 1);
}

/* ── Dotted key path convenience ─────────────────────────────────── */

TEST_F(TomlTest, GetPathMissing) {
    parse("a = 1");
    EXPECT_EQ(basl_toml_table_get_path(root, "x.y.z"), nullptr);
}
