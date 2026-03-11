#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "basl/basl.h"
}

namespace {

void ExpectClearedLocation(const basl_source_location_t &location) {
    EXPECT_EQ(location.source_id, 0U);
    EXPECT_EQ(location.line, 0U);
    EXPECT_EQ(location.column, 0U);
}

}  // namespace

TEST(BaslStatusTest, StatusNamesAreStable) {
    EXPECT_STREQ(basl_status_name(BASL_STATUS_OK), "ok");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_INVALID_ARGUMENT), "invalid_argument");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_OUT_OF_MEMORY), "out_of_memory");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_INTERNAL), "internal");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_UNSUPPORTED), "unsupported");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_SYNTAX_ERROR), "syntax_error");
}

TEST(BaslStatusTest, ErrorClearResetsSourceLocation) {
    basl_error_t error = {};

    error.type = BASL_STATUS_INTERNAL;
    error.value = "bad";
    error.length = 3U;
    error.location.source_id = 7U;
    error.location.line = 11U;
    error.location.column = 13U;

    basl_error_clear(&error);

    EXPECT_EQ(error.type, BASL_STATUS_OK);
    EXPECT_EQ(error.value, nullptr);
    EXPECT_EQ(error.length, 0U);
    ExpectClearedLocation(error.location);
}

TEST(BaslStatusTest, SourceLocationClearResetsFields) {
    basl_source_location_t location = {};

    location.source_id = 3U;
    location.line = 5U;
    location.column = 8U;

    basl_source_location_clear(&location);

    ExpectClearedLocation(location);
}

TEST(BaslStatusTest, ErrorLengthMatchesMessage) {
    basl_error_t error = {};

    error.type = BASL_STATUS_INVALID_ARGUMENT;
    error.value = "example";
    error.length = std::strlen(error.value);

    EXPECT_EQ(error.length, 7U);
}

TEST(BaslStatusTest, ErrorMessageFallsBackToKnownString) {
    basl_error_t error = {};

    EXPECT_STREQ(basl_error_message(nullptr), "unknown error");
    EXPECT_STREQ(basl_error_message(&error), "unknown error");

    error.value = "specific";
    EXPECT_STREQ(basl_error_message(&error), "specific");
}
