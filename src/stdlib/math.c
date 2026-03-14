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

MATH_UNARY(asin, asin)
MATH_UNARY(acos, acos)
MATH_UNARY(atan, atan)

static basl_status_t basl_math_deg2rad(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    double d = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, d * 3.14159265358979323846 / 180.0, error);
}

static basl_status_t basl_math_rad2deg(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    double r = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, r * 180.0 / 3.14159265358979323846, error);
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

static basl_status_t basl_math_lerp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    double t = basl_math_pop_f64(vm);
    double b = basl_math_pop_f64(vm);
    double a = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, a + (b - a) * t, error);
}

static basl_status_t basl_math_normalize(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    double end = basl_math_pop_f64(vm);
    double start = basl_math_pop_f64(vm);
    double val = basl_math_pop_f64(vm);
    double range = end - start;
    return basl_math_push_f64(vm, (range == 0.0) ? 0.0 : (val - start) / range, error);
}

static basl_status_t basl_math_wrap(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    double hi = basl_math_pop_f64(vm);
    double lo = basl_math_pop_f64(vm);
    double val = basl_math_pop_f64(vm);
    double range = hi - lo;
    if (range == 0.0) return basl_math_push_f64(vm, lo, error);
    double result = fmod(val - lo, range);
    if (result < 0.0) result += range;
    return basl_math_push_f64(vm, result + lo, error);
}

/* ── (f64, f64, f64, f64, f64) -> f64 callbacks ─────────────────── */

static const int basl_math_f64x5_params[] = {
    BASL_TYPE_F64, BASL_TYPE_F64, BASL_TYPE_F64,
    BASL_TYPE_F64, BASL_TYPE_F64
};

static basl_status_t basl_math_remap(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    double out_end = basl_math_pop_f64(vm);
    double out_start = basl_math_pop_f64(vm);
    double in_end = basl_math_pop_f64(vm);
    double in_start = basl_math_pop_f64(vm);
    double val = basl_math_pop_f64(vm);
    double in_range = in_end - in_start;
    double t = (in_range == 0.0) ? 0.0 : (val - in_start) / in_range;
    return basl_math_push_f64(vm, out_start + (out_end - out_start) * t, error);
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

#define MATH_FN5(id, n, nl)                                         \
    { n, nl, basl_math_##id, 5U, basl_math_f64x5_params,           \
      BASL_TYPE_F64, 1U, NULL }

static const basl_native_module_function_t basl_math_functions[] = {
    MATH_FN0(pi,        "pi",       2U),
    MATH_FN0(e,         "e",        1U),
    MATH_FN1(floor,     "floor",    5U),
    MATH_FN1(ceil,      "ceil",     4U),
    MATH_FN1(round,     "round",    5U),
    MATH_FN1(trunc,     "trunc",    5U),
    MATH_FN1(abs,       "abs",      3U),
    MATH_FN1(sign,      "sign",     4U),
    MATH_FN1(sqrt,      "sqrt",     4U),
    MATH_FN1(sin,       "sin",      3U),
    MATH_FN1(cos,       "cos",      3U),
    MATH_FN1(tan,       "tan",      3U),
    MATH_FN1(asin,      "asin",     4U),
    MATH_FN1(acos,      "acos",     4U),
    MATH_FN1(atan,      "atan",     4U),
    MATH_FN1(log,       "log",      3U),
    MATH_FN1(log2,      "log2",     4U),
    MATH_FN1(log10,     "log10",    5U),
    MATH_FN1(exp,       "exp",      3U),
    MATH_FN1(deg2rad,   "deg2rad",  7U),
    MATH_FN1(rad2deg,   "rad2deg",  7U),
    MATH_FN2(pow,       "pow",      3U),
    MATH_FN2(min,       "min",      3U),
    MATH_FN2(max,       "max",      3U),
    MATH_FN2(atan2,     "atan2",    5U),
    MATH_FN2(hypot,     "hypot",    5U),
    MATH_FN2(fmod,      "fmod",     4U),
    MATH_FN3(clamp,     "clamp",    5U),
    MATH_FN3(lerp,      "lerp",     4U),
    MATH_FN3(normalize, "normalize", 9U),
    MATH_FN3(wrap,      "wrap",     4U),
    MATH_FN5(remap,     "remap",    5U),
};

#define BASL_MATH_FUNCTION_COUNT \
    (sizeof(basl_math_functions) / sizeof(basl_math_functions[0]))

/* ── Vec2 class ──────────────────────────────────────────────────── */

/*
 * Vec2/Vec3 methods receive self (the instance) as the first stack arg.
 * self is at stack[base], additional args follow.
 */

/* Helper: extract f64 field from an instance at a given stack slot. */
static double basl_vec_get_field(basl_vm_t *vm, size_t slot, size_t idx) {
    basl_value_t val = basl_vm_stack_get(vm, slot);
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(val);
    basl_value_t field;
    basl_instance_object_get_field(obj, idx, &field);
    return basl_nanbox_decode_double(field);
}

/* Helper: push a new Vec2 instance with given x, y. */
static basl_status_t basl_vec2_push_new(
    basl_vm_t *vm, double x, double y, size_t class_index,
    basl_error_t *error
) {
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_value_t fields[2];
    basl_object_t *inst;
    basl_value_t result;
    basl_status_t s;
    fields[0] = basl_nanbox_encode_double(x);
    fields[1] = basl_nanbox_encode_double(y);
    s = basl_instance_object_new(rt, class_index, fields, 2U, &inst, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_init_object(&result, &inst);
    s = basl_vm_stack_push(vm, &result, error);
    basl_value_release(&result);
    return s;
}

/* Get class_index from self instance. */
static size_t basl_vec_self_class(basl_vm_t *vm, size_t base) {
    basl_value_t val = basl_vm_stack_get(vm, base);
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(val);
    return basl_instance_object_class_index(obj);
}

static basl_status_t basl_vec2_length(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, sqrt(x * x + y * y), error);
}

static basl_status_t basl_vec2_dot(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, x1 * x2 + y1 * y2, error);
}

static basl_status_t basl_vec2_vnormalize(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    size_t ci = basl_vec_self_class(vm, base);
    double len = sqrt(x * x + y * y);
    basl_vm_stack_pop_n(vm, arg_count);
    if (len == 0.0) return basl_vec2_push_new(vm, 0.0, 0.0, ci, error);
    return basl_vec2_push_new(vm, x / len, y / len, ci, error);
}

static basl_status_t basl_vec2_add(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec2_push_new(vm, x1 + x2, y1 + y2, ci, error);
}

static basl_status_t basl_vec2_sub(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec2_push_new(vm, x1 - x2, y1 - y2, ci, error);
}

static basl_status_t basl_vec2_scale(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_value_t sv = basl_vm_stack_get(vm, base + 1U);
    double s = basl_nanbox_decode_double(sv);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec2_push_new(vm, x * s, y * s, ci, error);
}

static basl_status_t basl_vec2_distance(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double dx = basl_vec_get_field(vm, base, 0U) - basl_vec_get_field(vm, base + 1U, 0U);
    double dy = basl_vec_get_field(vm, base, 1U) - basl_vec_get_field(vm, base + 1U, 1U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, sqrt(dx * dx + dy * dy), error);
}

static const basl_native_class_field_t basl_vec2_fields[] = {
    { "x", 1U, BASL_TYPE_F64 },
    { "y", 1U, BASL_TYPE_F64 },
};

static const int basl_vec_obj_params[] = { BASL_TYPE_OBJECT };
static const int basl_vec_f64_params[] = { BASL_TYPE_F64 };

static const basl_native_class_method_t basl_vec2_methods[] = {
    { "length",    6U, basl_vec2_length,     0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "dot",       3U, basl_vec2_dot,        1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "normalize", 9U, basl_vec2_vnormalize, 0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "add",       3U, basl_vec2_add,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "sub",       3U, basl_vec2_sub,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "scale",     5U, basl_vec2_scale,      1U, basl_vec_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "distance",  8U, basl_vec2_distance,   1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
};

/* ── Vec3 class ──────────────────────────────────────────────────── */

static basl_status_t basl_vec3_push_new(
    basl_vm_t *vm, double x, double y, double z, size_t class_index,
    basl_error_t *error
) {
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_value_t fields[3];
    basl_object_t *inst;
    basl_value_t result;
    basl_status_t s;
    fields[0] = basl_nanbox_encode_double(x);
    fields[1] = basl_nanbox_encode_double(y);
    fields[2] = basl_nanbox_encode_double(z);
    s = basl_instance_object_new(rt, class_index, fields, 3U, &inst, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_init_object(&result, &inst);
    s = basl_vm_stack_push(vm, &result, error);
    basl_value_release(&result);
    return s;
}

static basl_status_t basl_vec3_length(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, sqrt(x*x + y*y + z*z), error);
}

static basl_status_t basl_vec3_dot(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double z1 = basl_vec_get_field(vm, base, 2U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    double z2 = basl_vec_get_field(vm, base + 1U, 2U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, x1*x2 + y1*y2 + z1*z2, error);
}

static basl_status_t basl_vec3_cross(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double z1 = basl_vec_get_field(vm, base, 2U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    double z2 = basl_vec_get_field(vm, base + 1U, 2U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec3_push_new(vm,
        y1*z2 - z1*y2,
        z1*x2 - x1*z2,
        x1*y2 - y1*x2, ci, error);
}

static basl_status_t basl_vec3_vnormalize(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    size_t ci = basl_vec_self_class(vm, base);
    double len = sqrt(x*x + y*y + z*z);
    basl_vm_stack_pop_n(vm, arg_count);
    if (len == 0.0) return basl_vec3_push_new(vm, 0.0, 0.0, 0.0, ci, error);
    return basl_vec3_push_new(vm, x/len, y/len, z/len, ci, error);
}

static basl_status_t basl_vec3_add(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double z1 = basl_vec_get_field(vm, base, 2U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    double z2 = basl_vec_get_field(vm, base + 1U, 2U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec3_push_new(vm, x1+x2, y1+y2, z1+z2, ci, error);
}

static basl_status_t basl_vec3_sub(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double z1 = basl_vec_get_field(vm, base, 2U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    double z2 = basl_vec_get_field(vm, base + 1U, 2U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec3_push_new(vm, x1-x2, y1-y2, z1-z2, ci, error);
}

static basl_status_t basl_vec3_scale(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_value_t sv = basl_vm_stack_get(vm, base + 1U);
    double s = basl_nanbox_decode_double(sv);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec3_push_new(vm, x*s, y*s, z*s, ci, error);
}

static basl_status_t basl_vec3_distance(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double dx = basl_vec_get_field(vm, base, 0U) - basl_vec_get_field(vm, base + 1U, 0U);
    double dy = basl_vec_get_field(vm, base, 1U) - basl_vec_get_field(vm, base + 1U, 1U);
    double dz = basl_vec_get_field(vm, base, 2U) - basl_vec_get_field(vm, base + 1U, 2U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, sqrt(dx*dx + dy*dy + dz*dz), error);
}

static const basl_native_class_field_t basl_vec3_fields[] = {
    { "x", 1U, BASL_TYPE_F64 },
    { "y", 1U, BASL_TYPE_F64 },
    { "z", 1U, BASL_TYPE_F64 },
};

static const basl_native_class_method_t basl_vec3_methods[] = {
    { "length",    6U, basl_vec3_length,     0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "dot",       3U, basl_vec3_dot,        1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "cross",     5U, basl_vec3_cross,      1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "normalize", 9U, basl_vec3_vnormalize, 0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "add",       3U, basl_vec3_add,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "sub",       3U, basl_vec3_sub,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "scale",     5U, basl_vec3_scale,      1U, basl_vec_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "distance",  8U, basl_vec3_distance,   1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
};

static const basl_native_class_t basl_math_classes[] = {
    {
        "Vec2", 4U,
        basl_vec2_fields, 2U,
        basl_vec2_methods, 7U,
        NULL
    },
    {
        "Vec3", 4U,
        basl_vec3_fields, 3U,
        basl_vec3_methods, 8U,
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
