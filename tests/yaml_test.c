/* Unit tests for BASL YAML parsing library. */
#include "basl_test.h"
#include "basl/yaml.h"
#include "basl/json.h"

#include <stdlib.h>
#include <string.h>

/* ── Helper ──────────────────────────────────────────────────────── */

static char *yaml_to_json(const char *yaml) {
    basl_json_value_t *json = NULL;
    char *out = NULL;
    size_t len = 0;
    
    if (basl_yaml_parse(yaml, strlen(yaml), NULL, &json, NULL) != BASL_STATUS_OK)
        return NULL;
    if (basl_json_emit(json, &out, &len, NULL) != BASL_STATUS_OK) {
        basl_json_free(&json);
        return NULL;
    }
    basl_json_free(&json);
    return out;
}

/* ── Scalar Tests ────────────────────────────────────────────────── */

TEST(BaslYamlTest, ParseString) {
    char *json = yaml_to_json("name: test");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"name\":\"test\"}");
    free(json);
}

TEST(BaslYamlTest, ParseNumber) {
    char *json = yaml_to_json("count: 42");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"count\":42}");
    free(json);
}

TEST(BaslYamlTest, ParseFloat) {
    char *json = yaml_to_json("pi: 3.14");
    ASSERT_NE(json, NULL);
    /* Float may have precision artifacts, just check it starts correctly */
    EXPECT_TRUE(strncmp(json, "{\"pi\":3.14", 10) == 0);
    free(json);
}

TEST(BaslYamlTest, ParseBool) {
    char *json = yaml_to_json("enabled: true\ndisabled: false");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"enabled\":true,\"disabled\":false}");
    free(json);
}

TEST(BaslYamlTest, ParseNull) {
    char *json = yaml_to_json("empty: null\ntilde: ~");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"empty\":null,\"tilde\":null}");
    free(json);
}

/* ── Sequence Tests ──────────────────────────────────────────────── */

TEST(BaslYamlTest, ParseSequence) {
    char *json = yaml_to_json("- apple\n- banana");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "[\"apple\",\"banana\"]");
    free(json);
}

TEST(BaslYamlTest, ParseNestedSequence) {
    char *json = yaml_to_json("items:\n  - a\n  - b");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"items\":[\"a\",\"b\"]}");
    free(json);
}

/* ── Mapping Tests ───────────────────────────────────────────────── */

TEST(BaslYamlTest, ParseMapping) {
    char *json = yaml_to_json("name: test\ncount: 42");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"name\":\"test\",\"count\":42}");
    free(json);
}

TEST(BaslYamlTest, ParseNestedMapping) {
    char *json = yaml_to_json("server:\n  host: localhost\n  port: 8080");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"server\":{\"host\":\"localhost\",\"port\":8080}}");
    free(json);
}

/* ── Quoted String Tests ─────────────────────────────────────────── */

TEST(BaslYamlTest, ParseDoubleQuoted) {
    char *json = yaml_to_json("msg: \"hello world\"");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"msg\":\"hello world\"}");
    free(json);
}

TEST(BaslYamlTest, ParseSingleQuoted) {
    char *json = yaml_to_json("msg: 'hello world'");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"msg\":\"hello world\"}");
    free(json);
}

/* ── Comment Tests ───────────────────────────────────────────────── */

TEST(BaslYamlTest, ParseWithComments) {
    char *json = yaml_to_json("# comment\nname: test  # inline");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"name\":\"test\"}");
    free(json);
}

/* ── Test Registration ───────────────────────────────────────────── */

void register_yaml_tests(void) {
    REGISTER_TEST(BaslYamlTest, ParseString);
    REGISTER_TEST(BaslYamlTest, ParseNumber);
    REGISTER_TEST(BaslYamlTest, ParseFloat);
    REGISTER_TEST(BaslYamlTest, ParseBool);
    REGISTER_TEST(BaslYamlTest, ParseNull);
    REGISTER_TEST(BaslYamlTest, ParseSequence);
    REGISTER_TEST(BaslYamlTest, ParseNestedSequence);
    REGISTER_TEST(BaslYamlTest, ParseMapping);
    REGISTER_TEST(BaslYamlTest, ParseNestedMapping);
    REGISTER_TEST(BaslYamlTest, ParseDoubleQuoted);
    REGISTER_TEST(BaslYamlTest, ParseSingleQuoted);
    REGISTER_TEST(BaslYamlTest, ParseWithComments);
}
