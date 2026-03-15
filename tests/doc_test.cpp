#include <gtest/gtest.h>

extern "C" {
#include "basl/doc.h"
#include "basl/lexer.h"
#include "basl/source.h"
#include "basl/runtime.h"
}

#include <cstring>
#include <string>

class BaslDocTest : public ::testing::Test {
protected:
    basl_runtime_t *runtime = nullptr;
    basl_source_registry_t registry;
    basl_token_list_t tokens;
    basl_diagnostic_list_t diagnostics;
    basl_error_t error = {};

    void SetUp() override {
        ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
        basl_source_registry_init(&registry, runtime);
        basl_token_list_init(&tokens, runtime);
        basl_diagnostic_list_init(&diagnostics, runtime);
    }

    void TearDown() override {
        basl_diagnostic_list_free(&diagnostics);
        basl_token_list_free(&tokens);
        basl_source_registry_free(&registry);
        basl_runtime_close(&runtime);
    }

    std::string extract_and_render(const char *src, const char *symbol = nullptr) {
        basl_source_id_t source_id = 0;
        EXPECT_EQ(basl_source_registry_register_cstr(
            &registry, "test.basl", src, &source_id, &error), BASL_STATUS_OK);
        EXPECT_EQ(basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
                  BASL_STATUS_OK);

        basl_doc_module_t module;
        EXPECT_EQ(basl_doc_extract(nullptr, "test.basl", 9, src, strlen(src),
                                    &tokens, &module, &error), BASL_STATUS_OK);

        char *text = nullptr;
        size_t length = 0;
        basl_status_t status = basl_doc_render(&module, symbol, &text, &length, &error);
        std::string result;
        if (status == BASL_STATUS_OK && text != nullptr) {
            result.assign(text, length);
            free(text);
        }
        basl_doc_module_free(&module);
        return result;
    }
};

TEST_F(BaslDocTest, ModuleViewShowsAllPublicSymbols) {
    std::string out = extract_and_render(
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
        "}\n"
    );

    EXPECT_NE(out.find("MODULE\n  test"), std::string::npos);
    EXPECT_NE(out.find("SUMMARY\n  Geometry helpers."), std::string::npos);
    EXPECT_NE(out.find("CONSTANTS\n  answer i32\n    Mathematical answer."), std::string::npos);
    EXPECT_NE(out.find("FUNCTIONS\n  add(i32 a, i32 b) -> i32\n    Adds two numbers."), std::string::npos);
    EXPECT_EQ(out.find("hidden_fn"), std::string::npos);
}

TEST_F(BaslDocTest, ClassWithFieldsAndMethods) {
    std::string out = extract_and_render(
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
        "}\n"
    );

    EXPECT_NE(out.find("Point implements Formatter"), std::string::npos);
    EXPECT_NE(out.find("A point in world space."), std::string::npos);
    EXPECT_NE(out.find("Fields\n      x i32\n        X coordinate."), std::string::npos);
    EXPECT_NE(out.find("Methods\n      format(string label) -> string\n        Formats the point name."), std::string::npos);
    EXPECT_EQ(out.find("hidden"), std::string::npos);
    EXPECT_EQ(out.find("secret"), std::string::npos);
}

TEST_F(BaslDocTest, InterfaceWithMethods) {
    std::string out = extract_and_render(
        "// A named formatter.\n"
        "pub interface Formatter {\n"
        "\t// Formats a label.\n"
        "\tfn format(string label) -> string;\n"
        "}\n"
    );

    EXPECT_NE(out.find("INTERFACES\n  Formatter"), std::string::npos);
    EXPECT_NE(out.find("A named formatter."), std::string::npos);
    EXPECT_NE(out.find("format(string label) -> string"), std::string::npos);
    EXPECT_NE(out.find("Formats a label."), std::string::npos);
}

TEST_F(BaslDocTest, EnumWithVariants) {
    std::string out = extract_and_render(
        "// Primary colors.\n"
        "pub enum Color {\n"
        "\tRed,\n"
        "\tGreen,\n"
        "\tBlue\n"
        "}\n"
    );

    EXPECT_NE(out.find("ENUMS\n  Color"), std::string::npos);
    EXPECT_NE(out.find("Primary colors."), std::string::npos);
    EXPECT_NE(out.find("Variants\n      Red\n      Green\n      Blue"), std::string::npos);
}

TEST_F(BaslDocTest, VariableDeclaration) {
    std::string out = extract_and_render(
        "// A mutable counter.\n"
        "pub i32 count = 0;\n"
    );

    EXPECT_NE(out.find("VARIABLES\n  count i32\n    A mutable counter."), std::string::npos);
}

TEST_F(BaslDocTest, SymbolLookupFunction) {
    std::string out = extract_and_render(
        "// Adds two numbers.\n"
        "pub fn add(i32 a, i32 b) -> i32 {\n"
        "\treturn a + b;\n"
        "}\n",
        "add"
    );

    EXPECT_NE(out.find("add(i32 a, i32 b) -> i32"), std::string::npos);
    EXPECT_NE(out.find("Adds two numbers."), std::string::npos);
}

TEST_F(BaslDocTest, SymbolLookupClassMember) {
    std::string out = extract_and_render(
        "pub class Point {\n"
        "\t// X coordinate.\n"
        "\tpub i32 x;\n"
        "\t// Measures size.\n"
        "\tpub fn size() -> i32 {\n"
        "\t\treturn self.x;\n"
        "\t}\n"
        "}\n",
        "Point.x"
    );

    EXPECT_EQ(out, "Point.x i32\n\nX coordinate.\n");
}

TEST_F(BaslDocTest, SymbolLookupClassMethod) {
    std::string out = extract_and_render(
        "pub class Point {\n"
        "\t// Measures size.\n"
        "\tpub fn size() -> i32 {\n"
        "\t\treturn self.x;\n"
        "\t}\n"
        "}\n",
        "Point.size"
    );

    EXPECT_EQ(out, "Point.size() -> i32\n\nMeasures size.\n");
}

TEST_F(BaslDocTest, SymbolLookupInterfaceMethod) {
    std::string out = extract_and_render(
        "pub interface Formatter {\n"
        "\t// Formats a label.\n"
        "\tfn format(string label) -> string;\n"
        "}\n",
        "Formatter.format"
    );

    EXPECT_EQ(out, "Formatter.format(string label) -> string\n\nFormats a label.\n");
}

TEST_F(BaslDocTest, MissingSymbolReturnsError) {
    basl_source_id_t source_id = 0;
    const char *src = "pub fn add(i32 a, i32 b) -> i32 {\n\treturn a + b;\n}\n";
    ASSERT_EQ(basl_source_registry_register_cstr(
        &registry, "test.basl", src, &source_id, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
              BASL_STATUS_OK);

    basl_doc_module_t module;
    ASSERT_EQ(basl_doc_extract(nullptr, "test.basl", 9, src, strlen(src),
                                &tokens, &module, &error), BASL_STATUS_OK);

    char *text = nullptr;
    size_t length = 0;
    EXPECT_EQ(basl_doc_render(&module, "missing", &text, &length, &error),
              BASL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(text, nullptr);
    basl_doc_module_free(&module);
}

TEST_F(BaslDocTest, ComplexReturnTypes) {
    std::string out = extract_and_render(
        "pub fn get_pair() -> (i32, err) {\n"
        "\treturn (0, ok);\n"
        "}\n"
    );

    EXPECT_NE(out.find("get_pair() -> (i32, err)"), std::string::npos);
}

TEST_F(BaslDocTest, GenericTypes) {
    std::string out = extract_and_render(
        "pub fn process(array<string> items, map<string, i32> counts) -> void {\n"
        "}\n"
    );

    EXPECT_NE(out.find("process(array<string> items, map<string, i32> counts) -> void"), std::string::npos);
}

TEST_F(BaslDocTest, PrivateDeclarationsHidden) {
    std::string out = extract_and_render(
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
        "i32 private_var = 0;\n"
    );

    EXPECT_EQ(out.find("private"), std::string::npos);
    EXPECT_EQ(out.find("Private"), std::string::npos);
    EXPECT_EQ(out.find("PRIVATE"), std::string::npos);
}

TEST_F(BaslDocTest, EmptyFileShowsModuleOnly) {
    std::string out = extract_and_render("");
    EXPECT_NE(out.find("MODULE\n  test"), std::string::npos);
    EXPECT_EQ(out.find("SUMMARY"), std::string::npos);
    EXPECT_EQ(out.find("FUNCTIONS"), std::string::npos);
}

TEST_F(BaslDocTest, ModuleNameFromPath) {
    basl_source_id_t source_id = 0;
    const char *src = "pub fn hello() -> void {\n}\n";
    ASSERT_EQ(basl_source_registry_register_cstr(
        &registry, "lib/utils.basl", src, &source_id, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error),
              BASL_STATUS_OK);

    basl_doc_module_t module;
    ASSERT_EQ(basl_doc_extract(nullptr, "lib/utils.basl", 14, src, strlen(src),
                                &tokens, &module, &error), BASL_STATUS_OK);

    char *text = nullptr;
    size_t length = 0;
    ASSERT_EQ(basl_doc_render(&module, nullptr, &text, &length, &error), BASL_STATUS_OK);
    std::string out(text, length);
    free(text);
    EXPECT_NE(out.find("MODULE\n  utils"), std::string::npos);
    basl_doc_module_free(&module);
}
