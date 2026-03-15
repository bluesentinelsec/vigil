#include "basl_test.h"

#include <string.h>


#include "basl/basl.h"

static void ExpectClearedLocation(int *basl_test_failed_, const basl_source_location_t *location) {
    EXPECT_EQ(location->source_id, 0U);
    EXPECT_EQ(location->offset, 0U);
    EXPECT_EQ(location->line, 0U);
    EXPECT_EQ(location->column, 0U);
}


TEST(BaslStatusTest, StatusNamesAreStable) {
    EXPECT_STREQ(basl_status_name(BASL_STATUS_OK), "ok");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_INVALID_ARGUMENT), "invalid_argument");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_OUT_OF_MEMORY), "out_of_memory");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_INTERNAL), "internal");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_UNSUPPORTED), "unsupported");
    EXPECT_STREQ(basl_status_name(BASL_STATUS_SYNTAX_ERROR), "syntax_error");
}

TEST(BaslStatusTest, ErrorClearResetsSourceLocation) {
    basl_error_t error = {0};

    error.type = BASL_STATUS_INTERNAL;
    error.value = "bad";
    error.length = 3U;
    error.location.source_id = 7U;
    error.location.offset = 17U;
    error.location.line = 11U;
    error.location.column = 13U;

    basl_error_clear(&error);

    EXPECT_EQ(error.type, BASL_STATUS_OK);
    EXPECT_EQ(error.value, NULL);
    EXPECT_EQ(error.length, 0U);
    ExpectClearedLocation(basl_test_failed_, &error.location);
}

TEST(BaslStatusTest, SourceLocationClearResetsFields) {
    basl_source_location_t location = {0};

    location.source_id = 3U;
    location.offset = 21U;
    location.line = 5U;
    location.column = 8U;

    basl_source_location_clear(&location);

    ExpectClearedLocation(basl_test_failed_, &location);
}

TEST(BaslStatusTest, ErrorLengthMatchesMessage) {
    basl_error_t error = {0};

    error.type = BASL_STATUS_INVALID_ARGUMENT;
    error.value = "example";
    error.length = strlen(error.value);

    EXPECT_EQ(error.length, 7U);
}

TEST(BaslStatusTest, ErrorMessageFallsBackToKnownString) {
    basl_error_t error = {0};

    EXPECT_STREQ(basl_error_message(NULL), "unknown error");
    EXPECT_STREQ(basl_error_message(&error), "unknown error");

    error.value = "specific";
    EXPECT_STREQ(basl_error_message(&error), "specific");
}

void register_status_tests(void) {
    REGISTER_TEST(BaslStatusTest, StatusNamesAreStable);
    REGISTER_TEST(BaslStatusTest, ErrorClearResetsSourceLocation);
    REGISTER_TEST(BaslStatusTest, SourceLocationClearResetsFields);
    REGISTER_TEST(BaslStatusTest, ErrorLengthMatchesMessage);
    REGISTER_TEST(BaslStatusTest, ErrorMessageFallsBackToKnownString);
}
