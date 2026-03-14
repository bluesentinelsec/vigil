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

static basl_status_t basl_math_inverselerp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    double val = basl_math_pop_f64(vm);
    double b = basl_math_pop_f64(vm);
    double a = basl_math_pop_f64(vm);
    double range = b - a;
    return basl_math_push_f64(vm, (range == 0.0) ? 0.0 : (val - a) / range, error);
}

static basl_status_t basl_math_smoothstep(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    double x = basl_math_pop_f64(vm);
    double edge1 = basl_math_pop_f64(vm);
    double edge0 = basl_math_pop_f64(vm);
    double range = edge1 - edge0;
    double t = (range == 0.0) ? 0.0 : (x - edge0) / range;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return basl_math_push_f64(vm, t * t * (3.0 - 2.0 * t), error);
}

static basl_status_t basl_math_step(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    (void)arg_count;
    double x = basl_math_pop_f64(vm);
    double edge = basl_math_pop_f64(vm);
    return basl_math_push_f64(vm, (x >= edge) ? 1.0 : 0.0, error);
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
    MATH_FN2(step,      "step",     4U),
    MATH_FN3(clamp,     "clamp",    5U),
    MATH_FN3(lerp,      "lerp",     4U),
    MATH_FN3(inverselerp, "inverseLerp", 11U),
    MATH_FN3(smoothstep, "smoothstep", 10U),
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

static basl_status_t basl_vec2_lengthsqr(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, x * x + y * y, error);
}

static basl_status_t basl_vec2_negate(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec2_push_new(vm, -x, -y, ci, error);
}

static basl_status_t basl_vec2_vlerp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    basl_value_t tv = basl_vm_stack_get(vm, base + 2U);
    double t = basl_nanbox_decode_double(tv);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec2_push_new(vm, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, ci, error);
}

static basl_status_t basl_vec2_reflect(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double vx = basl_vec_get_field(vm, base, 0U);
    double vy = basl_vec_get_field(vm, base, 1U);
    double nx = basl_vec_get_field(vm, base + 1U, 0U);
    double ny = basl_vec_get_field(vm, base + 1U, 1U);
    double d2 = 2.0 * (vx * nx + vy * ny);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec2_push_new(vm, vx - d2 * nx, vy - d2 * ny, ci, error);
}

/* Helper: primitive field descriptor (object_kind=0, no class/element). */
#define BASL_PFIELD(n, nl, t) { n, nl, t, 0, NULL, 0U, 0 }

static const basl_native_class_field_t basl_vec2_fields[] = {
    BASL_PFIELD("x", 1U, BASL_TYPE_F64),
    BASL_PFIELD("y", 1U, BASL_TYPE_F64),
};

static const int basl_vec_obj_params[] = { BASL_TYPE_OBJECT };
static const int basl_vec_f64_params[] = { BASL_TYPE_F64 };
static const int basl_vec_obj_f64_params[] = { BASL_TYPE_OBJECT, BASL_TYPE_F64 };

static const basl_native_class_method_t basl_vec2_methods[] = {
    { "length",    6U, basl_vec2_length,     0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "lengthSqr", 9U, basl_vec2_lengthsqr, 0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "dot",       3U, basl_vec2_dot,        1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "distance",  8U, basl_vec2_distance,   1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "normalize", 9U, basl_vec2_vnormalize, 0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "negate",    6U, basl_vec2_negate,     0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "add",       3U, basl_vec2_add,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "sub",       3U, basl_vec2_sub,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "scale",     5U, basl_vec2_scale,      1U, basl_vec_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "lerp",      4U, basl_vec2_vlerp,      2U, basl_vec_obj_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "reflect",   7U, basl_vec2_reflect,    1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
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

static basl_status_t basl_vec3_lengthsqr(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, x*x + y*y + z*z, error);
}

static basl_status_t basl_vec3_negate(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec3_push_new(vm, -x, -y, -z, ci, error);
}

static basl_status_t basl_vec3_vlerp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double z1 = basl_vec_get_field(vm, base, 2U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    double z2 = basl_vec_get_field(vm, base + 1U, 2U);
    basl_value_t tv = basl_vm_stack_get(vm, base + 2U);
    double t = basl_nanbox_decode_double(tv);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec3_push_new(vm,
        x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, z1 + (z2 - z1) * t,
        ci, error);
}

static basl_status_t basl_vec3_reflect(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double vx = basl_vec_get_field(vm, base, 0U);
    double vy = basl_vec_get_field(vm, base, 1U);
    double vz = basl_vec_get_field(vm, base, 2U);
    double nx = basl_vec_get_field(vm, base + 1U, 0U);
    double ny = basl_vec_get_field(vm, base + 1U, 1U);
    double nz = basl_vec_get_field(vm, base + 1U, 2U);
    double d2 = 2.0 * (vx*nx + vy*ny + vz*nz);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec3_push_new(vm, vx - d2*nx, vy - d2*ny, vz - d2*nz, ci, error);
}

static basl_status_t basl_vec3_angle(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double z1 = basl_vec_get_field(vm, base, 2U);
    double x2 = basl_vec_get_field(vm, base + 1U, 0U);
    double y2 = basl_vec_get_field(vm, base + 1U, 1U);
    double z2 = basl_vec_get_field(vm, base + 1U, 2U);
    double dot = x1*x2 + y1*y2 + z1*z2;
    double len1 = sqrt(x1*x1 + y1*y1 + z1*z1);
    double len2 = sqrt(x2*x2 + y2*y2 + z2*z2);
    double denom = len1 * len2;
    double cosA;
    basl_vm_stack_pop_n(vm, arg_count);
    if (denom == 0.0) return basl_math_push_f64(vm, 0.0, error);
    cosA = dot / denom;
    if (cosA < -1.0) cosA = -1.0;
    if (cosA > 1.0) cosA = 1.0;
    return basl_math_push_f64(vm, acos(cosA), error);
}

static const basl_native_class_field_t basl_vec3_fields[] = {
    BASL_PFIELD("x", 1U, BASL_TYPE_F64),
    BASL_PFIELD("y", 1U, BASL_TYPE_F64),
    BASL_PFIELD("z", 1U, BASL_TYPE_F64),
};

static const basl_native_class_method_t basl_vec3_methods[] = {
    { "length",    6U, basl_vec3_length,     0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "lengthSqr", 9U, basl_vec3_lengthsqr, 0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "dot",       3U, basl_vec3_dot,        1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "distance",  8U, basl_vec3_distance,   1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "angle",     5U, basl_vec3_angle,      1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "cross",     5U, basl_vec3_cross,      1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "normalize", 9U, basl_vec3_vnormalize, 0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "negate",    6U, basl_vec3_negate,     0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "add",       3U, basl_vec3_add,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "sub",       3U, basl_vec3_sub,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "scale",     5U, basl_vec3_scale,      1U, basl_vec_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "lerp",      4U, basl_vec3_vlerp,      2U, basl_vec_obj_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "reflect",   7U, basl_vec3_reflect,    1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
};

/* ── Vec4 class ──────────────────────────────────────────────────── */

static basl_status_t basl_vec4_push_new(
    basl_vm_t *vm, double x, double y, double z, double w,
    size_t class_index, basl_error_t *error
) {
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_value_t fields[4];
    basl_object_t *inst;
    basl_value_t result;
    basl_status_t s;
    fields[0] = basl_nanbox_encode_double(x);
    fields[1] = basl_nanbox_encode_double(y);
    fields[2] = basl_nanbox_encode_double(z);
    fields[3] = basl_nanbox_encode_double(w);
    s = basl_instance_object_new(rt, class_index, fields, 4U, &inst, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_init_object(&result, &inst);
    s = basl_vm_stack_push(vm, &result, error);
    basl_value_release(&result);
    return s;
}

static basl_status_t basl_vec4_length(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, sqrt(x*x + y*y + z*z + w*w), error);
}

static basl_status_t basl_vec4_lengthsqr(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, x*x + y*y + z*z + w*w, error);
}

static basl_status_t basl_vec4_dot(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double r = basl_vec_get_field(vm, base, 0U) * basl_vec_get_field(vm, base+1U, 0U)
             + basl_vec_get_field(vm, base, 1U) * basl_vec_get_field(vm, base+1U, 1U)
             + basl_vec_get_field(vm, base, 2U) * basl_vec_get_field(vm, base+1U, 2U)
             + basl_vec_get_field(vm, base, 3U) * basl_vec_get_field(vm, base+1U, 3U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, r, error);
}

static basl_status_t basl_vec4_distance(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double dx = basl_vec_get_field(vm, base, 0U) - basl_vec_get_field(vm, base+1U, 0U);
    double dy = basl_vec_get_field(vm, base, 1U) - basl_vec_get_field(vm, base+1U, 1U);
    double dz = basl_vec_get_field(vm, base, 2U) - basl_vec_get_field(vm, base+1U, 2U);
    double dw = basl_vec_get_field(vm, base, 3U) - basl_vec_get_field(vm, base+1U, 3U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, sqrt(dx*dx + dy*dy + dz*dz + dw*dw), error);
}

static basl_status_t basl_vec4_vnormalize(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    size_t ci = basl_vec_self_class(vm, base);
    double len = sqrt(x*x + y*y + z*z + w*w);
    basl_vm_stack_pop_n(vm, arg_count);
    if (len == 0.0) return basl_vec4_push_new(vm, 0, 0, 0, 0, ci, error);
    return basl_vec4_push_new(vm, x/len, y/len, z/len, w/len, ci, error);
}

static basl_status_t basl_vec4_negate(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t ci = basl_vec_self_class(vm, base);
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec4_push_new(vm, -x, -y, -z, -w, ci, error);
}

static basl_status_t basl_vec4_add(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t ci = basl_vec_self_class(vm, base);
    double x = basl_vec_get_field(vm, base, 0U) + basl_vec_get_field(vm, base+1U, 0U);
    double y = basl_vec_get_field(vm, base, 1U) + basl_vec_get_field(vm, base+1U, 1U);
    double z = basl_vec_get_field(vm, base, 2U) + basl_vec_get_field(vm, base+1U, 2U);
    double w = basl_vec_get_field(vm, base, 3U) + basl_vec_get_field(vm, base+1U, 3U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec4_push_new(vm, x, y, z, w, ci, error);
}

static basl_status_t basl_vec4_sub(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t ci = basl_vec_self_class(vm, base);
    double x = basl_vec_get_field(vm, base, 0U) - basl_vec_get_field(vm, base+1U, 0U);
    double y = basl_vec_get_field(vm, base, 1U) - basl_vec_get_field(vm, base+1U, 1U);
    double z = basl_vec_get_field(vm, base, 2U) - basl_vec_get_field(vm, base+1U, 2U);
    double w = basl_vec_get_field(vm, base, 3U) - basl_vec_get_field(vm, base+1U, 3U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec4_push_new(vm, x, y, z, w, ci, error);
}

static basl_status_t basl_vec4_scale(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t ci = basl_vec_self_class(vm, base);
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    basl_value_t sv = basl_vm_stack_get(vm, base + 1U);
    double s = basl_nanbox_decode_double(sv);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec4_push_new(vm, x*s, y*s, z*s, w*s, ci, error);
}

static basl_status_t basl_vec4_vlerp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t ci = basl_vec_self_class(vm, base);
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double z1 = basl_vec_get_field(vm, base, 2U);
    double w1 = basl_vec_get_field(vm, base, 3U);
    double x2 = basl_vec_get_field(vm, base+1U, 0U);
    double y2 = basl_vec_get_field(vm, base+1U, 1U);
    double z2 = basl_vec_get_field(vm, base+1U, 2U);
    double w2 = basl_vec_get_field(vm, base+1U, 3U);
    basl_value_t tv = basl_vm_stack_get(vm, base + 2U);
    double t = basl_nanbox_decode_double(tv);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_vec4_push_new(vm,
        x1+(x2-x1)*t, y1+(y2-y1)*t, z1+(z2-z1)*t, w1+(w2-w1)*t, ci, error);
}

static const basl_native_class_field_t basl_vec4_fields[] = {
    BASL_PFIELD("x", 1U, BASL_TYPE_F64),
    BASL_PFIELD("y", 1U, BASL_TYPE_F64),
    BASL_PFIELD("z", 1U, BASL_TYPE_F64),
    BASL_PFIELD("w", 1U, BASL_TYPE_F64),
};

static const basl_native_class_method_t basl_vec4_methods[] = {
    { "length",    6U, basl_vec4_length,     0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "lengthSqr", 9U, basl_vec4_lengthsqr, 0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "dot",       3U, basl_vec4_dot,        1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "distance",  8U, basl_vec4_distance,   1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "normalize", 9U, basl_vec4_vnormalize, 0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "negate",    6U, basl_vec4_negate,     0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "add",       3U, basl_vec4_add,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "sub",       3U, basl_vec4_sub,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "scale",     5U, basl_vec4_scale,      1U, basl_vec_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "lerp",      4U, basl_vec4_vlerp,      2U, basl_vec_obj_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
};

/* ── Quaternion class ─────────────────────────────────────────────── */

/*
 * Quaternion stores (x, y, z, w) where w is the scalar part.
 * Convention: q = w + xi + yj + zk.
 * Identity quaternion: (0, 0, 0, 1).
 */

/* Reuse basl_vec4_push_new for quaternion — same 4-field layout. */
#define basl_quat_push_new basl_vec4_push_new

static basl_status_t basl_quat_length(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, sqrt(x*x + y*y + z*z + w*w), error);
}

static basl_status_t basl_quat_vnormalize(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    size_t ci = basl_vec_self_class(vm, base);
    double len = sqrt(x*x + y*y + z*z + w*w);
    basl_vm_stack_pop_n(vm, arg_count);
    if (len == 0.0) return basl_quat_push_new(vm, 0, 0, 0, 1, ci, error);
    return basl_quat_push_new(vm, x/len, y/len, z/len, w/len, ci, error);
}

static basl_status_t basl_quat_conjugate(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_quat_push_new(vm, -x, -y, -z, w, ci, error);
}

static basl_status_t basl_quat_inverse(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    size_t ci = basl_vec_self_class(vm, base);
    double lsq = x*x + y*y + z*z + w*w;
    basl_vm_stack_pop_n(vm, arg_count);
    if (lsq == 0.0) return basl_quat_push_new(vm, 0, 0, 0, 1, ci, error);
    return basl_quat_push_new(vm, -x/lsq, -y/lsq, -z/lsq, w/lsq, ci, error);
}

/* Hamilton product: q1 * q2 */
static basl_status_t basl_quat_multiply(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double z1 = basl_vec_get_field(vm, base, 2U);
    double w1 = basl_vec_get_field(vm, base, 3U);
    double x2 = basl_vec_get_field(vm, base+1U, 0U);
    double y2 = basl_vec_get_field(vm, base+1U, 1U);
    double z2 = basl_vec_get_field(vm, base+1U, 2U);
    double w2 = basl_vec_get_field(vm, base+1U, 3U);
    size_t ci = basl_vec_self_class(vm, base);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_quat_push_new(vm,
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2,
        w1*w2 - x1*x2 - y1*y2 - z1*z2,
        ci, error);
}

static basl_status_t basl_quat_dot(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double r = basl_vec_get_field(vm, base, 0U) * basl_vec_get_field(vm, base+1U, 0U)
             + basl_vec_get_field(vm, base, 1U) * basl_vec_get_field(vm, base+1U, 1U)
             + basl_vec_get_field(vm, base, 2U) * basl_vec_get_field(vm, base+1U, 2U)
             + basl_vec_get_field(vm, base, 3U) * basl_vec_get_field(vm, base+1U, 3U);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, r, error);
}

/* Spherical linear interpolation. */
static basl_status_t basl_quat_slerp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x1 = basl_vec_get_field(vm, base, 0U);
    double y1 = basl_vec_get_field(vm, base, 1U);
    double z1 = basl_vec_get_field(vm, base, 2U);
    double w1 = basl_vec_get_field(vm, base, 3U);
    double x2 = basl_vec_get_field(vm, base+1U, 0U);
    double y2 = basl_vec_get_field(vm, base+1U, 1U);
    double z2 = basl_vec_get_field(vm, base+1U, 2U);
    double w2 = basl_vec_get_field(vm, base+1U, 3U);
    basl_value_t tv = basl_vm_stack_get(vm, base + 2U);
    double t = basl_nanbox_decode_double(tv);
    size_t ci = basl_vec_self_class(vm, base);
    double cosHalf = x1*x2 + y1*y2 + z1*z2 + w1*w2;
    double s1, s2, halfAngle, sinHalf;
    basl_vm_stack_pop_n(vm, arg_count);
    /* Take shortest path. */
    if (cosHalf < 0.0) {
        x2 = -x2; y2 = -y2; z2 = -z2; w2 = -w2;
        cosHalf = -cosHalf;
    }
    if (cosHalf > 0.9995) {
        /* Nearly identical — use linear interpolation. */
        s1 = 1.0 - t; s2 = t;
    } else {
        halfAngle = acos(cosHalf);
        sinHalf = sin(halfAngle);
        s1 = sin((1.0 - t) * halfAngle) / sinHalf;
        s2 = sin(t * halfAngle) / sinHalf;
    }
    return basl_quat_push_new(vm,
        s1*x1 + s2*x2, s1*y1 + s2*y2, s1*z1 + s2*z2, s1*w1 + s2*w2,
        ci, error);
}

/* Create quaternion from axis (Vec3-like, but passed as Quat fields) and angle. */
static basl_status_t basl_quat_from_axis_angle(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* self is ignored (called on any Quat instance as factory pattern).
     * Args: axis (object with x,y,z), angle (f64). */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t ci = basl_vec_self_class(vm, base);
    double ax = basl_vec_get_field(vm, base + 1U, 0U);
    double ay = basl_vec_get_field(vm, base + 1U, 1U);
    double az = basl_vec_get_field(vm, base + 1U, 2U);
    basl_value_t av = basl_vm_stack_get(vm, base + 2U);
    double angle = basl_nanbox_decode_double(av);
    double half = angle * 0.5;
    double s = sin(half);
    double len = sqrt(ax*ax + ay*ay + az*az);
    basl_vm_stack_pop_n(vm, arg_count);
    if (len != 0.0) { ax /= len; ay /= len; az /= len; }
    return basl_quat_push_new(vm, ax*s, ay*s, az*s, cos(half), ci, error);
}

/* Convert to Euler angles (pitch, yaw, roll) in radians.
 * Returns a Vec3 — but we can't reference Vec3's class_index here,
 * so we return the angles as x, y, z of a new Quaternion (w=0).
 * The user reads .x (pitch), .y (yaw), .z (roll). */
static basl_status_t basl_quat_to_euler(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    double x = basl_vec_get_field(vm, base, 0U);
    double y = basl_vec_get_field(vm, base, 1U);
    double z = basl_vec_get_field(vm, base, 2U);
    double w = basl_vec_get_field(vm, base, 3U);
    size_t ci = basl_vec_self_class(vm, base);
    double sinp, pitch, yaw, roll;
    basl_vm_stack_pop_n(vm, arg_count);
    /* pitch (x-axis rotation) */
    sinp = 2.0 * (w * x - y * z);
    if (sinp >= 1.0) pitch = 3.14159265358979323846 * 0.5;
    else if (sinp <= -1.0) pitch = -3.14159265358979323846 * 0.5;
    else pitch = asin(sinp);
    /* yaw (y-axis rotation) */
    yaw = atan2(2.0 * (w * y + x * z), 1.0 - 2.0 * (x * x + y * y));
    /* roll (z-axis rotation) */
    roll = atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (x * x + z * z));
    return basl_quat_push_new(vm, pitch, yaw, roll, 0.0, ci, error);
}

static const basl_native_class_field_t basl_quat_fields[] = {
    BASL_PFIELD("x", 1U, BASL_TYPE_F64),
    BASL_PFIELD("y", 1U, BASL_TYPE_F64),
    BASL_PFIELD("z", 1U, BASL_TYPE_F64),
    BASL_PFIELD("w", 1U, BASL_TYPE_F64),
};

static const basl_native_class_method_t basl_quat_methods[] = {
    { "length",        6U, basl_quat_length,          0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "dot",           3U, basl_quat_dot,             1U, basl_vec_obj_params,
      BASL_TYPE_F64, 1U, NULL },
    { "normalize",     9U, basl_quat_vnormalize,      0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "conjugate",     9U, basl_quat_conjugate,       0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "inverse",       7U, basl_quat_inverse,         0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "multiply",      8U, basl_quat_multiply,        1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "slerp",         5U, basl_quat_slerp,           2U, basl_vec_obj_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "fromAxisAngle", 13U, basl_quat_from_axis_angle, 2U, basl_vec_obj_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "toEuler",       7U, basl_quat_to_euler,        0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
};

/* ── Mat4 class ──────────────────────────────────────────────────── */

/*
 * Mat4 stores a single field: data (array<f64>, 16 elements).
 * Column-major layout matching OpenGL/raylib convention:
 *   [m0  m4  m8  m12]
 *   [m1  m5  m9  m13]
 *   [m2  m6  m10 m14]
 *   [m3  m7  m11 m15]
 * Index = col * 4 + row.
 */

/* Helper: get the data array object from a Mat4 instance at stack slot. */
static basl_object_t *basl_mat4_get_data(basl_vm_t *vm, size_t slot) {
    basl_value_t self_val = basl_vm_stack_get(vm, slot);
    basl_object_t *inst = (basl_object_t *)basl_nanbox_decode_ptr(self_val);
    basl_value_t field;
    basl_instance_object_get_field(inst, 0U, &field);
    return (basl_object_t *)basl_nanbox_decode_ptr(field);
}

/* Helper: read f64 from mat4 data array at index. */
static double basl_mat4_read(basl_object_t *arr, size_t idx) {
    basl_value_t v;
    basl_array_object_get(arr, idx, &v);
    return basl_nanbox_decode_double(v);
}

/* Helper: push a new Mat4 with 16 doubles. */
static basl_status_t basl_mat4_push_new(
    basl_vm_t *vm, const double m[16], size_t class_index,
    basl_error_t *error
) {
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_value_t items[16];
    basl_object_t *arr;
    basl_value_t arr_val;
    basl_value_t inst_fields[1];
    basl_object_t *inst;
    basl_value_t result;
    basl_status_t s;
    size_t i;
    for (i = 0; i < 16; i++) items[i] = basl_nanbox_encode_double(m[i]);
    s = basl_array_object_new(rt, items, 16U, &arr, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_init_object(&arr_val, &arr);
    inst_fields[0] = arr_val;
    s = basl_instance_object_new(rt, class_index, inst_fields, 1U, &inst, error);
    basl_value_release(&arr_val);
    if (s != BASL_STATUS_OK) return s;
    basl_value_init_object(&result, &inst);
    s = basl_vm_stack_push(vm, &result, error);
    basl_value_release(&result);
    return s;
}

/* identity(): returns identity matrix */
static basl_status_t basl_mat4_identity(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t ci = basl_vec_self_class(vm, base);
    double m[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_mat4_push_new(vm, m, ci, error);
}

/* get(row, col) -> f64 */
static basl_status_t basl_mat4_get(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *arr = basl_mat4_get_data(vm, base);
    basl_value_t rv = basl_vm_stack_get(vm, base + 1U);
    basl_value_t cv = basl_vm_stack_get(vm, base + 2U);
    int row = (int)basl_nanbox_decode_i32(rv);
    int col = (int)basl_nanbox_decode_i32(cv);
    double val = basl_mat4_read(arr, (size_t)(col * 4 + row));
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, val, error);
}

/* set(row, col, val) -> Mat4 (returns new matrix) */
static basl_status_t basl_mat4_set(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *arr = basl_mat4_get_data(vm, base);
    size_t ci = basl_vec_self_class(vm, base);
    basl_value_t rv = basl_vm_stack_get(vm, base + 1U);
    basl_value_t cv = basl_vm_stack_get(vm, base + 2U);
    basl_value_t vv = basl_vm_stack_get(vm, base + 3U);
    int row = (int)basl_nanbox_decode_i32(rv);
    int col = (int)basl_nanbox_decode_i32(cv);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++) m[i] = basl_mat4_read(arr, i);
    m[col * 4 + row] = basl_nanbox_decode_double(vv);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_mat4_push_new(vm, m, ci, error);
}

/* multiply(Mat4) -> Mat4 */
static basl_status_t basl_mat4_multiply(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *a = basl_mat4_get_data(vm, base);
    basl_object_t *b = basl_mat4_get_data(vm, base + 1U);
    size_t ci = basl_vec_self_class(vm, base);
    double m[16];
    int c, r, k;
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            double sum = 0.0;
            for (k = 0; k < 4; k++) {
                sum += basl_mat4_read(a, k * 4 + r) *
                       basl_mat4_read(b, c * 4 + k);
            }
            m[c * 4 + r] = sum;
        }
    }
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_mat4_push_new(vm, m, ci, error);
}

/* transpose() -> Mat4 */
static basl_status_t basl_mat4_transpose(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *a = basl_mat4_get_data(vm, base);
    size_t ci = basl_vec_self_class(vm, base);
    double m[16];
    int c, r;
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++)
            m[c * 4 + r] = basl_mat4_read(a, r * 4 + c);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_mat4_push_new(vm, m, ci, error);
}

/* determinant() -> f64 */
static basl_status_t basl_mat4_determinant(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *a = basl_mat4_get_data(vm, base);
    double m[16];
    double det;
    size_t i;
    for (i = 0; i < 16; i++) m[i] = basl_mat4_read(a, i);
    det = m[0]*(m[5]*(m[10]*m[15]-m[11]*m[14])-m[9]*(m[6]*m[15]-m[7]*m[14])+m[13]*(m[6]*m[11]-m[7]*m[10]))
        - m[4]*(m[1]*(m[10]*m[15]-m[11]*m[14])-m[9]*(m[2]*m[15]-m[3]*m[14])+m[13]*(m[2]*m[11]-m[3]*m[10]))
        + m[8]*(m[1]*(m[6]*m[15]-m[7]*m[14])-m[5]*(m[2]*m[15]-m[3]*m[14])+m[13]*(m[2]*m[7]-m[3]*m[6]))
        - m[12]*(m[1]*(m[6]*m[11]-m[7]*m[10])-m[5]*(m[2]*m[11]-m[3]*m[10])+m[9]*(m[2]*m[7]-m[3]*m[6]));
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_math_push_f64(vm, det, error);
}

/* add(Mat4) -> Mat4 */
static basl_status_t basl_mat4_add(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *a = basl_mat4_get_data(vm, base);
    basl_object_t *b = basl_mat4_get_data(vm, base + 1U);
    size_t ci = basl_vec_self_class(vm, base);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++) m[i] = basl_mat4_read(a, i) + basl_mat4_read(b, i);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_mat4_push_new(vm, m, ci, error);
}

/* scale(f64) -> Mat4 (scalar multiply) */
static basl_status_t basl_mat4_scale(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *a = basl_mat4_get_data(vm, base);
    size_t ci = basl_vec_self_class(vm, base);
    basl_value_t sv = basl_vm_stack_get(vm, base + 1U);
    double s = basl_nanbox_decode_double(sv);
    double m[16];
    size_t i;
    for (i = 0; i < 16; i++) m[i] = basl_mat4_read(a, i) * s;
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_mat4_push_new(vm, m, ci, error);
}

static const int basl_mat4_i32i32_params[] = { BASL_TYPE_I32, BASL_TYPE_I32 };
static const int basl_mat4_i32i32f64_params[] = {
    BASL_TYPE_I32, BASL_TYPE_I32, BASL_TYPE_F64
};

static const basl_native_class_field_t basl_mat4_fields[] = {
    { "data", 4U, BASL_TYPE_OBJECT,
      BASL_NATIVE_FIELD_ARRAY, NULL, 0U, BASL_TYPE_F64 },
};

static const basl_native_class_method_t basl_mat4_methods[] = {
    { "identity",    8U, basl_mat4_identity,    0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "get",         3U, basl_mat4_get,         2U, basl_mat4_i32i32_params,
      BASL_TYPE_F64, 1U, NULL },
    { "set",         3U, basl_mat4_set,         3U, basl_mat4_i32i32f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "multiply",    8U, basl_mat4_multiply,    1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "transpose",   9U, basl_mat4_transpose,   0U, NULL,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "determinant", 11U, basl_mat4_determinant, 0U, NULL,
      BASL_TYPE_F64, 1U, NULL },
    { "add",         3U, basl_mat4_add,         1U, basl_vec_obj_params,
      BASL_TYPE_OBJECT, 1U, NULL },
    { "scale",       5U, basl_mat4_scale,       1U, basl_vec_f64_params,
      BASL_TYPE_OBJECT, 1U, NULL },
};

static const basl_native_class_t basl_math_classes[] = {
    {
        "Vec2", 4U,
        basl_vec2_fields, 2U,
        basl_vec2_methods, 11U,
        NULL
    },
    {
        "Vec3", 4U,
        basl_vec3_fields, 3U,
        basl_vec3_methods, 13U,
        NULL
    },
    {
        "Vec4", 4U,
        basl_vec4_fields, 4U,
        basl_vec4_methods, 10U,
        NULL
    },
    {
        "Quaternion", 10U,
        basl_quat_fields, 4U,
        basl_quat_methods, 9U,
        NULL
    },
    {
        "Mat4", 4U,
        basl_mat4_fields, 1U,
        basl_mat4_methods, 8U,
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
