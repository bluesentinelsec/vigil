#include "basl_test.h"


#include "basl/basl.h"

TEST(BaslTokenTest, KindNamesAreStable) {
    EXPECT_STREQ(basl_token_kind_name(BASL_TOKEN_EOF), "eof");
    EXPECT_STREQ(basl_token_kind_name(BASL_TOKEN_FN), "fn");
    EXPECT_STREQ(basl_token_kind_name(BASL_TOKEN_NIL), "nil");
    EXPECT_STREQ(basl_token_kind_name(BASL_TOKEN_STRING_LITERAL), "string_literal");
    EXPECT_STREQ(basl_token_kind_name(BASL_TOKEN_ARROW), "arrow");
    EXPECT_STREQ(basl_token_kind_name((basl_token_kind_t)999), "unknown");
}

TEST(BaslTokenTest, ListInitAndFreeResetState) {
    basl_token_list_t list;

    basl_token_list_init(&list, NULL);
    EXPECT_EQ(list.runtime, NULL);
    EXPECT_EQ(list.items, NULL);
    EXPECT_EQ(list.count, 0U);
    EXPECT_EQ(list.capacity, 0U);

    basl_token_list_free(&list);
    EXPECT_EQ(list.runtime, NULL);
    EXPECT_EQ(list.items, NULL);
    EXPECT_EQ(list.count, 0U);
    EXPECT_EQ(list.capacity, 0U);
}
