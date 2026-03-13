#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "basl/chunk.h"
#include "internal/basl_internal.h"
#include "basl/map.h"
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

typedef struct basl_error_object {
    basl_object_t base;
    basl_string_t message;
    int64_t kind;
} basl_error_object_t;

typedef struct basl_function_object {
    basl_object_t base;
    basl_string_t name;
    size_t arity;
    size_t return_count;
    basl_chunk_t chunk;
    basl_object_t **functions;
    size_t function_count;
    size_t function_index;
    int owns_function_table;
    basl_value_t *globals;
    size_t global_count;
    int owns_global_table;
    struct basl_runtime_class *classes;
    size_t class_count;
    int owns_class_table;
} basl_function_object_t;

typedef struct basl_closure_object {
    basl_object_t base;
    basl_object_t *function;
    basl_value_t *captures;
    size_t capture_count;
} basl_closure_object_t;

typedef struct basl_runtime_interface_impl {
    size_t interface_index;
    size_t *function_indices;
    size_t function_count;
} basl_runtime_interface_impl_t;

typedef struct basl_runtime_class {
    basl_runtime_interface_impl_t *interface_impls;
    size_t interface_impl_count;
} basl_runtime_class_t;

typedef struct basl_instance_object {
    basl_object_t base;
    size_t class_index;
    basl_value_t *fields;
    size_t field_count;
} basl_instance_object_t;

typedef struct basl_array_object {
    basl_object_t base;
    basl_value_t *items;
    size_t item_count;
} basl_array_object_t;

typedef struct basl_map_object {
    basl_object_t base;
    basl_map_t entries;
} basl_map_object_t;

static const basl_string_object_t *basl_string_object_cast(
    const basl_object_t *object
) {
    if (object == NULL || object->type != BASL_OBJECT_STRING) {
        return NULL;
    }

    return (const basl_string_object_t *)object;
}

static const basl_function_object_t *basl_function_object_cast(
    const basl_object_t *object
) {
    if (object == NULL || object->type != BASL_OBJECT_FUNCTION) {
        return NULL;
    }

    return (const basl_function_object_t *)object;
}

static const basl_closure_object_t *basl_closure_object_cast(
    const basl_object_t *object
) {
    if (object == NULL || object->type != BASL_OBJECT_CLOSURE) {
        return NULL;
    }

    return (const basl_closure_object_t *)object;
}

static const basl_error_object_t *basl_error_object_cast(
    const basl_object_t *object
) {
    if (object == NULL || object->type != BASL_OBJECT_ERROR) {
        return NULL;
    }

    return (const basl_error_object_t *)object;
}

static const basl_instance_object_t *basl_instance_object_cast(
    const basl_object_t *object
) {
    if (object == NULL || object->type != BASL_OBJECT_INSTANCE) {
        return NULL;
    }

    return (const basl_instance_object_t *)object;
}

static const basl_array_object_t *basl_array_object_cast(
    const basl_object_t *object
) {
    if (object == NULL || object->type != BASL_OBJECT_ARRAY) {
        return NULL;
    }

    return (const basl_array_object_t *)object;
}

static const basl_map_object_t *basl_map_object_cast(
    const basl_object_t *object
) {
    if (object == NULL || object->type != BASL_OBJECT_MAP) {
        return NULL;
    }

    return (const basl_map_object_t *)object;
}

static void basl_object_destroy(basl_object_t *object) {
    basl_runtime_t *runtime;
    void *memory;
    basl_string_object_t *string_object;
    basl_function_object_t *function_object;
    basl_closure_object_t *closure_object;
    basl_instance_object_t *instance_object;
    basl_array_object_t *array_object;

    if (object == NULL) {
        return;
    }

    runtime = object->runtime;
    switch (object->type) {
        case BASL_OBJECT_STRING:
            string_object = (basl_string_object_t *)object;
            basl_string_free(&string_object->value);
            break;
        case BASL_OBJECT_ERROR:
            basl_string_free(&((basl_error_object_t *)object)->message);
            break;
        case BASL_OBJECT_FUNCTION:
            function_object = (basl_function_object_t *)object;
            basl_string_free(&function_object->name);
            basl_chunk_free(&function_object->chunk);
            if (function_object->owns_class_table && function_object->classes != NULL) {
                size_t class_index;

                for (class_index = 0U; class_index < function_object->class_count; ++class_index) {
                    size_t impl_index;

                    for (
                        impl_index = 0U;
                        impl_index < function_object->classes[class_index].interface_impl_count;
                        ++impl_index
                    ) {
                        memory =
                            function_object->classes[class_index]
                                .interface_impls[impl_index]
                                .function_indices;
                        basl_runtime_free(runtime, &memory);
                    }

                    memory = function_object->classes[class_index].interface_impls;
                    basl_runtime_free(runtime, &memory);
                }

                memory = function_object->classes;
                basl_runtime_free(runtime, &memory);
            }
            if (function_object->owns_function_table && function_object->functions != NULL) {
                size_t i;
                void *table_memory;

                for (i = 0U; i < function_object->function_count; ++i) {
                    if (i == function_object->function_index) {
                        continue;
                    }

                    basl_object_release(&function_object->functions[i]);
                }

                table_memory = function_object->functions;
                basl_runtime_free(runtime, &table_memory);
            }
            if (function_object->owns_global_table && function_object->globals != NULL) {
                size_t global_index;

                for (global_index = 0U; global_index < function_object->global_count; ++global_index) {
                    basl_value_release(&function_object->globals[global_index]);
                }

                memory = function_object->globals;
                basl_runtime_free(runtime, &memory);
            }
            break;
        case BASL_OBJECT_CLOSURE:
            closure_object = (basl_closure_object_t *)object;
            if (closure_object->function != NULL) {
                basl_object_release(&closure_object->function);
            }
            if (closure_object->captures != NULL) {
                size_t capture_index;

                for (capture_index = 0U; capture_index < closure_object->capture_count; ++capture_index) {
                    basl_value_release(&closure_object->captures[capture_index]);
                }

                memory = closure_object->captures;
                basl_runtime_free(runtime, &memory);
            }
            break;
        case BASL_OBJECT_INSTANCE:
            instance_object = (basl_instance_object_t *)object;
            if (instance_object->fields != NULL) {
                size_t i;

                for (i = 0U; i < instance_object->field_count; ++i) {
                    basl_value_release(&instance_object->fields[i]);
                }

                memory = instance_object->fields;
                basl_runtime_free(runtime, &memory);
            }
            break;
        case BASL_OBJECT_ARRAY:
            array_object = (basl_array_object_t *)object;
            if (array_object->items != NULL) {
                size_t i;

                for (i = 0U; i < array_object->item_count; ++i) {
                    basl_value_release(&array_object->items[i]);
                }

                memory = array_object->items;
                basl_runtime_free(runtime, &memory);
            }
            break;
        case BASL_OBJECT_MAP:
            basl_map_free(&((basl_map_object_t *)object)->entries);
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

    if (object->ref_count == SIZE_MAX) {
        abort();
    }

    object->ref_count += 1U;
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

basl_status_t basl_error_object_new(
    basl_runtime_t *runtime,
    const char *message,
    size_t length,
    int64_t kind,
    basl_object_t **out_object,
    basl_error_t *error
) {
    basl_status_t status;
    basl_error_object_t *object;
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
    if (message == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "error object message must not be null"
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

    object = (basl_error_object_t *)memory;
    memset(object, 0, sizeof(*object));
    basl_object_init(&object->base, runtime, BASL_OBJECT_ERROR);
    basl_string_init(&object->message, runtime);
    status = basl_string_assign(&object->message, message, length, error);
    if (status != BASL_STATUS_OK) {
        basl_object_t *base = &object->base;

        basl_object_release(&base);
        return status;
    }
    object->kind = kind;
    *out_object = &object->base;
    return BASL_STATUS_OK;
}

basl_status_t basl_error_object_new_cstr(
    basl_runtime_t *runtime,
    const char *message,
    int64_t kind,
    basl_object_t **out_object,
    basl_error_t *error
) {
    if (message == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "error object message must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return basl_error_object_new(runtime, message, strlen(message), kind, out_object, error);
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
    const basl_string_object_t *string_object;

    string_object = basl_string_object_cast(object);
    if (string_object == NULL) {
        return "";
    }

    return basl_string_c_str(&string_object->value);
}

size_t basl_string_object_length(const basl_object_t *object) {
    const basl_string_object_t *string_object;

    string_object = basl_string_object_cast(object);
    if (string_object == NULL) {
        return 0U;
    }

    return basl_string_length(&string_object->value);
}

const char *basl_error_object_message(const basl_object_t *object) {
    const basl_error_object_t *error_object = basl_error_object_cast(object);

    if (error_object == NULL) {
        return NULL;
    }

    return basl_string_c_str(&error_object->message);
}

size_t basl_error_object_message_length(const basl_object_t *object) {
    const basl_error_object_t *error_object = basl_error_object_cast(object);

    if (error_object == NULL) {
        return 0U;
    }

    return basl_string_length(&error_object->message);
}

int64_t basl_error_object_kind(const basl_object_t *object) {
    const basl_error_object_t *error_object = basl_error_object_cast(object);

    if (error_object == NULL) {
        return 0;
    }

    return error_object->kind;
}

basl_status_t basl_function_object_new(
    basl_runtime_t *runtime,
    const char *name,
    size_t name_length,
    size_t arity,
    size_t return_count,
    basl_chunk_t *chunk,
    basl_object_t **out_object,
    basl_error_t *error
) {
    basl_status_t status;
    basl_function_object_t *object;
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

    if (name == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "function object name must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "function object chunk must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (chunk->runtime != runtime) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "function object chunk runtime must match runtime"
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

    object = (basl_function_object_t *)memory;
    basl_object_init(&object->base, runtime, BASL_OBJECT_FUNCTION);
    basl_string_init(&object->name, runtime);
    object->arity = arity;
    object->return_count = return_count;
    object->functions = NULL;
    object->function_count = 0U;
    object->function_index = 0U;
    object->owns_function_table = 0;
    object->globals = NULL;
    object->global_count = 0U;
    object->owns_global_table = 0;
    object->classes = NULL;
    object->class_count = 0U;
    object->owns_class_table = 0;
    status = basl_string_assign(&object->name, name, name_length, error);
    if (status != BASL_STATUS_OK) {
        basl_object_destroy(&object->base);
        return status;
    }

    object->chunk = *chunk;
    memset(chunk, 0, sizeof(*chunk));
    *out_object = &object->base;
    return BASL_STATUS_OK;
}

basl_status_t basl_function_object_new_cstr(
    basl_runtime_t *runtime,
    const char *name,
    size_t arity,
    size_t return_count,
    basl_chunk_t *chunk,
    basl_object_t **out_object,
    basl_error_t *error
) {
    if (name == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "function object name must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return basl_function_object_new(
        runtime,
        name,
        strlen(name),
        arity,
        return_count,
        chunk,
        out_object,
        error
    );
}

const char *basl_function_object_name(const basl_object_t *object) {
    const basl_function_object_t *function_object;

    function_object = basl_function_object_cast(object);
    if (function_object == NULL) {
        return "";
    }

    return basl_string_c_str(&function_object->name);
}

size_t basl_function_object_arity(const basl_object_t *object) {
    const basl_function_object_t *function_object;

    function_object = basl_function_object_cast(object);
    if (function_object == NULL) {
        return 0U;
    }

    return function_object->arity;
}

size_t basl_function_object_return_count(const basl_object_t *object) {
    const basl_function_object_t *function_object;

    function_object = basl_function_object_cast(object);
    if (function_object == NULL) {
        return 0U;
    }

    return function_object->return_count;
}

const basl_chunk_t *basl_function_object_chunk(const basl_object_t *object) {
    const basl_function_object_t *function_object;

    function_object = basl_function_object_cast(object);
    if (function_object == NULL) {
        return NULL;
    }

    return &function_object->chunk;
}

basl_status_t basl_closure_object_new(
    basl_runtime_t *runtime,
    basl_object_t *function,
    const basl_value_t *captures,
    size_t capture_count,
    basl_object_t **out_object,
    basl_error_t *error
) {
    basl_status_t status;
    basl_closure_object_t *object;
    void *memory;
    size_t capture_index;

    basl_error_clear(error);
    if (runtime == NULL || function == NULL || out_object == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "closure object arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (basl_object_type(function) != BASL_OBJECT_FUNCTION) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "closure function must be a function object"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (capture_count != 0U && captures == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "closure captures must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = basl_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    object = (basl_closure_object_t *)memory;
    memset(object, 0, sizeof(*object));
    basl_object_init(&object->base, runtime, BASL_OBJECT_CLOSURE);
    basl_object_retain(function);
    object->function = function;
    object->capture_count = capture_count;

    if (capture_count != 0U) {
        memory = NULL;
        status = basl_runtime_alloc(runtime, capture_count * sizeof(*object->captures), &memory, error);
        if (status != BASL_STATUS_OK) {
            basl_object_t *base = &object->base;

            basl_object_release(&base);
            return status;
        }

        object->captures = (basl_value_t *)memory;
        for (capture_index = 0U; capture_index < capture_count; ++capture_index) {
            object->captures[capture_index] = basl_value_copy(&captures[capture_index]);
        }
    }

    *out_object = &object->base;
    return BASL_STATUS_OK;
}

const basl_object_t *basl_closure_object_function(const basl_object_t *object) {
    const basl_closure_object_t *closure_object = basl_closure_object_cast(object);

    if (closure_object == NULL) {
        return NULL;
    }

    return closure_object->function;
}

size_t basl_closure_object_capture_count(const basl_object_t *object) {
    const basl_closure_object_t *closure_object = basl_closure_object_cast(object);

    if (closure_object == NULL) {
        return 0U;
    }

    return closure_object->capture_count;
}

int basl_closure_object_get_capture(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_value
) {
    const basl_closure_object_t *closure_object = basl_closure_object_cast(object);

    if (closure_object == NULL || out_value == NULL || index >= closure_object->capture_count) {
        return 0;
    }

    *out_value = basl_value_copy(&closure_object->captures[index]);
    return 1;
}

basl_status_t basl_closure_object_set_capture(
    basl_object_t *object,
    size_t index,
    const basl_value_t *value,
    basl_error_t *error
) {
    basl_closure_object_t *closure_object = (basl_closure_object_t *)object;
    basl_value_t copy;

    basl_error_clear(error);
    if (
        closure_object == NULL ||
        closure_object->base.type != BASL_OBJECT_CLOSURE ||
        value == NULL ||
        index >= closure_object->capture_count
    ) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "closure capture arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    copy = basl_value_copy(value);
    basl_value_release(&closure_object->captures[index]);
    closure_object->captures[index] = copy;
    return BASL_STATUS_OK;
}

basl_status_t basl_instance_object_new(
    basl_runtime_t *runtime,
    size_t class_index,
    const basl_value_t *fields,
    size_t field_count,
    basl_object_t **out_object,
    basl_error_t *error
) {
    basl_status_t status;
    basl_instance_object_t *object;
    void *memory;
    size_t i;

    basl_error_clear(error);

    if (runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (field_count != 0U && fields == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "instance object fields must not be null"
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

    object = (basl_instance_object_t *)memory;
    basl_object_init(&object->base, runtime, BASL_OBJECT_INSTANCE);
    object->class_index = class_index;
    object->fields = NULL;
    object->field_count = field_count;
    if (field_count != 0U) {
        memory = NULL;
        status = basl_runtime_alloc(
            runtime,
            field_count * sizeof(*object->fields),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) {
            basl_object_destroy(&object->base);
            return status;
        }

        object->fields = (basl_value_t *)memory;
        for (i = 0U; i < field_count; ++i) {
            object->fields[i] = basl_value_copy(&fields[i]);
        }
    }

    *out_object = &object->base;
    return BASL_STATUS_OK;
}

size_t basl_instance_object_class_index(const basl_object_t *object) {
    const basl_instance_object_t *instance_object;

    instance_object = basl_instance_object_cast(object);
    if (instance_object == NULL) {
        return 0U;
    }

    return instance_object->class_index;
}

size_t basl_instance_object_field_count(const basl_object_t *object) {
    const basl_instance_object_t *instance_object;

    instance_object = basl_instance_object_cast(object);
    if (instance_object == NULL) {
        return 0U;
    }

    return instance_object->field_count;
}

int basl_instance_object_get_field(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_value
) {
    const basl_instance_object_t *instance_object;

    instance_object = basl_instance_object_cast(object);
    if (instance_object == NULL || out_value == NULL || index >= instance_object->field_count) {
        return 0;
    }

    *out_value = basl_value_copy(&instance_object->fields[index]);
    return 1;
}

basl_status_t basl_instance_object_set_field(
    basl_object_t *object,
    size_t index,
    const basl_value_t *value,
    basl_error_t *error
) {
    const basl_instance_object_t *instance_object;
    basl_value_t copy;

    basl_error_clear(error);
    instance_object = basl_instance_object_cast(object);
    if (instance_object == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "object must be an instance object"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "field value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (index >= instance_object->field_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "field index is out of range"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    copy = basl_value_copy(value);
    basl_value_release(&((basl_instance_object_t *)object)->fields[index]);
    ((basl_instance_object_t *)object)->fields[index] = copy;
    return BASL_STATUS_OK;
}

basl_status_t basl_array_object_new(
    basl_runtime_t *runtime,
    const basl_value_t *items,
    size_t item_count,
    basl_object_t **out_object,
    basl_error_t *error
) {
    basl_status_t status;
    basl_array_object_t *object;
    void *memory;
    size_t index;

    basl_error_clear(error);
    if (runtime == NULL || out_object == NULL || (item_count != 0U && items == NULL)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "array object arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = basl_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    object = (basl_array_object_t *)memory;
    memset(object, 0, sizeof(*object));
    basl_object_init(&object->base, runtime, BASL_OBJECT_ARRAY);
    if (item_count != 0U) {
        memory = NULL;
        status = basl_runtime_alloc(
            runtime,
            item_count * sizeof(*object->items),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) {
            basl_object_destroy(&object->base);
            return status;
        }

        object->items = (basl_value_t *)memory;
        object->item_count = item_count;
        for (index = 0U; index < item_count; ++index) {
            object->items[index] = basl_value_copy(&items[index]);
        }
    }

    *out_object = &object->base;
    return BASL_STATUS_OK;
}

size_t basl_array_object_length(const basl_object_t *object) {
    const basl_array_object_t *array_object;

    array_object = basl_array_object_cast(object);
    if (array_object == NULL) {
        return 0U;
    }

    return array_object->item_count;
}

int basl_array_object_get(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_value
) {
    const basl_array_object_t *array_object;

    array_object = basl_array_object_cast(object);
    if (array_object == NULL || out_value == NULL || index >= array_object->item_count) {
        return 0;
    }

    *out_value = basl_value_copy(&array_object->items[index]);
    return 1;
}

basl_status_t basl_array_object_set(
    basl_object_t *object,
    size_t index,
    const basl_value_t *value,
    basl_error_t *error
) {
    basl_array_object_t *array_object;
    basl_value_t copy;

    basl_error_clear(error);
    array_object = (basl_array_object_t *)object;
    if (array_object == NULL || array_object->base.type != BASL_OBJECT_ARRAY || value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "array object set arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (index >= array_object->item_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "array index is out of range"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    copy = basl_value_copy(value);
    basl_value_release(&array_object->items[index]);
    array_object->items[index] = copy;
    return BASL_STATUS_OK;
}

basl_status_t basl_map_object_new(
    basl_runtime_t *runtime,
    basl_object_t **out_object,
    basl_error_t *error
) {
    basl_status_t status;
    basl_map_object_t *object;
    void *memory;

    basl_error_clear(error);
    if (runtime == NULL || out_object == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map object arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_object = NULL;
    memory = NULL;
    status = basl_runtime_alloc(runtime, sizeof(*object), &memory, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    object = (basl_map_object_t *)memory;
    memset(object, 0, sizeof(*object));
    basl_object_init(&object->base, runtime, BASL_OBJECT_MAP);
    basl_map_init(&object->entries, runtime);
    *out_object = &object->base;
    return BASL_STATUS_OK;
}

size_t basl_map_object_count(const basl_object_t *object) {
    const basl_map_object_t *map_object;

    map_object = basl_map_object_cast(object);
    if (map_object == NULL) {
        return 0U;
    }

    return basl_map_count(&map_object->entries);
}

int basl_map_object_get(
    const basl_object_t *object,
    const basl_value_t *key,
    basl_value_t *out_value
) {
    const basl_map_object_t *map_object;
    const basl_value_t *stored;

    map_object = basl_map_object_cast(object);
    if (map_object == NULL || key == NULL || out_value == NULL) {
        return 0;
    }

    stored = basl_map_get_value(&map_object->entries, key);
    if (stored == NULL) {
        return 0;
    }

    *out_value = basl_value_copy(stored);
    return 1;
}

int basl_map_object_key_at(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_key
) {
    const basl_map_object_t *map_object;
    const basl_value_t *stored_key;
    const basl_value_t *stored_value;

    if (out_key == NULL) {
        return 0;
    }
    basl_value_init_nil(out_key);
    map_object = basl_map_object_cast(object);
    if (map_object == NULL) {
        return 0;
    }

    stored_key = NULL;
    stored_value = NULL;
    if (!basl_map_entry_value_at(&map_object->entries, index, &stored_key, &stored_value)) {
        return 0;
    }

    *out_key = basl_value_copy(stored_key);
    return 1;
}

int basl_map_object_value_at(
    const basl_object_t *object,
    size_t index,
    basl_value_t *out_value
) {
    const basl_map_object_t *map_object;
    const basl_value_t *stored_key;
    const basl_value_t *stored_value;

    map_object = basl_map_object_cast(object);
    if (map_object == NULL || out_value == NULL) {
        return 0;
    }

    stored_key = NULL;
    stored_value = NULL;
    if (!basl_map_entry_value_at(&map_object->entries, index, &stored_key, &stored_value)) {
        return 0;
    }

    *out_value = basl_value_copy(stored_value);
    return 1;
}

basl_status_t basl_map_object_set(
    basl_object_t *object,
    const basl_value_t *key,
    const basl_value_t *value,
    basl_error_t *error
) {
    basl_map_object_t *map_object;

    basl_error_clear(error);
    map_object = (basl_map_object_t *)object;
    if (map_object == NULL || map_object->base.type != BASL_OBJECT_MAP || key == NULL || value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map object set arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return basl_map_set_value(&map_object->entries, key, value, error);
}

basl_status_t basl_function_object_attach_siblings(
    basl_object_t *owner_function,
    basl_object_t **functions,
    size_t function_count,
    size_t owner_index,
    const basl_value_t *initial_globals,
    size_t global_count,
    const basl_runtime_class_init_t *classes_init,
    size_t class_count,
    basl_error_t *error
) {
    size_t i;
    basl_function_object_t *owner;
    basl_function_object_t *function_object;
    basl_runtime_t *runtime;
    basl_runtime_class_t *classes;
    basl_value_t *globals;
    void *memory;
    int invalid_function_table;

    basl_error_clear(error);
    owner = (basl_function_object_t *)owner_function;
    if (owner == NULL || owner->base.type != BASL_OBJECT_FUNCTION) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "owner_function must be a function object"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (functions == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "function table must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (function_count == 0U || owner_index >= function_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "function table bounds are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    runtime = owner->base.runtime;
    classes = NULL;
    globals = NULL;
    invalid_function_table = 0;
    if (class_count != 0U && classes_init == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "class metadata must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (global_count != 0U) {
        size_t global_index;

        memory = NULL;
        if (
            basl_runtime_alloc(runtime, global_count * sizeof(*globals), &memory, error) !=
            BASL_STATUS_OK
        ) {
            return BASL_STATUS_OUT_OF_MEMORY;
        }
        globals = (basl_value_t *)memory;
        for (global_index = 0U; global_index < global_count; ++global_index) {
            if (initial_globals != NULL) {
                globals[global_index] = basl_value_copy(&initial_globals[global_index]);
            } else {
                basl_value_init_nil(&globals[global_index]);
            }
        }
    }

    if (class_count != 0U) {
        size_t class_index;

        memory = NULL;
        if (basl_runtime_alloc(runtime, class_count * sizeof(*classes), &memory, error) !=
            BASL_STATUS_OK) {
            return BASL_STATUS_OUT_OF_MEMORY;
        }
        classes = (basl_runtime_class_t *)memory;
        memset(classes, 0, class_count * sizeof(*classes));

        for (class_index = 0U; class_index < class_count; ++class_index) {
            size_t interface_count = classes_init[class_index].interface_impl_count;

            classes[class_index].interface_impl_count = interface_count;
            if (interface_count == 0U) {
                continue;
            }

            memory = NULL;
            if (
                basl_runtime_alloc(
                    runtime,
                    interface_count * sizeof(*classes[class_index].interface_impls),
                    &memory,
                    error
                ) != BASL_STATUS_OK
            ) {
                goto cleanup_classes;
            }
            classes[class_index].interface_impls = (basl_runtime_interface_impl_t *)memory;
            memset(
                classes[class_index].interface_impls,
                0,
                interface_count * sizeof(*classes[class_index].interface_impls)
            );

            for (i = 0U; i < interface_count; ++i) {
                size_t method_count = classes_init[class_index].interface_impls[i].function_count;

                classes[class_index].interface_impls[i].interface_index =
                    classes_init[class_index].interface_impls[i].interface_index;
                classes[class_index].interface_impls[i].function_count = method_count;
                if (method_count == 0U) {
                    continue;
                }

                memory = NULL;
                if (
                    basl_runtime_alloc(
                        runtime,
                        method_count *
                            sizeof(*classes[class_index].interface_impls[i].function_indices),
                        &memory,
                        error
                    ) != BASL_STATUS_OK
                ) {
                    goto cleanup_classes;
                }
                classes[class_index].interface_impls[i].function_indices = (size_t *)memory;
                memcpy(
                    classes[class_index].interface_impls[i].function_indices,
                    classes_init[class_index].interface_impls[i].function_indices,
                    method_count *
                        sizeof(*classes[class_index].interface_impls[i].function_indices)
                );
            }
        }
    }

    for (i = 0U; i < function_count; ++i) {
        function_object = (basl_function_object_t *)functions[i];
        if (function_object == NULL || function_object->base.type != BASL_OBJECT_FUNCTION) {
            invalid_function_table = 1;
            goto cleanup_classes;
        }

        function_object->functions = functions;
        function_object->function_count = function_count;
        function_object->function_index = i;
        function_object->owns_function_table = 0;
        function_object->globals = globals;
        function_object->global_count = global_count;
        function_object->owns_global_table = 0;
        function_object->classes = classes;
        function_object->class_count = class_count;
        function_object->owns_class_table = 0;
    }

    owner->owns_function_table = 1;
    owner->owns_global_table = 1;
    owner->owns_class_table = 1;
    return BASL_STATUS_OK;

cleanup_classes:
    if (globals != NULL) {
        size_t global_index;

        for (global_index = 0U; global_index < global_count; ++global_index) {
            basl_value_release(&globals[global_index]);
        }

        memory = globals;
        basl_runtime_free(runtime, &memory);
    }
    if (classes != NULL) {
        size_t class_index;

        for (class_index = 0U; class_index < class_count; ++class_index) {
            size_t impl_index;

            for (
                impl_index = 0U;
                impl_index < classes[class_index].interface_impl_count;
                ++impl_index
            ) {
                memory = classes[class_index].interface_impls[impl_index].function_indices;
                basl_runtime_free(runtime, &memory);
            }

            memory = classes[class_index].interface_impls;
            basl_runtime_free(runtime, &memory);
        }

        memory = classes;
        basl_runtime_free(runtime, &memory);
    }

    if (invalid_function_table) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "function table entries must all be function objects"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    return BASL_STATUS_OUT_OF_MEMORY;
}

const basl_object_t *basl_function_object_sibling(
    const basl_object_t *function,
    size_t index
) {
    const basl_function_object_t *function_object;

    function_object = basl_function_object_cast(function);
    if (function_object == NULL || function_object->functions == NULL) {
        return NULL;
    }
    if (index >= function_object->function_count) {
        return NULL;
    }

    return function_object->functions[index];
}

const basl_object_t *basl_function_object_resolve_interface_method(
    const basl_object_t *function,
    size_t class_index,
    size_t interface_index,
    size_t method_index
) {
    const basl_function_object_t *function_object;
    const basl_runtime_class_t *class_metadata;
    size_t impl_index;
    size_t function_index;

    function_object = basl_function_object_cast(function);
    if (function_object == NULL || function_object->classes == NULL) {
        return NULL;
    }
    if (class_index >= function_object->class_count) {
        return NULL;
    }

    class_metadata = &function_object->classes[class_index];
    for (impl_index = 0U; impl_index < class_metadata->interface_impl_count; ++impl_index) {
        if (class_metadata->interface_impls[impl_index].interface_index != interface_index) {
            continue;
        }
        if (method_index >= class_metadata->interface_impls[impl_index].function_count) {
            return NULL;
        }

        function_index = class_metadata->interface_impls[impl_index].function_indices[method_index];
        return basl_function_object_sibling(function, function_index);
    }

    return NULL;
}

int basl_function_object_get_global(
    const basl_object_t *function,
    size_t index,
    basl_value_t *out_value
) {
    const basl_function_object_t *function_object;

    function_object = basl_function_object_cast(function);
    if (
        function_object == NULL ||
        out_value == NULL ||
        function_object->globals == NULL ||
        index >= function_object->global_count
    ) {
        return 0;
    }

    *out_value = basl_value_copy(&function_object->globals[index]);
    return 1;
}

basl_status_t basl_function_object_set_global(
    const basl_object_t *function,
    size_t index,
    const basl_value_t *value,
    basl_error_t *error
) {
    const basl_function_object_t *function_object;
    basl_value_t copy;

    basl_error_clear(error);
    function_object = basl_function_object_cast(function);
    if (function_object == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "function must be a function object"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "global value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (function_object->globals == NULL || index >= function_object->global_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "global index is out of range"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    copy = basl_value_copy(value);
    basl_value_release(&((basl_function_object_t *)function)->globals[index]);
    ((basl_function_object_t *)function)->globals[index] = copy;
    return BASL_STATUS_OK;
}

const basl_object_t *basl_callable_object_function(const basl_object_t *callable) {
    if (callable == NULL) {
        return NULL;
    }
    if (basl_object_type(callable) == BASL_OBJECT_FUNCTION) {
        return callable;
    }
    if (basl_object_type(callable) == BASL_OBJECT_CLOSURE) {
        return basl_closure_object_function(callable);
    }

    return NULL;
}

size_t basl_callable_object_arity(const basl_object_t *callable) {
    const basl_object_t *function = basl_callable_object_function(callable);

    return basl_function_object_arity(function);
}

size_t basl_callable_object_return_count(const basl_object_t *callable) {
    const basl_object_t *function = basl_callable_object_function(callable);

    return basl_function_object_return_count(function);
}

const basl_chunk_t *basl_callable_object_chunk(const basl_object_t *callable) {
    const basl_object_t *function = basl_callable_object_function(callable);

    return basl_function_object_chunk(function);
}
