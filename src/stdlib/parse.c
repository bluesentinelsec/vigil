/* BASL standard library: parse module.
 *
 * Provides string-to-value parsing functions.
 *   parse.i32(string) -> (i32, err)
 *   parse.i64(string) -> (i64, err)
 *   parse.f64(string) -> (f64, err)
 *   parse.bool(string) -> (bool, err)
 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"
#include "basl/runtime.h"

/* ── Helpers ─────────────────────────────────────────────────── */

static const char *get_string_arg(basl_vm_t *vm, size_t base) {
    basl_value_t v = basl_vm_stack_get(vm, base);
    basl_object_t *obj = basl_value_as_object(&v);
    if (obj == NULL || basl_object_type(obj) != BASL_OBJECT_STRING) return NULL;
    return basl_string_object_c_str(obj);
}

static basl_status_t push_ok_err(basl_vm_t *vm, basl_error_t *error) {
    basl_object_t *obj = NULL;
    basl_status_t s = basl_error_object_new_cstr(basl_vm_runtime(vm), "", 0, &obj, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_t v;
    basl_value_init_object(&v, &obj);
    s = basl_vm_stack_push(vm, &v, error);
    basl_object_release(&obj);
    return s;
}

static basl_status_t push_parse_err(basl_vm_t *vm, const char *msg, basl_error_t *error) {
    /* err.parse = kind 8 */
    basl_object_t *obj = NULL;
    basl_status_t s = basl_error_object_new_cstr(basl_vm_runtime(vm), msg, 8, &obj, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_t v;
    basl_value_init_object(&v, &obj);
    s = basl_vm_stack_push(vm, &v, error);
    basl_object_release(&obj);
    return s;
}

/* ── Functions ───────────────────────────────────────────────── */

static basl_status_t parse_i32_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *s = get_string_arg(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);

    if (s == NULL || *s == '\0') {
        basl_value_t v; basl_value_init_int(&v, 0);
        basl_status_t st = basl_vm_stack_push(vm, &v, error);
        if (st != BASL_STATUS_OK) return st;
        return push_parse_err(vm, "empty string", error);
    }
    char *end = NULL;
    errno = 0;
    long val = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || val < INT32_MIN || val > INT32_MAX) {
        basl_value_t v; basl_value_init_int(&v, 0);
        basl_status_t st = basl_vm_stack_push(vm, &v, error);
        if (st != BASL_STATUS_OK) return st;
        return push_parse_err(vm, "invalid integer", error);
    }
    basl_value_t v; basl_value_init_int(&v, (int64_t)val);
    basl_status_t st = basl_vm_stack_push(vm, &v, error);
    if (st != BASL_STATUS_OK) return st;
    return push_ok_err(vm, error);
}

static basl_status_t parse_i64_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *s = get_string_arg(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);

    if (s == NULL || *s == '\0') {
        basl_value_t v; basl_value_init_int(&v, 0);
        basl_status_t st = basl_vm_stack_push(vm, &v, error);
        if (st != BASL_STATUS_OK) return st;
        return push_parse_err(vm, "empty string", error);
    }
    char *end = NULL;
    errno = 0;
    long long val = strtoll(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        basl_value_t v; basl_value_init_int(&v, 0);
        basl_status_t st = basl_vm_stack_push(vm, &v, error);
        if (st != BASL_STATUS_OK) return st;
        return push_parse_err(vm, "invalid integer", error);
    }
    basl_value_t v; basl_value_init_int(&v, (int64_t)val);
    basl_status_t st = basl_vm_stack_push(vm, &v, error);
    if (st != BASL_STATUS_OK) return st;
    return push_ok_err(vm, error);
}

static basl_status_t parse_f64_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *s = get_string_arg(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);

    if (s == NULL || *s == '\0') {
        basl_value_t v; basl_value_init_float(&v, 0.0);
        basl_status_t st = basl_vm_stack_push(vm, &v, error);
        if (st != BASL_STATUS_OK) return st;
        return push_parse_err(vm, "empty string", error);
    }
    char *end = NULL;
    errno = 0;
    double val = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0') {
        basl_value_t v; basl_value_init_float(&v, 0.0);
        basl_status_t st = basl_vm_stack_push(vm, &v, error);
        if (st != BASL_STATUS_OK) return st;
        return push_parse_err(vm, "invalid float", error);
    }
    basl_value_t v; basl_value_init_float(&v, val);
    basl_status_t st = basl_vm_stack_push(vm, &v, error);
    if (st != BASL_STATUS_OK) return st;
    return push_ok_err(vm, error);
}

static basl_status_t parse_bool_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *s = get_string_arg(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);

    if (s == NULL) {
        basl_value_t v; basl_value_init_bool(&v, 0);
        basl_status_t st = basl_vm_stack_push(vm, &v, error);
        if (st != BASL_STATUS_OK) return st;
        return push_parse_err(vm, "invalid boolean", error);
    }
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) {
        basl_value_t v; basl_value_init_bool(&v, 1);
        basl_status_t st = basl_vm_stack_push(vm, &v, error);
        if (st != BASL_STATUS_OK) return st;
        return push_ok_err(vm, error);
    }
    if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0) {
        basl_value_t v; basl_value_init_bool(&v, 0);
        basl_status_t st = basl_vm_stack_push(vm, &v, error);
        if (st != BASL_STATUS_OK) return st;
        return push_ok_err(vm, error);
    }
    basl_value_t v; basl_value_init_bool(&v, 0);
    basl_status_t st = basl_vm_stack_push(vm, &v, error);
    if (st != BASL_STATUS_OK) return st;
    return push_parse_err(vm, "invalid boolean", error);
}

/* ── Module definition ───────────────────────────────────────── */

static const int string_param[] = { BASL_TYPE_STRING };

static const int i32_err_returns[] = { BASL_TYPE_I32, BASL_TYPE_ERR };
static const int i64_err_returns[] = { BASL_TYPE_I64, BASL_TYPE_ERR };
static const int f64_err_returns[] = { BASL_TYPE_F64, BASL_TYPE_ERR };
static const int bool_err_returns[] = { BASL_TYPE_BOOL, BASL_TYPE_ERR };

static const basl_native_module_function_t parse_funcs[] = {
    {"i32", 3U, parse_i32_fn, 1U, string_param, BASL_TYPE_I32, 2U, i32_err_returns, 0, NULL, NULL},
    {"i64", 3U, parse_i64_fn, 1U, string_param, BASL_TYPE_I64, 2U, i64_err_returns, 0, NULL, NULL},
    {"f64", 3U, parse_f64_fn, 1U, string_param, BASL_TYPE_F64, 2U, f64_err_returns, 0, NULL, NULL},
    {"bool", 4U, parse_bool_fn, 1U, string_param, BASL_TYPE_BOOL, 2U, bool_err_returns, 0, NULL, NULL},
};

BASL_API const basl_native_module_t basl_stdlib_parse = {
    "parse", 5U,
    parse_funcs, sizeof(parse_funcs) / sizeof(parse_funcs[0]),
    NULL, 0U
};
