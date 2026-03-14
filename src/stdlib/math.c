/* BASL standard library: math module.
 *
 * All functions use only C11 <math.h> — fully platform-universal.
 */
#include <math.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"

/* ── helpers ─────────────────────────────────────────────────────── */

static double basl_math_pop_f64(basl_vm_t *vm) {
    basl_value_t v = basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1U);
    basl_vm_stack_pop_n(vm, 1U);
    return basl_nanbox_decode_double(v);
}

static basl_status_t basl_math_push_f64(
    basl_vm_t *vm, double d, basl_error_t *error
) {
    basl_value_t val = basl_nanbox_encode_double(d);
    return basl_vm_stack_push(vm, &val, error);
}

/* ── f64 -> f64 callbacks ────────────────────────────────────────── */

#define MATH_UNARY(name, cfn)                                       \
    static basl_status_t basl_math_##name(                          \
        basl_vm_t *vm, size_t arg_count, basl_error_t *error        \
    ) {                                                             \
        double a;                                                   \
        (void)arg_count;                                            \
        a = basl_math_pop_f64(vm);                                  \
        return basl_math_push_f64(vm, cfn(a), error);               \
    }

MATH_UNARY(floor, floor)
MATH_UNARY(ceil, ceil)
MATH_UNARY(round, round)
MATH_UNARY(abs, fabs)
MATH_UNARY(sqrt, sqrt)
MATH_UNARY(sin, sin)
MATH_UNARY(cos, cos)
MATH_UNARY(tan, tan)
MATH_UNARY(log, log)
MATH_UNARY(log2, log2)
MATH_UNARY(log10, log10)
MATH_UNARY(exp, exp)
MATH_UNARY(trunc, trunc)

/* ── () -> f64 callbacks ─────────────────────────────────────────── */

static basl_status_t basl_math_pi(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    return basl_math_push_f64(vm, 3.14159265358979323846, error);
}

static basl_status_t basl_math_e(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    return basl_math_push_f64(vm, 2.71828182845904523536, error);
}

/* ── (f64, f64) -> f64 callbacks ─────────────────────────────────── */

static basl_status_t basl_math_pow(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    double base_val, exp_val;
    (void)arg_count;
    exp_val = basl_math_pop_f64(vm);
    base_val = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, pow(base_val, exp_val), error);
}

static basl_status_t basl_math_min(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    double a, b;
    (void)arg_count;
    b = basl_math_pop_f64(vm);
    a = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, fmin(a, b), error);
}

static basl_status_t basl_math_max(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    double a, b;
    (void)arg_count;
    b = basl_math_pop_f64(vm);
    a = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, fmax(a, b), error);
}

static basl_status_t basl_math_atan2(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    double y, x;
    (void)arg_count;
    x = basl_math_pop_f64(vm);
    y = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, atan2(y, x), error);
}

static basl_status_t basl_math_hypot(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    double a, b;
    (void)arg_count;
    b = basl_math_pop_f64(vm);
    a = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, hypot(a, b), error);
}

static basl_status_t basl_math_fmod(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    double a, b;
    (void)arg_count;
    b = basl_math_pop_f64(vm);
    a = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, fmod(a, b), error);
}

static basl_status_t basl_math_sign(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    double a;
    (void)arg_count;
    a = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, (a > 0.0) - (a < 0.0), error);
}

/* ── (f64, f64, f64) -> f64 callbacks ────────────────────────────── */

static basl_status_t basl_math_clamp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    double val, lo, hi;
    (void)arg_count;
    hi = basl_math_pop_f64(vm);
    lo = basl_math_pop_f64(vm);
    val = basl_math_pop_f64(vm);
    if (val < lo) val = lo;
    if (val > hi) val = hi;
    return basl_math_push_f64(vm, val, error);
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int basl_math_f64_params[] = { BASL_TYPE_F64 };
static const int basl_math_f64f64_params[] = {
    BASL_TYPE_F64, BASL_TYPE_F64
};
static const int basl_math_f64f64f64_params[] = {
    BASL_TYPE_F64, BASL_TYPE_F64, BASL_TYPE_F64
};

#define MATH_FN0(id, n, nl)                                         \
    { n, nl, basl_math_##id, 0U, NULL,                             \
      BASL_TYPE_F64, 1U, NULL }

#define MATH_FN1(id, n, nl)                                         \
    { n, nl, basl_math_##id, 1U, basl_math_f64_params,             \
      BASL_TYPE_F64, 1U, NULL }

#define MATH_FN2(id, n, nl)                                         \
    { n, nl, basl_math_##id, 2U, basl_math_f64f64_params,          \
      BASL_TYPE_F64, 1U, NULL }

#define MATH_FN3(id, n, nl)                                         \
    { n, nl, basl_math_##id, 3U, basl_math_f64f64f64_params,       \
      BASL_TYPE_F64, 1U, NULL }

static const basl_native_module_function_t basl_math_functions[] = {
    MATH_FN0(pi,    "pi",    2U),
    MATH_FN0(e,     "e",     1U),
    MATH_FN1(floor, "floor", 5U),
    MATH_FN1(ceil,  "ceil",  4U),
    MATH_FN1(round, "round", 5U),
    MATH_FN1(trunc, "trunc", 5U),
    MATH_FN1(abs,   "abs",   3U),
    MATH_FN1(sign,  "sign",  4U),
    MATH_FN1(sqrt,  "sqrt",  4U),
    MATH_FN1(sin,   "sin",   3U),
    MATH_FN1(cos,   "cos",   3U),
    MATH_FN1(tan,   "tan",   3U),
    MATH_FN1(log,   "log",   3U),
    MATH_FN1(log2,  "log2",  4U),
    MATH_FN1(log10, "log10", 5U),
    MATH_FN1(exp,   "exp",   3U),
    MATH_FN2(pow,   "pow",   3U),
    MATH_FN2(min,   "min",   3U),
    MATH_FN2(max,   "max",   3U),
    MATH_FN2(atan2, "atan2", 5U),
    MATH_FN2(hypot, "hypot", 5U),
    MATH_FN2(fmod,  "fmod",  4U),
    MATH_FN3(clamp, "clamp", 5U),
};

#define BASL_MATH_FUNCTION_COUNT \
    (sizeof(basl_math_functions) / sizeof(basl_math_functions[0]))

/* ── Vec2 class ──────────────────────────────────────────────────── */

/*
 * Vec2 methods receive self (the instance) as the first stack arg.
 * self is at stack[base], additional args follow.
 */

static double basl_vec2_get_field(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t self_val = basl_vm_stack_get(vm, base);
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(self_val);
    basl_value_t field;
    basl_instance_object_get_field(obj, idx, &field);
    return basl_nanbox_decode_double(field);
}

static basl_status_t basl_vec2_length(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec2_get_field(vm, base, 0U);
    double y = basl_vec2_get_field(vm, base, 1U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, sqrt(x * x + y * y), error);
}

static basl_status_t basl_vec2_dot(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec2_get_field(vm, base, 0U);
    double y1 = basl_vec2_get_field(vm, base, 1U);
    /* second arg is also a Vec2 instance */
    basl_value_t other_val = basl_vm_stack_get(vm, base + 1U);
    basl_object_t *other = (basl_object_t *)basl_nanbox_decode_ptr(other_val);
    basl_value_t fx, fy;
    basl_instance_object_get_field(other, 0U, &fx);
    basl_instance_object_get_field(other, 1U, &fy);
    double x2 = basl_nanbox_decode_double(fx);
    double y2 = basl_nanbox_decode_double(fy);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, x1 * x2 + y1 * y2, error);
}

static const basl_native_class_field_t basl_vec2_fields[] = {
    { "x", 1U, BASL_TYPE_F64 },
    { "y", 1U, BASL_TYPE_F64 },
};

static const int basl_vec2_dot_params[] = { BASL_TYPE_OBJECT };

static const basl_native_class_method_t basl_vec2_methods[] = {
    { "length", 6U, basl_vec2_length, 0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "dot", 3U, basl_vec2_dot, 1U, basl_vec2_dot_params,
      BASL_TYPE_F64, 1U, NULL },
};

static const basl_native_class_t basl_math_classes[] = {
    {
        "Vec2", 4U,
        basl_vec2_fields, 2U,
        basl_vec2_methods, 2U,
        NULL
    },
};

#define BASL_MATH_CLASS_COUNT \
    (sizeof(basl_math_classes) / sizeof(basl_math_classes[0]))

BASL_API const basl_native_module_t basl_stdlib_math = {
    "math", 4U,
    basl_math_functions,
    BASL_MATH_FUNCTION_COUNT,
    basl_math_classes,
    BASL_MATH_CLASS_COUNT
};
