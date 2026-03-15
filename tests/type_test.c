#include "basl_test.h"


#include "basl/basl.h"

TEST(BaslTypeTest, KindNamesAndParsingAreStable) {
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_INVALID), "invalid");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_I32), "i32");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_I64), "i64");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_U8), "u8");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_U32), "u32");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_U64), "u64");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_F64), "f64");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_BOOL), "bool");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_STRING), "string");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_ERR), "err");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_VOID), "void");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_NIL), "nil");
    EXPECT_STREQ(basl_type_kind_name(BASL_TYPE_OBJECT), "object");

    EXPECT_EQ(basl_type_kind_from_name("i32", 3U), BASL_TYPE_I32);
    EXPECT_EQ(basl_type_kind_from_name("i64", 3U), BASL_TYPE_I64);
    EXPECT_EQ(basl_type_kind_from_name("u8", 2U), BASL_TYPE_U8);
    EXPECT_EQ(basl_type_kind_from_name("u32", 3U), BASL_TYPE_U32);
    EXPECT_EQ(basl_type_kind_from_name("u64", 3U), BASL_TYPE_U64);
    EXPECT_EQ(basl_type_kind_from_name("f64", 3U), BASL_TYPE_F64);
    EXPECT_EQ(basl_type_kind_from_name("bool", 4U), BASL_TYPE_BOOL);
    EXPECT_EQ(basl_type_kind_from_name("string", 6U), BASL_TYPE_STRING);
    EXPECT_EQ(basl_type_kind_from_name("err", 3U), BASL_TYPE_ERR);
    EXPECT_EQ(basl_type_kind_from_name("void", 4U), BASL_TYPE_VOID);
    EXPECT_EQ(basl_type_kind_from_name("nil", 3U), BASL_TYPE_NIL);
}

TEST(BaslTypeTest, AssignabilityRequiresMatchingValidTypes) {
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_I32, BASL_TYPE_I32));
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_I64, BASL_TYPE_I64));
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_U8, BASL_TYPE_U8));
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_U32, BASL_TYPE_U32));
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_U64, BASL_TYPE_U64));
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_F64, BASL_TYPE_F64));
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_BOOL, BASL_TYPE_BOOL));
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_STRING, BASL_TYPE_STRING));
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_ERR, BASL_TYPE_ERR));
    EXPECT_TRUE(basl_type_is_assignable(BASL_TYPE_OBJECT, BASL_TYPE_OBJECT));
    EXPECT_FALSE(basl_type_is_assignable(BASL_TYPE_I32, BASL_TYPE_BOOL));
    EXPECT_FALSE(basl_type_is_assignable(BASL_TYPE_BOOL, BASL_TYPE_NIL));
    EXPECT_FALSE(basl_type_is_assignable(BASL_TYPE_INVALID, BASL_TYPE_I32));
}

TEST(BaslTypeTest, UnaryAndBinaryOperatorSupportMatchesCurrentLanguageRules) {
    EXPECT_TRUE(
        basl_type_supports_unary_operator(BASL_UNARY_OPERATOR_NEGATE, BASL_TYPE_I32)
    );
    EXPECT_TRUE(
        basl_type_supports_unary_operator(BASL_UNARY_OPERATOR_NEGATE, BASL_TYPE_I64)
    );
    EXPECT_TRUE(
        basl_type_supports_unary_operator(BASL_UNARY_OPERATOR_NEGATE, BASL_TYPE_F64)
    );
    EXPECT_FALSE(
        basl_type_supports_unary_operator(BASL_UNARY_OPERATOR_NEGATE, BASL_TYPE_BOOL)
    );
    EXPECT_FALSE(
        basl_type_supports_unary_operator(BASL_UNARY_OPERATOR_NEGATE, BASL_TYPE_U8)
    );
    EXPECT_TRUE(
        basl_type_supports_unary_operator(
            BASL_UNARY_OPERATOR_LOGICAL_NOT,
            BASL_TYPE_BOOL
        )
    );
    EXPECT_TRUE(
        basl_type_supports_unary_operator(
            BASL_UNARY_OPERATOR_BITWISE_NOT,
            BASL_TYPE_I32
        )
    );
    EXPECT_FALSE(
        basl_type_supports_unary_operator(BASL_UNARY_OPERATOR_LOGICAL_NOT, BASL_TYPE_I32)
    );
    EXPECT_FALSE(
        basl_type_supports_unary_operator(
            BASL_UNARY_OPERATOR_BITWISE_NOT,
            BASL_TYPE_F64
        )
    );
    EXPECT_FALSE(
        basl_type_supports_unary_operator(
            BASL_UNARY_OPERATOR_BITWISE_NOT,
            BASL_TYPE_U32
        )
    );

    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_ADD,
            BASL_TYPE_I32,
            BASL_TYPE_I32
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_ADD,
            BASL_TYPE_I64,
            BASL_TYPE_I64
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_ADD,
            BASL_TYPE_F64,
            BASL_TYPE_F64
        )
    );
    EXPECT_FALSE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_ADD,
            BASL_TYPE_BOOL,
            BASL_TYPE_I32
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_ADD,
            BASL_TYPE_STRING,
            BASL_TYPE_STRING
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_EQUAL,
            BASL_TYPE_BOOL,
            BASL_TYPE_BOOL
        )
    );
    EXPECT_FALSE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_EQUAL,
            BASL_TYPE_BOOL,
            BASL_TYPE_I32
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_BITWISE_AND,
            BASL_TYPE_U32,
            BASL_TYPE_U32
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_BITWISE_AND,
            BASL_TYPE_I32,
            BASL_TYPE_I32
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_SHIFT_LEFT,
            BASL_TYPE_U8,
            BASL_TYPE_U8
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_SHIFT_LEFT,
            BASL_TYPE_I32,
            BASL_TYPE_I32
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_GREATER,
            BASL_TYPE_F64,
            BASL_TYPE_F64
        )
    );
    EXPECT_FALSE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_BITWISE_OR,
            BASL_TYPE_BOOL,
            BASL_TYPE_I32
        )
    );
    EXPECT_TRUE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_LOGICAL_AND,
            BASL_TYPE_BOOL,
            BASL_TYPE_BOOL
        )
    );
    EXPECT_FALSE(
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_LOGICAL_AND,
            BASL_TYPE_I32,
            BASL_TYPE_BOOL
        )
    );
}

TEST(BaslTypeTest, FunctionSignaturesValidateAndCheckArguments) {
    basl_type_kind_t parameter_types[2] = {BASL_TYPE_I32, BASL_TYPE_BOOL};
    basl_type_kind_t valid_arguments[2] = {BASL_TYPE_I32, BASL_TYPE_BOOL};
    basl_type_kind_t invalid_arguments[2] = {BASL_TYPE_BOOL, BASL_TYPE_BOOL};
    basl_function_signature_t signature = {
        BASL_TYPE_I32,
        parameter_types,
        2U
    };
    basl_function_signature_t invalid_signature = {
        BASL_TYPE_INVALID,
        parameter_types,
        2U
    };

    EXPECT_TRUE(basl_function_signature_is_valid(&signature));
    EXPECT_FALSE(basl_function_signature_is_valid(&invalid_signature));
    EXPECT_TRUE(
        basl_function_signature_accepts_arguments(&signature, valid_arguments, 2U)
    );
    EXPECT_FALSE(
        basl_function_signature_accepts_arguments(&signature, valid_arguments, 1U)
    );
    EXPECT_FALSE(
        basl_function_signature_accepts_arguments(&signature, invalid_arguments, 2U)
    );
}

void register_type_tests(void) {
    REGISTER_TEST(BaslTypeTest, KindNamesAndParsingAreStable);
    REGISTER_TEST(BaslTypeTest, AssignabilityRequiresMatchingValidTypes);
    REGISTER_TEST(BaslTypeTest, UnaryAndBinaryOperatorSupportMatchesCurrentLanguageRules);
    REGISTER_TEST(BaslTypeTest, FunctionSignaturesValidateAndCheckArguments);
}
