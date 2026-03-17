/* BASL standard library: readline module.
 *
 * Provides line editing with history support.
 *
 * readline.input(string prompt) -> string
 * readline.history_add(string line)
 * readline.history_get(i32 index) -> string
 * readline.history_length() -> i32
 * readline.history_clear()
 * readline.history_load(string path)
 * readline.history_save(string path)
 */
#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"
#include "platform/platform.h"

/* ── Global history (shared across all calls) ────────────────────── */

static basl_line_history_t g_history;
static int g_history_initialized = 0;

static void ensure_history(void) {
    if (!g_history_initialized) {
        basl_line_history_init(&g_history, 1000);
        g_history_initialized = 1;
    }
}

/* ── Native callbacks ────────────────────────────────────────────── */

static basl_status_t basl_readline_input(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base;
    basl_value_t prompt_val;
    const char *prompt = "";
    char buf[4096];
    basl_status_t status;
    basl_object_t *result = NULL;
    basl_value_t val;

    (void)arg_count;
    ensure_history();

    base = basl_vm_stack_depth(vm) - 1;
    prompt_val = (basl_value_t){basl_vm_stack_get(vm, base)};
    basl_vm_stack_pop_n(vm, 1);

    if (basl_nanbox_is_object(prompt_val)) {
        basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(prompt_val);
        if (obj && basl_object_type(obj) == BASL_OBJECT_STRING) {
            prompt = basl_string_object_c_str(obj);
        }
    }

    status = basl_line_editor_readline(prompt, buf, sizeof(buf), &g_history, error);
    if (status != BASL_STATUS_OK) {
        /* EOF — return empty string. */
        status = basl_string_object_new(basl_vm_runtime(vm), "", 0, &result, error);
        if (status != BASL_STATUS_OK) return status;
        basl_value_init_object(&val, &result);
        return basl_vm_stack_push(vm, &val, error);
    }

    /* Add to history if non-empty. */
    if (buf[0]) basl_line_history_add(&g_history, buf);

    status = basl_string_object_new_cstr(basl_vm_runtime(vm), buf, &result, error);
    if (status != BASL_STATUS_OK) return status;
    basl_value_init_object(&val, &result);
    return basl_vm_stack_push(vm, &val, error);
}

static basl_status_t basl_readline_history_add(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base;
    basl_value_t line_val;
    const char *line = "";

    (void)arg_count;
    (void)error;
    ensure_history();

    base = basl_vm_stack_depth(vm) - 1;
    line_val = (basl_value_t){basl_vm_stack_get(vm, base)};
    basl_vm_stack_pop_n(vm, 1);

    if (basl_nanbox_is_object(line_val)) {
        basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(line_val);
        if (obj && basl_object_type(obj) == BASL_OBJECT_STRING) {
            line = basl_string_object_c_str(obj);
        }
    }

    basl_line_history_add(&g_history, line);
    return BASL_STATUS_OK;
}

static basl_status_t basl_readline_history_get(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base;
    int64_t index;
    const char *entry;
    basl_object_t *result = NULL;
    basl_value_t val;
    basl_status_t status;

    (void)arg_count;
    ensure_history();

    base = basl_vm_stack_depth(vm) - 1;
    index = basl_value_as_int(&(basl_value_t){basl_vm_stack_get(vm, base)});
    basl_vm_stack_pop_n(vm, 1);

    entry = basl_line_history_get(&g_history, (size_t)index);
    if (!entry) entry = "";

    status = basl_string_object_new_cstr(basl_vm_runtime(vm), entry, &result, error);
    if (status != BASL_STATUS_OK) return status;
    basl_value_init_object(&val, &result);
    return basl_vm_stack_push(vm, &val, error);
}

static basl_status_t basl_readline_history_length(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    basl_value_t val;
    (void)arg_count;
    (void)error;
    ensure_history();
    basl_value_init_int(&val, (int64_t)g_history.count);
    return basl_vm_stack_push(vm, &val, error);
}

static basl_status_t basl_readline_history_clear(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)vm;
    (void)arg_count;
    (void)error;
    ensure_history();
    basl_line_history_clear(&g_history);
    return BASL_STATUS_OK;
}

static basl_status_t basl_readline_history_load(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base;
    basl_value_t path_val;
    const char *path = "";

    (void)arg_count;
    ensure_history();

    base = basl_vm_stack_depth(vm) - 1;
    path_val = (basl_value_t){basl_vm_stack_get(vm, base)};
    basl_vm_stack_pop_n(vm, 1);

    if (basl_nanbox_is_object(path_val)) {
        basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(path_val);
        if (obj && basl_object_type(obj) == BASL_OBJECT_STRING) {
            path = basl_string_object_c_str(obj);
        }
    }

    return basl_line_history_load(&g_history, path, error);
}

static basl_status_t basl_readline_history_save(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base;
    basl_value_t path_val;
    const char *path = "";

    (void)arg_count;
    ensure_history();

    base = basl_vm_stack_depth(vm) - 1;
    path_val = (basl_value_t){basl_vm_stack_get(vm, base)};
    basl_vm_stack_pop_n(vm, 1);

    if (basl_nanbox_is_object(path_val)) {
        basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(path_val);
        if (obj && basl_object_type(obj) == BASL_OBJECT_STRING) {
            path = basl_string_object_c_str(obj);
        }
    }

    return basl_line_history_save(&g_history, path, error);
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int basl_readline_string_param[] = { BASL_TYPE_STRING };
static const int basl_readline_i32_param[] = { BASL_TYPE_I32 };

static const basl_native_module_function_t basl_readline_functions[] = {
    {
        "input", 5,
        basl_readline_input,
        1, basl_readline_string_param,
        BASL_TYPE_STRING,
        1, NULL, 0
    },
    {
        "history_add", 11,
        basl_readline_history_add,
        1, basl_readline_string_param,
        BASL_TYPE_VOID,
        0, NULL, 0
    },
    {
        "history_get", 11,
        basl_readline_history_get,
        1, basl_readline_i32_param,
        BASL_TYPE_STRING,
        1, NULL, 0
    },
    {
        "history_length", 14,
        basl_readline_history_length,
        0, NULL,
        BASL_TYPE_I32,
        1, NULL, 0
    },
    {
        "history_clear", 13,
        basl_readline_history_clear,
        0, NULL,
        BASL_TYPE_VOID,
        0, NULL, 0
    },
    {
        "history_load", 12,
        basl_readline_history_load,
        1, basl_readline_string_param,
        BASL_TYPE_VOID,
        0, NULL, 0
    },
    {
        "history_save", 12,
        basl_readline_history_save,
        1, basl_readline_string_param,
        BASL_TYPE_VOID,
        0, NULL, 0
    }
};

BASL_API const basl_native_module_t basl_stdlib_readline = {
    "readline", 8,
    basl_readline_functions,
    sizeof(basl_readline_functions) / sizeof(basl_readline_functions[0]),
    NULL, 0
};
