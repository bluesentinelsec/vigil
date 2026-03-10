#include <gtest/gtest.h>

extern "C" {
#include "basl/basl.h"
}

TEST(BaslTest, SumAddsTwoIntegers) {
    EXPECT_EQ(basl_sum(2, 3), 5);
}

TEST(BaslTest, SumHandlesNegativeValues) {
    EXPECT_EQ(basl_sum(-2, 3), 1);
}
