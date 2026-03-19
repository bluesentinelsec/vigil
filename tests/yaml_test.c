/* Unit tests for VIGIL YAML parsing library. */
#include "vigil/json.h"
#include "vigil/yaml.h"
#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

/* ── Helper ──────────────────────────────────────────────────────── */

static char *yaml_to_json(const char *yaml)
{
    vigil_json_value_t *json = NULL;
    char *out = NULL;
    size_t len = 0;

    if (vigil_yaml_parse(yaml, strlen(yaml), NULL, &json, NULL) != VIGIL_STATUS_OK)
        return NULL;
    if (vigil_json_emit(json, &out, &len, NULL) != VIGIL_STATUS_OK)
    {
        vigil_json_free(&json);
        return NULL;
    }
    vigil_json_free(&json);
    return out;
}

/* ── Scalar Tests ────────────────────────────────────────────────── */

TEST(VigilYamlTest, ParseString)
{
    char *json = yaml_to_json("name: test");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"name\":\"test\"}");
    free(json);
}

TEST(VigilYamlTest, ParseNumber)
{
    char *json = yaml_to_json("count: 42");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"count\":42}");
    free(json);
}

TEST(VigilYamlTest, ParseFloat)
{
    char *json = yaml_to_json("pi: 3.14");
    ASSERT_NE(json, NULL);
    /* Float may have precision artifacts, just check it starts correctly */
    EXPECT_TRUE(strncmp(json, "{\"pi\":3.14", 10) == 0);
    free(json);
}

TEST(VigilYamlTest, ParseBool)
{
    char *json = yaml_to_json("enabled: true\ndisabled: false");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"enabled\":true,\"disabled\":false}");
    free(json);
}

TEST(VigilYamlTest, ParseNull)
{
    char *json = yaml_to_json("empty: null\ntilde: ~");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"empty\":null,\"tilde\":null}");
    free(json);
}

/* ── Sequence Tests ──────────────────────────────────────────────── */

TEST(VigilYamlTest, ParseSequence)
{
    char *json = yaml_to_json("- apple\n- banana");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "[\"apple\",\"banana\"]");
    free(json);
}

TEST(VigilYamlTest, ParseNestedSequence)
{
    char *json = yaml_to_json("items:\n  - a\n  - b");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"items\":[\"a\",\"b\"]}");
    free(json);
}

/* ── Mapping Tests ───────────────────────────────────────────────── */

TEST(VigilYamlTest, ParseMapping)
{
    char *json = yaml_to_json("name: test\ncount: 42");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"name\":\"test\",\"count\":42}");
    free(json);
}

TEST(VigilYamlTest, ParseNestedMapping)
{
    char *json = yaml_to_json("server:\n  host: localhost\n  port: 8080");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"server\":{\"host\":\"localhost\",\"port\":8080}}");
    free(json);
}

/* ── Quoted String Tests ─────────────────────────────────────────── */

TEST(VigilYamlTest, ParseDoubleQuoted)
{
    char *json = yaml_to_json("msg: \"hello world\"");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"msg\":\"hello world\"}");
    free(json);
}

TEST(VigilYamlTest, ParseSingleQuoted)
{
    char *json = yaml_to_json("msg: 'hello world'");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"msg\":\"hello world\"}");
    free(json);
}

/* ── Comment Tests ───────────────────────────────────────────────── */

TEST(VigilYamlTest, ParseWithComments)
{
    char *json = yaml_to_json("# comment\nname: test  # inline");
    ASSERT_NE(json, NULL);
    EXPECT_STREQ(json, "{\"name\":\"test\"}");
    free(json);
}

/* ── Test Registration ───────────────────────────────────────────── */

void register_yaml_tests(void)
{
    REGISTER_TEST(VigilYamlTest, ParseString);
    REGISTER_TEST(VigilYamlTest, ParseNumber);
    REGISTER_TEST(VigilYamlTest, ParseFloat);
    REGISTER_TEST(VigilYamlTest, ParseBool);
    REGISTER_TEST(VigilYamlTest, ParseNull);
    REGISTER_TEST(VigilYamlTest, ParseSequence);
    REGISTER_TEST(VigilYamlTest, ParseNestedSequence);
    REGISTER_TEST(VigilYamlTest, ParseMapping);
    REGISTER_TEST(VigilYamlTest, ParseNestedMapping);
    REGISTER_TEST(VigilYamlTest, ParseDoubleQuoted);
    REGISTER_TEST(VigilYamlTest, ParseSingleQuoted);
    REGISTER_TEST(VigilYamlTest, ParseWithComments);
}
