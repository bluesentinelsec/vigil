/* BASL standard library: regex module
 *
 * RE2-style regular expressions with linear time guarantees.
 */
#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"

#include "regex.h"

/* ── Helpers ────────────────────────────────────────────────── */

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

static basl_status_t push_bool(basl_vm_t *vm, bool b, basl_error_t *error) {
    basl_value_t val;
    basl_value_init_bool(&val, b);
    return basl_vm_stack_push(vm, &val, error);
}

/* ── regex.match(pattern, input) -> bool ────────────────────── */

static basl_status_t basl_regex_match_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input;
    size_t pattern_len, input_len;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) ||
        !get_string_arg(vm, base, 1, &input, &input_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, false, error);
    }

    char err_buf[128];
    basl_regex_t *re = basl_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, false, error);
    }

    bool matched = basl_regex_match(re, input, input_len, NULL);
    basl_regex_free(re);

    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, matched, error);
}

/* ── regex.find(pattern, input) -> (string, bool) ───────────── */

static basl_status_t basl_regex_find_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input;
    size_t pattern_len, input_len;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) ||
        !get_string_arg(vm, base, 1, &input, &input_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        s = push_string(vm, "", 0, error);
        if (s == BASL_STATUS_OK) s = push_bool(vm, false, error);
        return s;
    }

    char err_buf[128];
    basl_regex_t *re = basl_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re) {
        basl_vm_stack_pop_n(vm, arg_count);
        s = push_string(vm, "", 0, error);
        if (s == BASL_STATUS_OK) s = push_bool(vm, false, error);
        return s;
    }

    basl_regex_result_t result;
    bool found = basl_regex_find(re, input, input_len, &result);
    basl_regex_free(re);

    basl_vm_stack_pop_n(vm, arg_count);

    if (found && result.groups[0].start != SIZE_MAX) {
        size_t start = result.groups[0].start;
        size_t end = result.groups[0].end;
        s = push_string(vm, input + start, end - start, error);
        if (s == BASL_STATUS_OK) s = push_bool(vm, true, error);
    } else {
        s = push_string(vm, "", 0, error);
        if (s == BASL_STATUS_OK) s = push_bool(vm, false, error);
    }
    return s;
}

/* ── regex.find_all(pattern, input) -> array<string> ────────── */

static basl_status_t basl_regex_find_all_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input;
    size_t pattern_len, input_len;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) ||
        !get_string_arg(vm, base, 1, &input, &input_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        /* Return empty array */
        basl_object_t *arr = NULL;
        s = basl_array_object_new(basl_vm_runtime(vm), NULL, 0, &arr, error);
        if (s != BASL_STATUS_OK) return s;
        basl_value_t val;
        basl_value_init_object(&val, &arr);
        s = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return s;
    }

    char err_buf[128];
    basl_regex_t *re = basl_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re) {
        basl_vm_stack_pop_n(vm, arg_count);
        basl_object_t *arr = NULL;
        s = basl_array_object_new(basl_vm_runtime(vm), NULL, 0, &arr, error);
        if (s != BASL_STATUS_OK) return s;
        basl_value_t val;
        basl_value_init_object(&val, &arr);
        s = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return s;
    }

    basl_regex_result_t results[256];
    size_t count = basl_regex_find_all(re, input, input_len, results, 256);
    basl_regex_free(re);

    /* Build array of match strings */
    basl_value_t *items = NULL;
    if (count > 0) {
        items = malloc(count * sizeof(basl_value_t));
        if (!items) {
            basl_vm_stack_pop_n(vm, arg_count);
            return BASL_STATUS_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < count; i++) {
            size_t start = results[i].groups[0].start;
            size_t end = results[i].groups[0].end;
            basl_object_t *str_obj = NULL;
            s = basl_string_object_new(basl_vm_runtime(vm), input + start, end - start, &str_obj, error);
            if (s != BASL_STATUS_OK) {
                for (size_t j = 0; j < i; j++) basl_value_release(&items[j]);
                free(items);
                basl_vm_stack_pop_n(vm, arg_count);
                return s;
            }
            basl_value_init_object(&items[i], &str_obj);
        }
    }

    basl_vm_stack_pop_n(vm, arg_count);

    basl_object_t *arr = NULL;
    s = basl_array_object_new(basl_vm_runtime(vm), items, count, &arr, error);
    for (size_t i = 0; i < count; i++) basl_value_release(&items[i]);
    free(items);
    if (s != BASL_STATUS_OK) return s;

    basl_value_t val;
    basl_value_init_object(&val, &arr);
    s = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return s;
}

/* ── regex.replace(pattern, input, replacement) -> string ───── */

static basl_status_t basl_regex_replace_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input, *replacement;
    size_t pattern_len, input_len, replacement_len;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) ||
        !get_string_arg(vm, base, 1, &input, &input_len) ||
        !get_string_arg(vm, base, 2, &replacement, &replacement_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    char err_buf[128];
    basl_regex_t *re = basl_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, input, input_len, error);
    }

    char *output = NULL;
    size_t output_len = 0;
    s = basl_regex_replace(re, input, input_len, replacement, replacement_len, &output, &output_len);
    basl_regex_free(re);

    basl_vm_stack_pop_n(vm, arg_count);

    if (s != BASL_STATUS_OK || !output) {
        return push_string(vm, input, input_len, error);
    }

    s = push_string(vm, output, output_len, error);
    free(output);
    return s;
}

/* ── regex.replace_all(pattern, input, replacement) -> string ─ */

static basl_status_t basl_regex_replace_all_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input, *replacement;
    size_t pattern_len, input_len, replacement_len;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) ||
        !get_string_arg(vm, base, 1, &input, &input_len) ||
        !get_string_arg(vm, base, 2, &replacement, &replacement_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    char err_buf[128];
    basl_regex_t *re = basl_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, input, input_len, error);
    }

    char *output = NULL;
    size_t output_len = 0;
    s = basl_regex_replace_all(re, input, input_len, replacement, replacement_len, &output, &output_len);
    basl_regex_free(re);

    basl_vm_stack_pop_n(vm, arg_count);

    if (s != BASL_STATUS_OK || !output) {
        return push_string(vm, input, input_len, error);
    }

    s = push_string(vm, output, output_len, error);
    free(output);
    return s;
}

/* ── regex.split(pattern, input) -> array<string> ───────────── */

static basl_status_t basl_regex_split_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input;
    size_t pattern_len, input_len;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) ||
        !get_string_arg(vm, base, 1, &input, &input_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        basl_object_t *arr = NULL;
        s = basl_array_object_new(basl_vm_runtime(vm), NULL, 0, &arr, error);
        if (s != BASL_STATUS_OK) return s;
        basl_value_t val;
        basl_value_init_object(&val, &arr);
        s = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return s;
    }

    char err_buf[128];
    basl_regex_t *re = basl_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re) {
        basl_vm_stack_pop_n(vm, arg_count);
        /* Return array with just the input */
        basl_object_t *str_obj = NULL;
        s = basl_string_object_new(basl_vm_runtime(vm), input, input_len, &str_obj, error);
        if (s != BASL_STATUS_OK) return s;
        basl_value_t item;
        basl_value_init_object(&item, &str_obj);
        basl_object_t *arr = NULL;
        s = basl_array_object_new(basl_vm_runtime(vm), &item, 1, &arr, error);
        basl_value_release(&item);
        if (s != BASL_STATUS_OK) return s;
        basl_value_t val;
        basl_value_init_object(&val, &arr);
        s = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return s;
    }

    char **parts = NULL;
    size_t *part_lens = NULL;
    size_t part_count = 0;
    s = basl_regex_split(re, input, input_len, &parts, &part_lens, &part_count);
    basl_regex_free(re);

    basl_vm_stack_pop_n(vm, arg_count);

    if (s != BASL_STATUS_OK) {
        return s;
    }

    /* Build array */
    basl_value_t *items = malloc(part_count * sizeof(basl_value_t));
    if (!items) {
        for (size_t i = 0; i < part_count; i++) free(parts[i]);
        free(parts);
        free(part_lens);
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < part_count; i++) {
        basl_object_t *str_obj = NULL;
        s = basl_string_object_new(basl_vm_runtime(vm), parts[i], part_lens[i], &str_obj, error);
        free(parts[i]);
        if (s != BASL_STATUS_OK) {
            for (size_t j = 0; j < i; j++) basl_value_release(&items[j]);
            for (size_t j = i + 1; j < part_count; j++) free(parts[j]);
            free(parts);
            free(part_lens);
            free(items);
            return s;
        }
        basl_value_init_object(&items[i], &str_obj);
    }
    free(parts);
    free(part_lens);

    basl_object_t *arr = NULL;
    s = basl_array_object_new(basl_vm_runtime(vm), items, part_count, &arr, error);
    for (size_t i = 0; i < part_count; i++) basl_value_release(&items[i]);
    free(items);
    if (s != BASL_STATUS_OK) return s;

    basl_value_t val;
    basl_value_init_object(&val, &arr);
    s = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return s;
}

/* ── Module Descriptor ──────────────────────────────────────── */

static const int match_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int find_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int find_ret[] = { BASL_TYPE_STRING, BASL_TYPE_BOOL };
static const int find_all_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int replace_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int split_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING };

static const basl_native_module_function_t basl_regex_functions[] = {
    {
        "match", 5U,
        basl_regex_match_fn,
        2U, match_params,
        BASL_TYPE_BOOL, 1U, NULL, 0,
        NULL, NULL
    },
    {
        "find", 4U,
        basl_regex_find_fn,
        2U, find_params,
        BASL_TYPE_STRING, 2U, find_ret, 0,
        NULL, NULL
    },
    {
        "find_all", 8U,
        basl_regex_find_all_fn,
        2U, find_all_params,
        BASL_TYPE_OBJECT, 1U, NULL, BASL_TYPE_STRING,
        NULL, NULL
    },
    {
        "replace", 7U,
        basl_regex_replace_fn,
        3U, replace_params,
        BASL_TYPE_STRING, 1U, NULL, 0,
        NULL, NULL
    },
    {
        "replace_all", 11U,
        basl_regex_replace_all_fn,
        3U, replace_params,
        BASL_TYPE_STRING, 1U, NULL, 0,
        NULL, NULL
    },
    {
        "split", 5U,
        basl_regex_split_fn,
        2U, split_params,
        BASL_TYPE_OBJECT, 1U, NULL, BASL_TYPE_STRING,
        NULL, NULL
    },
};

#define BASL_REGEX_FUNCTION_COUNT \
    (sizeof(basl_regex_functions) / sizeof(basl_regex_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_regex = {
    "regex", 5U,
    basl_regex_functions,
    BASL_REGEX_FUNCTION_COUNT,
    NULL, 0U
};
