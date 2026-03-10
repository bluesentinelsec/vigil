#include <stddef.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/string.h"
#include "basl/value.h"

struct basl_object {
    basl_runtime_t *runtime;
    basl_object_type_t type;
    size_t ref_count;
};

typedef struct basl_string_object {
    basl_object_t base;
    basl_string_t value;
} basl_string_object_t;

static basl_string_object_t *basl_string_object_cast(
    const basl_object_t *object
) {
    if (object == NULL || object->type != BASL_OBJECT_STRING) {
        return NULL;
    }

    return (basl_string_object_t *)object;
}

static void basl_object_destroy(basl_object_t *object) {
    basl_runtime_t *runtime;
    void *memory;
    basl_string_object_t *string_object;

    if (object == NULL) {
        return;
    }

    runtime = object->runtime;
    switch (object->type) {
        case BASL_OBJECT_STRING:
            string_object = (basl_string_object_t *)object;
            basl_string_free(&string_object->value);
            break;
        case BASL_OBJECT_INVALID:
        default:
            break;
    }

    memory = object;
    if (runtime != NULL) {
        basl_runtime_free(runtime, &memory);
    }
}

static void basl_object_init(
    basl_object_t *object,
    basl_runtime_t *runtime,
    basl_object_type_t type
) {
    if (object == NULL) {
        return;
    }

    object->runtime = runtime;
    object->type = type;
    object->ref_count = 1U;
}

void basl_value_init_nil(basl_value_t *value) {
    if (value == NULL) {
        return;
    }

    memset(value, 0, sizeof(*value));
    value->kind = BASL_VALUE_NIL;
}

void basl_value_init_bool(basl_value_t *value, bool boolean) {
    if (value == NULL) {
        return;
    }

    basl_value_init_nil(value);
    value->kind = BASL_VALUE_BOOL;
    value->as.boolean = boolean;
}

void basl_value_init_int(basl_value_t *value, int64_t integer) {
    if (value == NULL) {
        return;
    }

    basl_value_init_nil(value);
    value->kind = BASL_VALUE_INT;
    value->as.integer = integer;
}

void basl_value_init_float(basl_value_t *value, double number) {
    if (value == NULL) {
        return;
    }

    basl_value_init_nil(value);
    value->kind = BASL_VALUE_FLOAT;
    value->as.number = number;
}

void basl_value_init_object(
    basl_value_t *value,
    basl_object_t **object
) {
    basl_object_t *resolved_object;

    if (value == NULL) {
        return;
    }

    basl_value_init_nil(value);

    if (object == NULL || *object == NULL) {
        return;
    }

    resolved_object = *object;
    *object = NULL;
    value->kind = BASL_VALUE_OBJECT;
    value->as.object = resolved_object;
}

basl_value_t basl_value_copy(const basl_value_t *value) {
    basl_value_t copy;

    basl_value_init_nil(&copy);
    if (value == NULL) {
        return copy;
    }

    copy = *value;
    if (copy.kind == BASL_VALUE_OBJECT) {
        basl_object_retain(copy.as.object);
    }

    return copy;
}

void basl_value_release(basl_value_t *value) {
    basl_object_t *object;

    if (value == NULL) {
        return;
    }

    if (value->kind == BASL_VALUE_OBJECT) {
        object = value->as.object;
        basl_object_release(&object);
    }

    basl_value_init_nil(value);
}

basl_value_kind_t basl_value_kind(const basl_value_t *value) {
    if (value == NULL) {
        return BASL_VALUE_NIL;
    }

    return value->kind;
}

bool basl_value_as_bool(const basl_value_t *value) {
    if (value == NULL || value->kind != BASL_VALUE_BOOL) {
        return false;
    }

    return value->as.boolean;
}

int64_t basl_value_as_int(const basl_value_t *value) {
    if (value == NULL || value->kind != BASL_VALUE_INT) {
        return 0;
    }

    return value->as.integer;
}

double basl_value_as_float(const basl_value_t *value) {
    if (value == NULL || value->kind != BASL_VALUE_FLOAT) {
        return 0.0;
    }

    return value->as.number;
}

basl_object_t *basl_value_as_object(const basl_value_t *value) {
    if (value == NULL || value->kind != BASL_VALUE_OBJECT) {
        return NULL;
    }

    return value->as.object;
}

basl_object_type_t basl_object_type(const basl_object_t *object) {
    if (object == NULL) {
        return BASL_OBJECT_INVALID;
    }

    return object->type;
}

size_t basl_object_ref_count(const basl_object_t *object) {
    if (object == NULL) {
        return 0U;
    }

    return object->ref_count;
}

void basl_object_retain(basl_object_t *object) {
    if (object == NULL) {
        return;
    }

    if (object->ref_count != SIZE_MAX) {
        object->ref_count += 1U;
    }
}

void basl_object_release(basl_object_t **object) {
    basl_object_t *resolved_object;

    if (object == NULL || *object == NULL) {
        return;
    }

    resolved_object = *object;
    *object = NULL;

    if (resolved_object->ref_count > 1U) {
        resolved_object->ref_count -= 1U;
        return;
    }

    basl_object_destroy(resolved_object);
}

basl_status_t basl_string_object_new(
    basl_runtime_t *runtime,
    const char *value,
    size_t length,
    basl_object_t **out_object,
    basl_error_t *error
) {
    basl_status_t status;
    basl_string_object_t *object;
    void *memory;

    basl_error_clear(error);

    if (runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string object value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (out_object == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "out_object must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = basl_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    object = (basl_string_object_t *)memory;
    basl_object_init(&object->base, runtime, BASL_OBJECT_STRING);
    basl_string_init(&object->value, runtime);
    status = basl_string_assign(&object->value, value, length, error);
    if (status != BASL_STATUS_OK) {
        basl_object_destroy(&object->base);
        return status;
    }

    *out_object = &object->base;
    return BASL_STATUS_OK;
}

basl_status_t basl_string_object_new_cstr(
    basl_runtime_t *runtime,
    const char *value,
    basl_object_t **out_object,
    basl_error_t *error
) {
    if (value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "string object value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return basl_string_object_new(
        runtime,
        value,
        strlen(value),
        out_object,
        error
    );
}

const char *basl_string_object_c_str(const basl_object_t *object) {
    basl_string_object_t *string_object;

    string_object = basl_string_object_cast(object);
    if (string_object == NULL) {
        return "";
    }

    return basl_string_c_str(&string_object->value);
}

size_t basl_string_object_length(const basl_object_t *object) {
    basl_string_object_t *string_object;

    string_object = basl_string_object_cast(object);
    if (string_object == NULL) {
        return 0U;
    }

    return basl_string_length(&string_object->value);
}
