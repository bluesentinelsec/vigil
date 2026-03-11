#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>

extern "C" {
#include "basl/basl.h"
}

namespace {

basl_source_id_t RegisterSource(
    basl_source_registry_t *registry,
    const char *path,
    const char *text,
    basl_error_t *error
) {
    basl_source_id_t source_id = 0U;

    EXPECT_EQ(
        basl_source_registry_register_cstr(registry, path, text, &source_id, error),
        BASL_STATUS_OK
    );
    return source_id;
}

const basl_token_t *TokenAt(const basl_token_list_t *tokens, size_t index) {
    const basl_token_t *token;

    token = basl_token_list_get(tokens, index);
    EXPECT_NE(token, nullptr);
    return token;
}

}  // namespace

TEST(BaslLexerTest, TokenizesSimpleFunction) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_token_list_t tokens;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_token_list_init(&tokens, runtime);
    source_id = RegisterSource(
        &registry,
        "simple.basl",
        "fn main() -> i32 { return 0; }",
        &error
    );

    ASSERT_EQ(
        basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 0U);

    EXPECT_EQ(TokenAt(&tokens, 0)->kind, BASL_TOKEN_FN);
    EXPECT_EQ(TokenAt(&tokens, 1)->kind, BASL_TOKEN_IDENTIFIER);
    EXPECT_EQ(TokenAt(&tokens, 2)->kind, BASL_TOKEN_LPAREN);
    EXPECT_EQ(TokenAt(&tokens, 3)->kind, BASL_TOKEN_RPAREN);
    EXPECT_EQ(TokenAt(&tokens, 4)->kind, BASL_TOKEN_ARROW);
    EXPECT_EQ(TokenAt(&tokens, 5)->kind, BASL_TOKEN_IDENTIFIER);
    EXPECT_EQ(TokenAt(&tokens, 6)->kind, BASL_TOKEN_LBRACE);
    EXPECT_EQ(TokenAt(&tokens, 7)->kind, BASL_TOKEN_RETURN);
    EXPECT_EQ(TokenAt(&tokens, 8)->kind, BASL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 9)->kind, BASL_TOKEN_SEMICOLON);
    EXPECT_EQ(TokenAt(&tokens, 10)->kind, BASL_TOKEN_RBRACE);
    EXPECT_EQ(TokenAt(&tokens, 11)->kind, BASL_TOKEN_EOF);

    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslLexerTest, SkipsCommentsAndWhitespace) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_token_list_t tokens;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_token_list_init(&tokens, runtime);
    source_id = RegisterSource(
        &registry,
        "comments.basl",
        "// leading\nimport /* inner */ \"fmt\";\n",
        &error
    );

    ASSERT_EQ(
        basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(TokenAt(&tokens, 0)->kind, BASL_TOKEN_IMPORT);
    EXPECT_EQ(TokenAt(&tokens, 1)->kind, BASL_TOKEN_STRING_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 2)->kind, BASL_TOKEN_SEMICOLON);
    EXPECT_EQ(TokenAt(&tokens, 3)->kind, BASL_TOKEN_EOF);

    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslLexerTest, TokenizesNumericAndStringLiteralForms) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_token_list_t tokens;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_token_list_init(&tokens, runtime);
    source_id = RegisterSource(
        &registry,
        "literals.basl",
        "0 0xFF 0b10 0o7 3.14 1e6 \"x\" `y` 'z' f\"hi {name}\"",
        &error
    );

    ASSERT_EQ(
        basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(TokenAt(&tokens, 0)->kind, BASL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 1)->kind, BASL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 2)->kind, BASL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 3)->kind, BASL_TOKEN_INT_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 4)->kind, BASL_TOKEN_FLOAT_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 5)->kind, BASL_TOKEN_FLOAT_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 6)->kind, BASL_TOKEN_STRING_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 7)->kind, BASL_TOKEN_RAW_STRING_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 8)->kind, BASL_TOKEN_CHAR_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 9)->kind, BASL_TOKEN_FSTRING_LITERAL);
    EXPECT_EQ(TokenAt(&tokens, 10)->kind, BASL_TOKEN_EOF);

    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslLexerTest, ReportsUnexpectedCharacter) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_token_list_t tokens;
    basl_source_id_t source_id;
    const basl_diagnostic_t *diagnostic;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_token_list_init(&tokens, runtime);
    source_id = RegisterSource(&registry, "bad.basl", "@", &error);

    EXPECT_EQ(
        basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    EXPECT_EQ(error.type, BASL_STATUS_SYNTAX_ERROR);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "unexpected character"), 0);
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    diagnostic = basl_diagnostic_list_get(&diagnostics, 0U);
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->severity, BASL_DIAGNOSTIC_ERROR);
    EXPECT_EQ(diagnostic->span.source_id, source_id);
    EXPECT_EQ(diagnostic->span.start_offset, 0U);
    EXPECT_EQ(diagnostic->span.end_offset, 1U);
    EXPECT_STREQ(basl_string_c_str(&diagnostic->message), "unexpected character");

    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslLexerTest, ReportsUnterminatedStringAndBlockComment) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_token_list_t tokens;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_token_list_init(&tokens, runtime);

    source_id = RegisterSource(&registry, "string.basl", "\"unterminated", &error);
    EXPECT_EQ(
        basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "unterminated string literal"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(&registry, "comment.basl", "/* never closes", &error);
    EXPECT_EQ(
        basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "unterminated block comment"
    );

    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslLexerTest, ReportsInvalidPrefixedNumericLiterals) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_token_list_t tokens;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_token_list_init(&tokens, runtime);

    source_id = RegisterSource(&registry, "badnum1.basl", "0x", &error);
    EXPECT_EQ(
        basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "expected digits after numeric base prefix"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(&registry, "badnum2.basl", "0b129", &error);
    EXPECT_EQ(
        basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "invalid digits for numeric base prefix"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(&registry, "badnum3.basl", "0o78", &error);
    EXPECT_EQ(
        basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "invalid digits for numeric base prefix"
    );

    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}
