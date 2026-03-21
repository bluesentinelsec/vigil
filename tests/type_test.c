#include "vigil_test.h"

#include "vigil/vigil.h"

typedef struct
{
    vigil_type_kind_t kind;
    const char *name;
} type_name_case_t;

typedef struct
{
    const char *name;
    size_t length;
    vigil_type_kind_t kind;
} type_parse_case_t;

static void expect_type_name_cases(const type_name_case_t *cases, size_t count)
{
    size_t index;

    for (index = 0U; index < count; index += 1U)
        EXPECT_STREQ(vigil_type_kind_name(cases[index].kind), cases[index].name);
}

static void expect_type_parse_cases(const type_parse_case_t *cases, size_t count)
{
    size_t index;

    for (index = 0U; index < count; index += 1U)
        EXPECT_EQ(vigil_type_kind_from_name(cases[index].name, cases[index].length), cases[index].kind);
}

TEST(VigilTypeTest, KindNamesAreStable)
{
    static const type_name_case_t cases[] = {
        {VIGIL_TYPE_INVALID, "invalid"}, {VIGIL_TYPE_I32, "i32"}, {VIGIL_TYPE_I64, "i64"},   {VIGIL_TYPE_U8, "u8"},
        {VIGIL_TYPE_U32, "u32"},         {VIGIL_TYPE_U64, "u64"}, {VIGIL_TYPE_F64, "f64"},   {VIGIL_TYPE_BOOL, "bool"},
        {VIGIL_TYPE_STRING, "string"},   {VIGIL_TYPE_ERR, "err"}, {VIGIL_TYPE_VOID, "void"}, {VIGIL_TYPE_NIL, "nil"},
        {VIGIL_TYPE_OBJECT, "object"},
    };

    expect_type_name_cases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(VigilTypeTest, KindParsingAcceptsBuiltinNames)
{
    static const type_parse_case_t cases[] = {
        {"i32", 3U, VIGIL_TYPE_I32},   {"i64", 3U, VIGIL_TYPE_I64},       {"u8", 2U, VIGIL_TYPE_U8},
        {"u32", 3U, VIGIL_TYPE_U32},   {"u64", 3U, VIGIL_TYPE_U64},       {"f64", 3U, VIGIL_TYPE_F64},
        {"bool", 4U, VIGIL_TYPE_BOOL}, {"string", 6U, VIGIL_TYPE_STRING}, {"err", 3U, VIGIL_TYPE_ERR},
        {"void", 4U, VIGIL_TYPE_VOID}, {"nil", 3U, VIGIL_TYPE_NIL},
    };

    expect_type_parse_cases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(VigilTypeTest, KindParsingRejectsUnknownNames)
{
    EXPECT_EQ(vigil_type_kind_from_name("object", 6U), VIGIL_TYPE_INVALID);
    EXPECT_EQ(vigil_type_kind_from_name("unknown", 7U), VIGIL_TYPE_INVALID);
    EXPECT_EQ(vigil_type_kind_from_name(NULL, 0U), VIGIL_TYPE_INVALID);
}

TEST(VigilTypeTest, AssignabilityRequiresMatchingValidTypes)
{
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_I32, VIGIL_TYPE_I32));
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_I64, VIGIL_TYPE_I64));
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_U8, VIGIL_TYPE_U8));
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_U32, VIGIL_TYPE_U32));
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_U64, VIGIL_TYPE_U64));
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_F64, VIGIL_TYPE_F64));
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_BOOL, VIGIL_TYPE_BOOL));
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_STRING, VIGIL_TYPE_STRING));
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_ERR, VIGIL_TYPE_ERR));
    EXPECT_TRUE(vigil_type_is_assignable(VIGIL_TYPE_OBJECT, VIGIL_TYPE_OBJECT));
    EXPECT_FALSE(vigil_type_is_assignable(VIGIL_TYPE_I32, VIGIL_TYPE_BOOL));
    EXPECT_FALSE(vigil_type_is_assignable(VIGIL_TYPE_BOOL, VIGIL_TYPE_NIL));
    EXPECT_FALSE(vigil_type_is_assignable(VIGIL_TYPE_INVALID, VIGIL_TYPE_I32));
}

TEST(VigilTypeTest, UnaryOperatorSupportMatchesCurrentLanguageRules)
{
    EXPECT_TRUE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_NEGATE, VIGIL_TYPE_I32));
    EXPECT_TRUE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_NEGATE, VIGIL_TYPE_I64));
    EXPECT_TRUE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_NEGATE, VIGIL_TYPE_F64));
    EXPECT_FALSE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_NEGATE, VIGIL_TYPE_BOOL));
    EXPECT_FALSE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_NEGATE, VIGIL_TYPE_U8));
    EXPECT_TRUE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_LOGICAL_NOT, VIGIL_TYPE_BOOL));
    EXPECT_TRUE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_BITWISE_NOT, VIGIL_TYPE_I32));
    EXPECT_FALSE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_LOGICAL_NOT, VIGIL_TYPE_I32));
    EXPECT_FALSE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_BITWISE_NOT, VIGIL_TYPE_F64));
    EXPECT_FALSE(vigil_type_supports_unary_operator(VIGIL_UNARY_OPERATOR_BITWISE_NOT, VIGIL_TYPE_U32));
}

TEST(VigilTypeTest, BinaryOperatorAddAndArithmeticRules)
{
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_ADD, VIGIL_TYPE_I32, VIGIL_TYPE_I32));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_ADD, VIGIL_TYPE_I64, VIGIL_TYPE_I64));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_ADD, VIGIL_TYPE_F64, VIGIL_TYPE_F64));
    EXPECT_FALSE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_ADD, VIGIL_TYPE_BOOL, VIGIL_TYPE_I32));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_ADD, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_SUBTRACT, VIGIL_TYPE_I32, VIGIL_TYPE_I32));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_DIVIDE, VIGIL_TYPE_F64, VIGIL_TYPE_F64));
    EXPECT_FALSE(
        vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_MULTIPLY, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING));
}

TEST(VigilTypeTest, BinaryOperatorBitwiseRules)
{
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_BITWISE_AND, VIGIL_TYPE_U32, VIGIL_TYPE_U32));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_BITWISE_AND, VIGIL_TYPE_I32, VIGIL_TYPE_I32));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_SHIFT_LEFT, VIGIL_TYPE_U8, VIGIL_TYPE_U8));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_SHIFT_LEFT, VIGIL_TYPE_I32, VIGIL_TYPE_I32));
    EXPECT_FALSE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_MODULO, VIGIL_TYPE_F64, VIGIL_TYPE_F64));
    EXPECT_FALSE(
        vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_BITWISE_OR, VIGIL_TYPE_BOOL, VIGIL_TYPE_I32));
}

TEST(VigilTypeTest, BinaryOperatorComparisonAndEqualityRules)
{
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_EQUAL, VIGIL_TYPE_BOOL, VIGIL_TYPE_BOOL));
    EXPECT_FALSE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_EQUAL, VIGIL_TYPE_BOOL, VIGIL_TYPE_I32));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_NOT_EQUAL, VIGIL_TYPE_ERR, VIGIL_TYPE_ERR));
    EXPECT_TRUE(vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_GREATER, VIGIL_TYPE_F64, VIGIL_TYPE_F64));
    EXPECT_TRUE(
        vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_LESS_EQUAL, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING));
}

TEST(VigilTypeTest, BinaryOperatorLogicalRules)
{
    EXPECT_TRUE(
        vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_LOGICAL_AND, VIGIL_TYPE_BOOL, VIGIL_TYPE_BOOL));
    EXPECT_TRUE(
        vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_LOGICAL_OR, VIGIL_TYPE_BOOL, VIGIL_TYPE_BOOL));
    EXPECT_FALSE(
        vigil_type_supports_binary_operator(VIGIL_BINARY_OPERATOR_LOGICAL_AND, VIGIL_TYPE_I32, VIGIL_TYPE_BOOL));
    EXPECT_FALSE(vigil_type_supports_binary_operator((vigil_binary_operator_kind_t)-1, VIGIL_TYPE_I32, VIGIL_TYPE_I32));
}

TEST(VigilTypeTest, FunctionSignaturesValidateAndCheckArguments)
{
    vigil_type_kind_t parameter_types[2] = {VIGIL_TYPE_I32, VIGIL_TYPE_BOOL};
    vigil_type_kind_t valid_arguments[2] = {VIGIL_TYPE_I32, VIGIL_TYPE_BOOL};
    vigil_type_kind_t invalid_arguments[2] = {VIGIL_TYPE_BOOL, VIGIL_TYPE_BOOL};
    vigil_function_signature_t signature = {VIGIL_TYPE_I32, parameter_types, 2U};
    vigil_function_signature_t invalid_signature = {VIGIL_TYPE_INVALID, parameter_types, 2U};

    EXPECT_TRUE(vigil_function_signature_is_valid(&signature));
    EXPECT_FALSE(vigil_function_signature_is_valid(&invalid_signature));
    EXPECT_TRUE(vigil_function_signature_accepts_arguments(&signature, valid_arguments, 2U));
    EXPECT_FALSE(vigil_function_signature_accepts_arguments(&signature, valid_arguments, 1U));
    EXPECT_FALSE(vigil_function_signature_accepts_arguments(&signature, invalid_arguments, 2U));
}

void register_type_tests(void)
{
    REGISTER_TEST(VigilTypeTest, KindNamesAreStable);
    REGISTER_TEST(VigilTypeTest, KindParsingAcceptsBuiltinNames);
    REGISTER_TEST(VigilTypeTest, KindParsingRejectsUnknownNames);
    REGISTER_TEST(VigilTypeTest, AssignabilityRequiresMatchingValidTypes);
    REGISTER_TEST(VigilTypeTest, UnaryOperatorSupportMatchesCurrentLanguageRules);
    REGISTER_TEST(VigilTypeTest, BinaryOperatorAddAndArithmeticRules);
    REGISTER_TEST(VigilTypeTest, BinaryOperatorBitwiseRules);
    REGISTER_TEST(VigilTypeTest, BinaryOperatorComparisonAndEqualityRules);
    REGISTER_TEST(VigilTypeTest, BinaryOperatorLogicalRules);
    REGISTER_TEST(VigilTypeTest, FunctionSignaturesValidateAndCheckArguments);
}
