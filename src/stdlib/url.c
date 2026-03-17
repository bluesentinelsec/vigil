/* BASL standard library: url module.
 *
 * Provides URL parsing and manipulation.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "basl/url.h"
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

/* ── url.parse(url: string) -> string ────────────────────────────── */

static basl_status_t basl_url_parse_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    basl_url_t url;
    char result[2048];
    size_t len;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (basl_url_parse(url_str, url_len, &url, error) != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    len = (size_t)snprintf(result, sizeof(result), "%s|%s|%s|%s|%s|%s|%s|%s",
        url.scheme ? url.scheme : "",
        url.username ? url.username : "",
        url.password ? url.password : "",
        url.host ? url.host : "",
        url.port ? url.port : "",
        url.path ? url.path : "",
        url.raw_query ? url.raw_query : "",
        url.fragment ? url.fragment : "");

    basl_url_free(&url);
    return push_string(vm, result, len, error);
}

/* ── url.scheme(url: string) -> string ───────────────────────────── */

static basl_status_t basl_url_scheme_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    basl_url_t url;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (basl_url_parse(url_str, url_len, &url, error) != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.scheme ? url.scheme : "", url.scheme ? strlen(url.scheme) : 0, error);
    basl_url_free(&url);
    return s;
}

/* ── url.host(url: string) -> string ─────────────────────────────── */

static basl_status_t basl_url_host_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    basl_url_t url;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (basl_url_parse(url_str, url_len, &url, error) != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.host ? url.host : "", url.host ? strlen(url.host) : 0, error);
    basl_url_free(&url);
    return s;
}

/* ── url.port(url: string) -> string ─────────────────────────────── */

static basl_status_t basl_url_port_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    basl_url_t url;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (basl_url_parse(url_str, url_len, &url, error) != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.port ? url.port : "", url.port ? strlen(url.port) : 0, error);
    basl_url_free(&url);
    return s;
}

/* ── url.path(url: string) -> string ─────────────────────────────── */

static basl_status_t basl_url_path_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    basl_url_t url;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (basl_url_parse(url_str, url_len, &url, error) != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.path ? url.path : "", url.path ? strlen(url.path) : 0, error);
    basl_url_free(&url);
    return s;
}

/* ── url.query(url: string) -> string ────────────────────────────── */

static basl_status_t basl_url_query_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    basl_url_t url;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (basl_url_parse(url_str, url_len, &url, error) != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.raw_query ? url.raw_query : "", url.raw_query ? strlen(url.raw_query) : 0, error);
    basl_url_free(&url);
    return s;
}

/* ── url.fragment(url: string) -> string ─────────────────────────── */

static basl_status_t basl_url_fragment_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *url_str;
    size_t url_len;
    basl_url_t url;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &url_str, &url_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (basl_url_parse(url_str, url_len, &url, error) != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, url.fragment ? url.fragment : "", url.fragment ? strlen(url.fragment) : 0, error);
    basl_url_free(&url);
    return s;
}

/* ── url.encode(s: string) -> string ─────────────────────────────── */

static basl_status_t basl_url_encode_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *input;
    size_t input_len;
    char *encoded;
    size_t encoded_len;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &input, &input_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (basl_url_query_escape(input, input_len, &encoded, &encoded_len, error) != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, encoded, encoded_len, error);
    free(encoded);
    return s;
}

/* ── url.decode(s: string) -> string ─────────────────────────────── */

static basl_status_t basl_url_decode_fn(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *input;
    size_t input_len;
    char *decoded;
    size_t decoded_len;
    basl_status_t s;

    if (!get_string_arg(vm, base, 0, &input, &input_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    if (basl_url_unescape(input, input_len, &decoded, &decoded_len, error) != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    s = push_string(vm, decoded, decoded_len, error);
    free(decoded);
    return s;
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_param[] = {BASL_TYPE_STRING};

static const basl_native_module_function_t basl_url_functions[] = {
    {"parse", 5U, basl_url_parse_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"scheme", 6U, basl_url_scheme_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"host", 4U, basl_url_host_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"port", 4U, basl_url_port_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"path", 4U, basl_url_path_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"query", 5U, basl_url_query_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"fragment", 8U, basl_url_fragment_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"encode", 6U, basl_url_encode_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"decode", 6U, basl_url_decode_fn, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
};

#define URL_FUNCTION_COUNT \
    (sizeof(basl_url_functions) / sizeof(basl_url_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_url = {
    "url", 3U,
    basl_url_functions,
    URL_FUNCTION_COUNT,
    NULL, 0U
};
