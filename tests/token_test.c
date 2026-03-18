#include "vigil_test.h"


#include "vigil/vigil.h"

TEST(VigilTokenTest, KindNamesAreStable) {
    EXPECT_STREQ(vigil_token_kind_name(VIGIL_TOKEN_EOF), "eof");
    EXPECT_STREQ(vigil_token_kind_name(VIGIL_TOKEN_FN), "fn");
    EXPECT_STREQ(vigil_token_kind_name(VIGIL_TOKEN_NIL), "nil");
    EXPECT_STREQ(vigil_token_kind_name(VIGIL_TOKEN_STRING_LITERAL), "string_literal");
    EXPECT_STREQ(vigil_token_kind_name(VIGIL_TOKEN_ARROW), "arrow");
    EXPECT_STREQ(vigil_token_kind_name((vigil_token_kind_t)999), "unknown");
}

TEST(VigilTokenTest, ListInitAndFreeResetState) {
    vigil_token_list_t list;

    vigil_token_list_init(&list, NULL);
    EXPECT_EQ(list.runtime, NULL);
    EXPECT_EQ(list.items, NULL);
    EXPECT_EQ(list.count, 0U);
    EXPECT_EQ(list.capacity, 0U);

    vigil_token_list_free(&list);
    EXPECT_EQ(list.runtime, NULL);
    EXPECT_EQ(list.items, NULL);
    EXPECT_EQ(list.count, 0U);
    EXPECT_EQ(list.capacity, 0U);
}

void register_token_tests(void) {
    REGISTER_TEST(VigilTokenTest, KindNamesAreStable);
    REGISTER_TEST(VigilTokenTest, ListInitAndFreeResetState);
}
