#include <stddef.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/string.h"

static size_t basl_string_storage_length(const basl_string_t *string) {
    if (string == NULL) {
        return 0U;
    }

    return string->bytes.length;
}

static basl_status_t basl_string_validate_mutable(
    const basl_string_t *string,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (string == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (string->bytes.runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static int basl_string_overlaps_storage(
    const basl_string_t *string,
    const char *value,
    size_t length
) {
    const uint8_t *start;
    const uint8_t *end;
    const uint8_t *value_start;
    const uint8_t *value_end;

    if (string == NULL || value == NULL || length == 0U || string->bytes.data == NULL) {
        return 0;
    }

    start = string->bytes.data;
    end = start + basl_string_length(string);
    value_start = (const uint8_t *)value;
    value_end = value_start + length;

    return value_start < end && value_end > start;
}

static basl_status_t basl_string_copy_input(
    basl_string_t *string,
    const char *value,
    size_t length,
    const char **out_value,
    void **out_temp,
    basl_error_t *error
) {
    basl_status_t status;

    *out_value = value;
    *out_temp = NULL;

    if (!basl_string_overlaps_storage(string, value, length)) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    status = basl_runtime_alloc(string->bytes.runtime, length, out_temp, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    memcpy(*out_temp, value, length);
    *out_value = (const char *)*out_temp;
    return BASL_STATUS_OK;
}

static basl_status_t basl_string_prepare_storage(
    basl_string_t *string,
    size_t length,
    basl_error_t *error
) {
    basl_status_t status;

    if (length == SIZE_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string length would overflow"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_byte_buffer_reserve(&string->bytes, length + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    string->bytes.length = length + 1U;
    string->bytes.data[length] = '\0';
    return BASL_STATUS_OK;
}

void basl_string_init(
    basl_string_t *string,
    basl_runtime_t *runtime
) {
    if (string == NULL) {
        return;
    }

    basl_byte_buffer_init(&string->bytes, runtime);
}

void basl_string_clear(basl_string_t *string) {
    if (string == NULL) {
        return;
    }

    if (string->bytes.data != NULL) {
        string->bytes.length = 1U;
        string->bytes.data[0] = '\0';
    }
}

void basl_string_free(basl_string_t *string) {
    if (string == NULL) {
        return;
    }

    basl_byte_buffer_free(&string->bytes);
}

size_t basl_string_length(const basl_string_t *string) {
    size_t storage_length;

    storage_length = basl_string_storage_length(string);
    if (storage_length == 0U) {
        return 0U;
    }

    return storage_length - 1U;
}

const char *basl_string_c_str(const basl_string_t *string) {
    static const char empty[] = "";

    if (string == NULL || string->bytes.data == NULL || string->bytes.length == 0U) {
        return empty;
    }

    return (const char *)string->bytes.data;
}

basl_status_t basl_string_reserve(
    basl_string_t *string,
    size_t length,
    basl_error_t *error
) {
    basl_status_t status;

    status = basl_string_validate_mutable(string, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (length == SIZE_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string reserve would overflow"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_byte_buffer_reserve(&string->bytes, length + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (string->bytes.length == 0U) {
        string->bytes.length = 1U;
        string->bytes.data[0] = '\0';
    }

    return BASL_STATUS_OK;
}

basl_status_t basl_string_assign(
    basl_string_t *string,
    const char *value,
    size_t length,
    basl_error_t *error
) {
    basl_status_t status;
    const char *copied_value;
    void *temp;

    status = basl_string_validate_mutable(string, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_string_copy_input(string, value, length, &copied_value, &temp, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_string_prepare_storage(string, length, error);
    if (status != BASL_STATUS_OK) {
        basl_runtime_free(string->bytes.runtime, &temp);
        return status;
    }

    if (length != 0U) {
        memcpy(string->bytes.data, copied_value, length);
        string->bytes.data[length] = '\0';
    }

    basl_runtime_free(string->bytes.runtime, &temp);
    return BASL_STATUS_OK;
}

basl_status_t basl_string_assign_cstr(
    basl_string_t *string,
    const char *value,
    basl_error_t *error
) {
    size_t length;

    if (value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    length = strlen(value);
    return basl_string_assign(string, value, length, error);
}

basl_status_t basl_string_append(
    basl_string_t *string,
    const char *value,
    size_t length,
    basl_error_t *error
) {
    basl_status_t status;
    size_t old_length;
    const char *copied_value;
    void *temp;

    status = basl_string_validate_mutable(string, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    old_length = basl_string_length(string);
    if (length > SIZE_MAX - old_length - 1U) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string append would overflow"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_string_copy_input(string, value, length, &copied_value, &temp, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_string_prepare_storage(string, old_length + length, error);
    if (status != BASL_STATUS_OK) {
        basl_runtime_free(string->bytes.runtime, &temp);
        return status;
    }

    if (length != 0U) {
        memcpy(string->bytes.data + old_length, copied_value, length);
        string->bytes.data[old_length + length] = '\0';
    }

    basl_runtime_free(string->bytes.runtime, &temp);
    return BASL_STATUS_OK;
}

basl_status_t basl_string_append_cstr(
    basl_string_t *string,
    const char *value,
    basl_error_t *error
) {
    size_t length;

    if (value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    length = strlen(value);
    return basl_string_append(string, value, length, error);
}

int basl_string_compare(
    const basl_string_t *left,
    const basl_string_t *right
) {
    size_t left_length;
    size_t right_length;
    size_t common_length;
    int compared;

    left_length = basl_string_length(left);
    right_length = basl_string_length(right);
    common_length = left_length < right_length ? left_length : right_length;

    compared = memcmp(basl_string_c_str(left), basl_string_c_str(right), common_length);
    if (compared != 0) {
        return compared;
    }

    if (left_length < right_length) {
        return -1;
    }

    if (left_length > right_length) {
        return 1;
    }

    return 0;
}

int basl_string_equals_cstr(
    const basl_string_t *string,
    const char *value
) {
    size_t value_length;

    value_length = value == NULL ? 0U : strlen(value);
    if (basl_string_length(string) != value_length) {
        return 0;
    }

    return memcmp(basl_string_c_str(string), value == NULL ? "" : value, value_length) == 0;
}
