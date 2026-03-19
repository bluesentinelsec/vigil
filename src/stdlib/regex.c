/* VIGIL standard library: regex module
 *
 * RE2-style regular expressions with linear time guarantees.
 */
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

#include "regex.h"

/* ── Helpers ────────────────────────────────────────────────── */

static bool get_string_arg(vigil_vm_t *vm, size_t base, size_t idx, const char **str, size_t *len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return false;
    *str = vigil_string_object_c_str(obj);
    *len = vigil_string_object_length(obj);
    return true;
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), str, len, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t push_bool(vigil_vm_t *vm, bool b, vigil_error_t *error)
{
    vigil_value_t val;
    vigil_value_init_bool(&val, b);
    return vigil_vm_stack_push(vm, &val, error);
}

/* ── regex.match(pattern, input) -> bool ────────────────────── */

static vigil_status_t vigil_regex_match_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input;
    size_t pattern_len, input_len;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) || !get_string_arg(vm, base, 1, &input, &input_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, false, error);
    }

    char err_buf[128];
    vigil_regex_t *re = vigil_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, false, error);
    }

    bool matched = vigil_regex_match(re, input, input_len, NULL);
    vigil_regex_free(re);

    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, matched, error);
}

/* ── regex.find(pattern, input) -> (string, bool) ───────────── */

static vigil_status_t vigil_regex_find_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input;
    size_t pattern_len, input_len;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) || !get_string_arg(vm, base, 1, &input, &input_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        s = push_string(vm, "", 0, error);
        if (s == VIGIL_STATUS_OK)
            s = push_bool(vm, false, error);
        return s;
    }

    char err_buf[128];
    vigil_regex_t *re = vigil_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        s = push_string(vm, "", 0, error);
        if (s == VIGIL_STATUS_OK)
            s = push_bool(vm, false, error);
        return s;
    }

    vigil_regex_result_t result;
    bool found = vigil_regex_find(re, input, input_len, &result);
    vigil_regex_free(re);

    vigil_vm_stack_pop_n(vm, arg_count);

    if (found && result.groups[0].start != SIZE_MAX)
    {
        size_t start = result.groups[0].start;
        size_t end = result.groups[0].end;
        s = push_string(vm, input + start, end - start, error);
        if (s == VIGIL_STATUS_OK)
            s = push_bool(vm, true, error);
    }
    else
    {
        s = push_string(vm, "", 0, error);
        if (s == VIGIL_STATUS_OK)
            s = push_bool(vm, false, error);
    }
    return s;
}

/* ── regex.find_all(pattern, input) -> array<string> ────────── */

static vigil_status_t vigil_regex_find_all_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input;
    size_t pattern_len, input_len;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) || !get_string_arg(vm, base, 1, &input, &input_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        /* Return empty array */
        vigil_object_t *arr = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    char err_buf[128];
    vigil_regex_t *re = vigil_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *arr = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    vigil_regex_result_t results[256];
    size_t count = vigil_regex_find_all(re, input, input_len, results, 256);
    vigil_regex_free(re);

    /* Build array of match strings */
    vigil_value_t *items = NULL;
    if (count > 0)
    {
        items = malloc(count * sizeof(vigil_value_t));
        if (!items)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < count; i++)
        {
            size_t start = results[i].groups[0].start;
            size_t end = results[i].groups[0].end;
            vigil_object_t *str_obj = NULL;
            s = vigil_string_object_new(vigil_vm_runtime(vm), input + start, end - start, &str_obj, error);
            if (s != VIGIL_STATUS_OK)
            {
                for (size_t j = 0; j < i; j++)
                    vigil_value_release(&items[j]);
                free(items);
                vigil_vm_stack_pop_n(vm, arg_count);
                return s;
            }
            vigil_value_init_object(&items[i], &str_obj);
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_object_t *arr = NULL;
    s = vigil_array_object_new(vigil_vm_runtime(vm), items, count, &arr, error);
    for (size_t i = 0; i < count; i++)
        vigil_value_release(&items[i]);
    free(items);
    if (s != VIGIL_STATUS_OK)
        return s;

    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

/* ── regex.replace(pattern, input, replacement) -> string ───── */

static vigil_status_t vigil_regex_replace_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input, *replacement;
    size_t pattern_len, input_len, replacement_len;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) || !get_string_arg(vm, base, 1, &input, &input_len) ||
        !get_string_arg(vm, base, 2, &replacement, &replacement_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    char err_buf[128];
    vigil_regex_t *re = vigil_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, input, input_len, error);
    }

    char *output = NULL;
    size_t output_len = 0;
    s = vigil_regex_replace(re, input, input_len, replacement, replacement_len, &output, &output_len);
    vigil_regex_free(re);

    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK || !output)
    {
        return push_string(vm, input, input_len, error);
    }

    s = push_string(vm, output, output_len, error);
    free(output);
    return s;
}

/* ── regex.replace_all(pattern, input, replacement) -> string ─ */

static vigil_status_t vigil_regex_replace_all_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input, *replacement;
    size_t pattern_len, input_len, replacement_len;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) || !get_string_arg(vm, base, 1, &input, &input_len) ||
        !get_string_arg(vm, base, 2, &replacement, &replacement_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    char err_buf[128];
    vigil_regex_t *re = vigil_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, input, input_len, error);
    }

    char *output = NULL;
    size_t output_len = 0;
    s = vigil_regex_replace_all(re, input, input_len, replacement, replacement_len, &output, &output_len);
    vigil_regex_free(re);

    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK || !output)
    {
        return push_string(vm, input, input_len, error);
    }

    s = push_string(vm, output, output_len, error);
    free(output);
    return s;
}

/* ── regex.split(pattern, input) -> array<string> ───────────── */

static vigil_status_t vigil_regex_split_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *pattern, *input;
    size_t pattern_len, input_len;
    vigil_status_t s;

    if (!get_string_arg(vm, base, 0, &pattern, &pattern_len) || !get_string_arg(vm, base, 1, &input, &input_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *arr = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    char err_buf[128];
    vigil_regex_t *re = vigil_regex_compile(pattern, pattern_len, err_buf, sizeof(err_buf));
    if (!re)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        /* Return array with just the input */
        vigil_object_t *str_obj = NULL;
        s = vigil_string_object_new(vigil_vm_runtime(vm), input, input_len, &str_obj, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t item;
        vigil_value_init_object(&item, &str_obj);
        vigil_object_t *arr = NULL;
        s = vigil_array_object_new(vigil_vm_runtime(vm), &item, 1, &arr, error);
        vigil_value_release(&item);
        if (s != VIGIL_STATUS_OK)
            return s;
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    char **parts = NULL;
    size_t *part_lens = NULL;
    size_t part_count = 0;
    s = vigil_regex_split(re, input, input_len, &parts, &part_lens, &part_count);
    vigil_regex_free(re);

    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK)
    {
        return s;
    }

    /* Build array */
    vigil_value_t *items = malloc(part_count * sizeof(vigil_value_t));
    if (!items)
    {
        for (size_t i = 0; i < part_count; i++)
            free(parts[i]);
        free(parts);
        free(part_lens);
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < part_count; i++)
    {
        vigil_object_t *str_obj = NULL;
        s = vigil_string_object_new(vigil_vm_runtime(vm), parts[i], part_lens[i], &str_obj, error);
        free(parts[i]);
        if (s != VIGIL_STATUS_OK)
        {
            for (size_t j = 0; j < i; j++)
                vigil_value_release(&items[j]);
            for (size_t j = i + 1; j < part_count; j++)
                free(parts[j]);
            free(parts);
            free(part_lens);
            free(items);
            return s;
        }
        vigil_value_init_object(&items[i], &str_obj);
    }
    free(parts);
    free(part_lens);

    vigil_object_t *arr = NULL;
    s = vigil_array_object_new(vigil_vm_runtime(vm), items, part_count, &arr, error);
    for (size_t i = 0; i < part_count; i++)
        vigil_value_release(&items[i]);
    free(items);
    if (s != VIGIL_STATUS_OK)
        return s;

    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

/* ── Module Descriptor ──────────────────────────────────────── */

static const int match_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int find_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int find_ret[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_BOOL};
static const int find_all_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int replace_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int split_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};

static const vigil_native_module_function_t vigil_regex_functions[] = {
    {"match", 5U, vigil_regex_match_fn, 2U, match_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"find", 4U, vigil_regex_find_fn, 2U, find_params, VIGIL_TYPE_STRING, 2U, find_ret, 0, NULL, NULL},
    {"find_all", 8U, vigil_regex_find_all_fn, 2U, find_all_params, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL,
     NULL},
    {"replace", 7U, vigil_regex_replace_fn, 3U, replace_params, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"replace_all", 11U, vigil_regex_replace_all_fn, 3U, replace_params, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"split", 5U, vigil_regex_split_fn, 2U, split_params, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL, NULL},
};

#define VIGIL_REGEX_FUNCTION_COUNT (sizeof(vigil_regex_functions) / sizeof(vigil_regex_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_regex = {
    "regex", 5U, vigil_regex_functions, VIGIL_REGEX_FUNCTION_COUNT, NULL, 0U};
