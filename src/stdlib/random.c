/* BASL standard library: random module.
 *
 * Provides random number generation using xorshift128+ for quality
 * and portability. Seeded from time by default.
 */
#include <stdint.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"

/* ── xorshift128+ state ──────────────────────────────────────────── */

static uint64_t rng_state[2] = {0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL};

static uint64_t xorshift128plus(void) {
    uint64_t s1 = rng_state[0];
    uint64_t s0 = rng_state[1];
    uint64_t result = s0 + s1;
    rng_state[0] = s0;
    s1 ^= s1 << 23;
    rng_state[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
    return result;
}

/* ── random.seed(n: i32) ─────────────────────────────────────────── */

static basl_status_t basl_random_seed(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    basl_value_t v;
    int32_t seed;
    (void)arg_count;
    (void)error;

    v = basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1U);
    basl_vm_stack_pop_n(vm, 1U);
    seed = basl_nanbox_decode_i32(v);

    rng_state[0] = (uint64_t)(uint32_t)seed;
    rng_state[1] = (uint64_t)(uint32_t)seed ^ 0x6a09e667bb67ae85ULL;
    /* Warm up */
    (void)xorshift128plus();
    (void)xorshift128plus();

    return BASL_STATUS_OK;
}

/* ── random.i64() -> i64 ─────────────────────────────────────────── */

static basl_status_t basl_random_i64(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    basl_value_t val;
    (void)arg_count;

    val = basl_nanbox_encode_int((int64_t)xorshift128plus());
    return basl_vm_stack_push(vm, &val, error);
}

/* ── random.i32() -> i32 ─────────────────────────────────────────── */

static basl_status_t basl_random_i32(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    basl_value_t val;
    (void)arg_count;

    val = basl_nanbox_encode_i32((int32_t)(xorshift128plus() >> 32));
    return basl_vm_stack_push(vm, &val, error);
}

/* ── random.f64() -> f64 in [0, 1) ───────────────────────────────── */

static basl_status_t basl_random_f64(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    basl_value_t val;
    double d;
    (void)arg_count;

    /* Use upper 53 bits for full double precision */
    d = (double)(xorshift128plus() >> 11) * (1.0 / 9007199254740992.0);
    val = basl_nanbox_encode_double(d);
    return basl_vm_stack_push(vm, &val, error);
}

/* ── random.range(min: i32, max: i32) -> i32 ─────────────────────── */

static basl_status_t basl_random_range(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    basl_value_t v;
    int32_t min_val, max_val, result;
    uint32_t range;
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    (void)arg_count;

    v = basl_vm_stack_get(vm, base);
    min_val = basl_nanbox_decode_i32(v);
    v = basl_vm_stack_get(vm, base + 1);
    max_val = basl_nanbox_decode_i32(v);
    basl_vm_stack_pop_n(vm, 2U);

    if (max_val <= min_val) {
        result = min_val;
    } else {
        range = (uint32_t)(max_val - min_val);
        result = min_val + (int32_t)((uint32_t)(xorshift128plus() >> 32) % range);
    }

    v = basl_nanbox_encode_i32(result);
    return basl_vm_stack_push(vm, &v, error);
}

/* ── Module definition ───────────────────────────────────────────── */

static const int seed_params[] = {BASL_TYPE_I32};
static const int range_params[] = {BASL_TYPE_I32, BASL_TYPE_I32};

static const basl_native_module_function_t basl_random_functions[] = {
    {"seed", 4U, basl_random_seed, 1U, seed_params, BASL_TYPE_VOID, 0U, NULL, 0},
    {"i64", 3U, basl_random_i64, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0},
    {"i32", 3U, basl_random_i32, 0U, NULL, BASL_TYPE_I32, 1U, NULL, 0},
    {"f64", 3U, basl_random_f64, 0U, NULL, BASL_TYPE_F64, 1U, NULL, 0},
    {"range", 5U, basl_random_range, 2U, range_params, BASL_TYPE_I32, 1U, NULL, 0},
};

#define RANDOM_FUNCTION_COUNT \
    (sizeof(basl_random_functions) / sizeof(basl_random_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_random = {
    "random", 6U,
    basl_random_functions,
    RANDOM_FUNCTION_COUNT,
    NULL, 0U
};
