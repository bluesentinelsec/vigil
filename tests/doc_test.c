#include "basl_test.h"

#include "basl/doc.h"
#include "basl/lexer.h"
#include "basl/source.h"
#include "basl/runtime.h"

#include <string.h>

/* ── Fixture ─────────────────────────────────────────────────────── */

typedef struct BaslDocTest {
    basl_runtime_t *runtime;
    basl_source_registry_t registry;
    basl_token_list_t tokens;
    basl_diagnostic_list_t diagnostics;
    basl_error_t error;
} BaslDocTest;

static void BaslDocTest_SetUp(void *p) {
    BaslDocTest *self = (BaslDocTest *)p;
    memset(self, 0, sizeof(*self));
    basl_runtime_open(&self->runtime, NULL, &self->error);
    basl_source_registry_init(&self->registry, self->runtime);
    basl_token_list_init(&self->tokens, self->runtime);
    basl_diagnostic_list_init(&self->diagnostics, self->runtime);
}

static void BaslDocTest_TearDown(void *p) {
    BaslDocTest *self = (BaslDocTest *)p;
    basl_diagnostic_list_free(&self->diagnostics);
    basl_token_list_free(&self->tokens);
    basl_source_registry_free(&self->registry);
    basl_runtime_close(&self->runtime);
}

#define F FIXTURE(BaslDocTest)

static char *doc_render_helper(BaslDocTest *f, const char *src_text, const char *symbol) {
    basl_source_id_t source_id = 0;
    char *text = NULL;
    size_t length = 0;
    basl_doc_module_t module;

    if (basl_source_registry_register_cstr(
            &f->registry, "test.basl", src_text, &source_id, &f->error) != BASL_STATUS_OK) return NULL;
    if (basl_lex_source(&f->registry, source_id, &f->tokens, &f->diagnostics, &f->error) != BASL_STATUS_OK) return NULL;
    if (basl_doc_extract(NULL, "test.basl", 9, src_text, strlen(src_text),
                          &f->tokens, &module, &f->error) != BASL_STATUS_OK) return NULL;

    basl_doc_render(&module, symbol, &text, &length, &f->error);
    basl_doc_module_free(&module);
    return text; /* caller must free */
}

TEST_F(BaslDocTest, ModuleViewShowsAllPublicSymbols) {
    char *out = doc_render_helper(F, 
        "// Geometry helpers.\n"
        "\n"
        "// Mathematical answer.\n"
        "pub const i32 answer = 42;\n"
        "\n"
        "// Adds two numbers.\n"
        "pub fn add(i32 a, i32 b) -> i32 {\n"
        "\treturn a + b;\n"
        "}\n"
        "\n"
        "fn hidden_fn() -> i32 {\n"
        "\treturn 0;\n"
        "}\n", NULL);

    EXPECT_TRUE(strstr(out, "MODULE\n  test") != NULL);
    EXPECT_TRUE(strstr(out, "SUMMARY\n  Geometry helpers.") != NULL);
    EXPECT_TRUE(strstr(out, "CONSTANTS\n  answer i32\n    Mathematical answer.") != NULL);
    EXPECT_TRUE(strstr(out, "FUNCTIONS\n  add(i32 a, i32 b) -> i32\n    Adds two numbers.") != NULL);
    EXPECT_TRUE(strstr(out, "hidden_fn") == NULL);
    free(out);
}

TEST_F(BaslDocTest, ClassWithFieldsAndMethods) {
    char *out = doc_render_helper(F, 
        "// A point in world space.\n"
        "pub class Point implements Formatter {\n"
        "\t// X coordinate.\n"
        "\tpub i32 x;\n"
        "\n"
        "\ti32 hidden;\n"
        "\n"
        "\t// Formats the point name.\n"
        "\tpub fn format(string label) -> string {\n"
        "\t\treturn label;\n"
        "\t}\n"
        "\n"
        "\tfn secret() -> i32 {\n"
        "\t\treturn self.hidden;\n"
        "\t}\n"
        "}\n", NULL);

    EXPECT_TRUE(strstr(out, "Point implements Formatter") != NULL);
    EXPECT_TRUE(strstr(out, "A point in world space.") != NULL);
    EXPECT_TRUE(strstr(out, "Fields\n      x i32\n        X coordinate.") != NULL);
    EXPECT_TRUE(strstr(out, "Methods\n      format(string label) -> string\n        Formats the point name.") != NULL);
    EXPECT_TRUE(strstr(out, "hidden") == NULL);
    EXPECT_TRUE(strstr(out, "secret") == NULL);
    free(out);
}

TEST_F(BaslDocTest, InterfaceWithMethods) {
    char *out = doc_render_helper(F, 
        "// A named formatter.\n"
        "pub interface Formatter {\n"
        "\t// Formats a label.\n"
        "\tfn format(string label) -> string;\n"
        "}\n", NULL);

    EXPECT_TRUE(strstr(out, "INTERFACES\n  Formatter") != NULL);
    EXPECT_TRUE(strstr(out, "A named formatter.") != NULL);
    EXPECT_TRUE(strstr(out, "format(string label) -> string") != NULL);
    EXPECT_TRUE(strstr(out, "Formats a label.") != NULL);
    free(out);
}

TEST_F(BaslDocTest, EnumWithVariants) {
    char *out = doc_render_helper(F, 
        "// Primary colors.\n"
        "pub enum Color {\n"
        "\tRed,\n"
        "\tGreen,\n"
        "\tBlue\n"
        "}\n", NULL);

    EXPECT_TRUE(strstr(out, "ENUMS\n  Color") != NULL);
    EXPECT_TRUE(strstr(out, "Primary colors.") != NULL);
    EXPECT_TRUE(strstr(out, "Variants\n      Red\n      Green\n      Blue") != NULL);
    free(out);
}

TEST_F(BaslDocTest, VariableDeclaration) {
    char *out = doc_render_helper(F, 
        "// A mutable counter.\n"
        "pub i32 count = 0;\n", NULL);

    EXPECT_TRUE(strstr(out, "VARIABLES\n  count i32\n    A mutable counter.") != NULL);
    free(out);
}

TEST_F(BaslDocTest, SymbolLookupFunction) {
    char *out = doc_render_helper(F, 
        "// Adds two numbers.\n"
        "pub fn add(i32 a, i32 b) -> i32 {\n"
        "\treturn a + b;\n"
        "}\n",
        "add");

    EXPECT_TRUE(strstr(out, "add(i32 a, i32 b) -> i32") != NULL);
    EXPECT_TRUE(strstr(out, "Adds two numbers.") != NULL);
    free(out);
}

TEST_F(BaslDocTest, SymbolLookupClassMember) {
    char *out = doc_render_helper(F, 
        "pub class Point {\n"
        "\t// X coordinate.\n"
        "\tpub i32 x;\n"
        "\t// Measures size.\n"
        "\tpub fn size() -> i32 {\n"
        "\t\treturn self.x;\n"
        "\t}\n"
        "}\n",
        "Point.x");

    EXPECT_STREQ(out, "Point.x i32\n\nX coordinate.\n");
    free(out);
}

TEST_F(BaslDocTest, SymbolLookupClassMethod) {
    char *out = doc_render_helper(F, 
        "pub class Point {\n"
        "\t// Measures size.\n"
        "\tpub fn size() -> i32 {\n"
        "\t\treturn self.x;\n"
        "\t}\n"
        "}\n",
        "Point.size");

    EXPECT_STREQ(out, "Point.size() -> i32\n\nMeasures size.\n");
    free(out);
}

TEST_F(BaslDocTest, SymbolLookupInterfaceMethod) {
    char *out = doc_render_helper(F, 
        "pub interface Formatter {\n"
        "\t// Formats a label.\n"
        "\tfn format(string label) -> string;\n"
        "}\n",
        "Formatter.format");

    EXPECT_STREQ(out, "Formatter.format(string label) -> string\n\nFormats a label.\n");
    free(out);
}

TEST_F(BaslDocTest, MissingSymbolReturnsError) {
    basl_source_id_t source_id = 0;
    const char *src = "pub fn add(i32 a, i32 b) -> i32 {\n\treturn a + b;\n}\n";
    ASSERT_EQ(basl_source_registry_register_cstr(
        &F->registry, "test.basl", src, &source_id, &F->error), BASL_STATUS_OK);
    ASSERT_EQ(basl_lex_source(&F->registry, source_id, &F->tokens, &F->diagnostics, &F->error),
              BASL_STATUS_OK);

    basl_doc_module_t module;
    ASSERT_EQ(basl_doc_extract(NULL, "test.basl", 9, src, strlen(src),
                                &F->tokens, &module, &F->error), BASL_STATUS_OK);

    char *text = NULL;
    size_t length = 0;
    EXPECT_EQ(basl_doc_render(&module, "missing", &text, &length, &F->error),
              BASL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(text, NULL);
    basl_doc_module_free(&module);
}

TEST_F(BaslDocTest, ComplexReturnTypes) {
    char *out = doc_render_helper(F, 
        "pub fn get_pair() -> (i32, err) {\n"
        "\treturn (0, ok);\n"
        "}\n", NULL);

    EXPECT_TRUE(strstr(out, "get_pair() -> (i32, err)") != NULL);
    free(out);
}

TEST_F(BaslDocTest, GenericTypes) {
    char *out = doc_render_helper(F, 
        "pub fn process(array<string> items, map<string, i32> counts) -> void {\n"
        "}\n", NULL);

    EXPECT_TRUE(strstr(out, "process(array<string> items, map<string, i32> counts) -> void") != NULL);
    free(out);
}

TEST_F(BaslDocTest, PrivateDeclarationsHidden) {
    char *out = doc_render_helper(F, 
        "fn private_fn() -> i32 {\n"
        "\treturn 0;\n"
        "}\n"
        "class PrivateClass {\n"
        "\tpub i32 x;\n"
        "}\n"
        "interface PrivateIface {\n"
        "\tfn method() -> void;\n"
        "}\n"
        "enum PrivateEnum {\n"
        "\tA,\n"
        "\tB\n"
        "}\n"
        "const i32 PRIVATE_CONST = 42;\n"
        "i32 private_var = 0;\n", NULL);

    EXPECT_TRUE(strstr(out, "private") == NULL);
    EXPECT_TRUE(strstr(out, "Private") == NULL);
    EXPECT_TRUE(strstr(out, "PRIVATE") == NULL);
    free(out);
}

TEST_F(BaslDocTest, EmptyFileShowsModuleOnly) {
    char *out = doc_render_helper(F, "", NULL);
    EXPECT_TRUE(strstr(out, "MODULE\n  test") != NULL);
    EXPECT_TRUE(strstr(out, "SUMMARY") == NULL);
    EXPECT_TRUE(strstr(out, "FUNCTIONS") == NULL);
    free(out);
}

TEST_F(BaslDocTest, ModuleNameFromPath) {
    basl_source_id_t source_id = 0;
    const char *src = "pub fn hello() -> void {\n}\n";
    ASSERT_EQ(basl_source_registry_register_cstr(
        &F->registry, "lib/utils.basl", src, &source_id, &F->error), BASL_STATUS_OK);
    ASSERT_EQ(basl_lex_source(&F->registry, source_id, &F->tokens, &F->diagnostics, &F->error),
              BASL_STATUS_OK);

    basl_doc_module_t module;
    ASSERT_EQ(basl_doc_extract(NULL, "lib/utils.basl", 14, src, strlen(src),
                                &F->tokens, &module, &F->error), BASL_STATUS_OK);

    char *text = NULL;
    size_t length = 0;
    ASSERT_EQ(basl_doc_render(&module, NULL, &text, &length, &F->error), BASL_STATUS_OK);
    char *out = text;
    EXPECT_TRUE(strstr(out, "MODULE\n  utils") != NULL);
    free(out);
    basl_doc_module_free(&module);
}

#undef F

void register_doc_tests(void) {
    REGISTER_TEST_F(BaslDocTest, ModuleViewShowsAllPublicSymbols);
    REGISTER_TEST_F(BaslDocTest, ClassWithFieldsAndMethods);
    REGISTER_TEST_F(BaslDocTest, InterfaceWithMethods);
    REGISTER_TEST_F(BaslDocTest, EnumWithVariants);
    REGISTER_TEST_F(BaslDocTest, VariableDeclaration);
    REGISTER_TEST_F(BaslDocTest, SymbolLookupFunction);
    REGISTER_TEST_F(BaslDocTest, SymbolLookupClassMember);
    REGISTER_TEST_F(BaslDocTest, SymbolLookupClassMethod);
    REGISTER_TEST_F(BaslDocTest, SymbolLookupInterfaceMethod);
    REGISTER_TEST_F(BaslDocTest, MissingSymbolReturnsError);
    REGISTER_TEST_F(BaslDocTest, ComplexReturnTypes);
    REGISTER_TEST_F(BaslDocTest, GenericTypes);
    REGISTER_TEST_F(BaslDocTest, PrivateDeclarationsHidden);
    REGISTER_TEST_F(BaslDocTest, EmptyFileShowsModuleOnly);
    REGISTER_TEST_F(BaslDocTest, ModuleNameFromPath);
}
