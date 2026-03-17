/* BASL standard library: yaml module.
 *
 * Provides YAML parsing with output as JSON strings.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/yaml.h"
#include "basl/json.h"
#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"
#include "basl/runtime.h"

#include "internal/basl_nanbox.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool get_string_arg(basl_vm_t *vm, size_t base, size_t idx,
                           const char **str, size_t *len) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    if (!basl_nanbox_is_object(v)) return false;
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(v);
    if (!obj || basl_object_type(obj) != BASL_OBJECT_STRING) return false;
    *str = basl_string_object_c_str(obj);
    *len = basl_string_object_length(obj);
    return true;
}

static basl_status_t push_string(basl_vm_t *vm, const char *str, size_t len,
                                  basl_error_t *error) {
    basl_object_t *obj = NULL;
    basl_status_t s = basl_string_object_new(basl_vm_runtime(vm), str, len, &obj, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_t val;
    basl_value_init_object(&val, &obj);
    s = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return s;
}

/* ── yaml.parse(yaml: string) -> string ──────────────────────────── */

static basl_status_t basl_yaml_parse_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *yaml_str;
    size_t yaml_len;
    basl_json_value_t *json = NULL;
    char *json_str = NULL;
    size_t json_len = 0;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    s = basl_yaml_parse(yaml_str, yaml_len, NULL, &json, error);
    if (s != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    s = basl_json_emit(json, &json_str, &json_len, error);
    basl_json_free(&json);
    
    if (s != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    basl_vm_stack_pop_n(vm, arg_count);
    s = push_string(vm, json_str, json_len, error);
    free(json_str);
    return s;
}

/* ── yaml.get(yaml: string, path: string) -> string ──────────────── */

static basl_status_t basl_yaml_get_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *yaml_str, *path_str;
    size_t yaml_len, path_len;
    basl_json_value_t *json = NULL;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &yaml_str, &yaml_len) ||
        !get_string_arg(vm, base, 1, &path_str, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    s = basl_yaml_parse(yaml_str, yaml_len, NULL, &json, error);
    if (s != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    /* Navigate path (dot-separated keys, brackets for array indices) */
    const basl_json_value_t *current = json;
    char *path_copy = malloc(path_len + 1);
    if (!path_copy) {
        basl_json_free(&json);
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    memcpy(path_copy, path_str, path_len);
    path_copy[path_len] = '\0';

    char *tok = path_copy;
    while (*tok && current) {
        /* Skip leading dots */
        while (*tok == '.') tok++;
        if (!*tok) break;

        if (*tok == '[') {
            /* Array index */
            tok++;
            size_t idx = (size_t)strtoul(tok, &tok, 10);
            if (*tok == ']') tok++;
            if (basl_json_type(current) == BASL_JSON_ARRAY) {
                current = basl_json_array_get(current, idx);
            } else {
                current = NULL;
            }
        } else {
            /* Object key */
            char *end = tok;
            while (*end && *end != '.' && *end != '[') end++;
            char saved = *end;
            *end = '\0';
            
            if (basl_json_type(current) == BASL_JSON_OBJECT) {
                current = basl_json_object_get(current, tok);
            } else {
                current = NULL;
            }
            
            *end = saved;
            tok = end;
        }
    }
    free(path_copy);

    basl_vm_stack_pop_n(vm, arg_count);

    if (!current) {
        basl_json_free(&json);
        return push_string(vm, "", 0, error);
    }

    /* Return value as string */
    char *result_str = NULL;
    size_t result_len = 0;
    
    switch (basl_json_type(current)) {
        case BASL_JSON_STRING:
            s = push_string(vm, basl_json_string_value(current),
                           basl_json_string_length(current), error);
            break;
        case BASL_JSON_NUMBER: {
            char buf[64];
            int n = snprintf(buf, sizeof(buf), "%g", basl_json_number_value(current));
            s = push_string(vm, buf, (size_t)n, error);
            break;
        }
        case BASL_JSON_BOOL:
            s = push_string(vm, basl_json_bool_value(current) ? "true" : "false",
                           basl_json_bool_value(current) ? 4 : 5, error);
            break;
        case BASL_JSON_NULL:
            s = push_string(vm, "null", 4, error);
            break;
        default:
            /* For arrays/objects, stringify */
            s = basl_json_emit(current, &result_str, &result_len, error);
            if (s == BASL_STATUS_OK) {
                s = push_string(vm, result_str, result_len, error);
                free(result_str);
            }
            break;
    }

    basl_json_free(&json);
    return s;
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_param[] = {BASL_TYPE_STRING};
static const int str_str_params[] = {BASL_TYPE_STRING, BASL_TYPE_STRING};

static const basl_native_module_function_t basl_yaml_functions[] = {
    {"parse", 5U, basl_yaml_parse_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"get", 3U, basl_yaml_get_fn, 2U, str_str_params, BASL_TYPE_STRING, 1U, NULL, 0},
};

#define YAML_FUNCTION_COUNT \
    (sizeof(basl_yaml_functions) / sizeof(basl_yaml_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_yaml = {
    "yaml", 4U,
    basl_yaml_functions,
    YAML_FUNCTION_COUNT,
    NULL, 0U
};
