/* BASL standard library: fmt module.
 *
 * Platform-specific stdlib modules live in src/stdlib/.  They may use
 * any system headers needed for their platform.  The core interpreter
 * in src/ never includes these files.
 */
#include <stdio.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"

/* ── helpers ─────────────────────────────────────────────────────── */

static void basl_fmt_print_value(FILE *stream, basl_value_t v) {
    if (basl_nanbox_is_int(v)) {
        fprintf(stream, "%lld", (long long)basl_nanbox_decode_int(v));
    } else if (basl_nanbox_is_double(v)) {
        fprintf(stream, "%g", basl_nanbox_decode_double(v));
    } else if (basl_nanbox_is_bool(v)) {
        fprintf(stream, "%s", basl_nanbox_decode_bool(v) ? "true" : "false");
    } else if (basl_nanbox_is_nil(v)) {
        fprintf(stream, "nil");
    } else if (basl_nanbox_is_object(v)) {
        const basl_object_t *obj =
            (const basl_object_t *)basl_nanbox_decode_ptr(v);
        if (obj != NULL && basl_object_type(obj) == BASL_OBJECT_STRING) {
            const char *text = basl_string_object_c_str(obj);
            size_t len = basl_string_object_length(obj);

            if (text != NULL) {
                fwrite(text, 1U, len, stream);
                return;
            }
        }
        fprintf(stream, "<object>");
    }
}

/* ── native callbacks ────────────────────────────────────────────── */

static basl_status_t basl_fmt_println(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base;
    size_t i;

    (void)error;
    base = basl_vm_stack_depth(vm) - arg_count;
    for (i = 0U; i < arg_count; i++) {
        if (i > 0U) {
            fputc(' ', stdout);
        }
        basl_fmt_print_value(stdout, basl_vm_stack_get(vm, base + i));
    }
    fputc('\n', stdout);
    basl_vm_stack_pop_n(vm, arg_count);
    return BASL_STATUS_OK;
}

static basl_status_t basl_fmt_print(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base;
    size_t i;

    (void)error;
    base = basl_vm_stack_depth(vm) - arg_count;
    for (i = 0U; i < arg_count; i++) {
        if (i > 0U) {
            fputc(' ', stdout);
        }
        basl_fmt_print_value(stdout, basl_vm_stack_get(vm, base + i));
    }
    fflush(stdout);
    basl_vm_stack_pop_n(vm, arg_count);
    return BASL_STATUS_OK;
}

static basl_status_t basl_fmt_eprintln(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base;
    size_t i;

    (void)error;
    base = basl_vm_stack_depth(vm) - arg_count;
    for (i = 0U; i < arg_count; i++) {
        if (i > 0U) {
            fputc(' ', stderr);
        }
        basl_fmt_print_value(stderr, basl_vm_stack_get(vm, base + i));
    }
    fputc('\n', stderr);
    basl_vm_stack_pop_n(vm, arg_count);
    return BASL_STATUS_OK;
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int basl_fmt_println_params[] = { BASL_TYPE_STRING };
static const int basl_fmt_print_params[] = { BASL_TYPE_STRING };
static const int basl_fmt_eprintln_params[] = { BASL_TYPE_STRING };

static const basl_native_module_function_t basl_fmt_functions[] = {
    {
        "println", 7U,
        basl_fmt_println,
        1U, basl_fmt_println_params,
        BASL_TYPE_VOID, 0U, NULL
    },
    {
        "print", 5U,
        basl_fmt_print,
        1U, basl_fmt_print_params,
        BASL_TYPE_VOID, 0U, NULL
    },
    {
        "eprintln", 8U,
        basl_fmt_eprintln,
        1U, basl_fmt_eprintln_params,
        BASL_TYPE_VOID, 0U, NULL
    },
};

#define BASL_FMT_FUNCTION_COUNT \
    (sizeof(basl_fmt_functions) / sizeof(basl_fmt_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_fmt = {
    "fmt", 3U,
    basl_fmt_functions,
    BASL_FMT_FUNCTION_COUNT,
    NULL, 0U
};
