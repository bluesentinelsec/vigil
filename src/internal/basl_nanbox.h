/*
 * NaN-boxing encoding for basl_value_t.
 *
 * Every BASL value is a single uint64_t.  IEEE 754 doubles are stored
 * as-is.  All other types are encoded in the quiet-NaN space.
 *
 * Bit layout:
 *
 *   Double:  any bit pattern where (val & QNAN) != QNAN.
 *            This includes all finite numbers, ±inf, ±0, and the
 *            canonical NaN (0x7FF8000000000000) since it lacks bit 50.
 *
 *   Tagged:  QNAN (bits 62..50 = 0x1FFF shifted) is our sentinel.
 *            QNAN = 0x7FFC000000000000.  Bit 50 is the discriminator —
 *            no standard FP operation sets it (canonical NaN has bit 51
 *            set but bit 50 clear).
 *
 *   Tag regions (bits 49..48 + sign bit):
 *
 *     QNAN | 1                = nil
 *     QNAN | 2                = false
 *     QNAN | 3                = true
 *     QNAN | (1 << 48) | p48  = inline signed integer
 *     QNAN | (2 << 48) | p48  = inline unsigned integer
 *     SIGN | QNAN | p48       = object pointer
 *     SIGN | QNAN | (1<<48) | p48 = boxed signed integer (bigint obj)
 *     SIGN | QNAN | (2<<48) | p48 = boxed unsigned integer (bigint obj)
 *
 *   Inline integer ranges:
 *     Signed:   -2^47 .. 2^47-1   (±140 trillion)
 *     Unsigned:  0 .. 2^48-1       (281 trillion)
 *
 *   Values outside these ranges are heap-boxed as bigint objects.
 *
 *   48-bit pointers: current x86-64 and AArch64 use at most 48 bits
 *   of virtual address space (with sign-extension to 64 bits on x86).
 *   We mask to 48 bits on encode and sign-extend on decode.
 */

#ifndef BASL_NANBOX_H
#define BASL_NANBOX_H

#include <stdint.h>
#include <string.h>

/* ── Bit constants ───────────────────────────────────────────────── */

#define BASL_NANBOX_QNAN         UINT64_C(0x7FFC000000000000)
#define BASL_NANBOX_SIGN         UINT64_C(0x8000000000000000)
#define BASL_NANBOX_PAYLOAD_MASK UINT64_C(0x0000FFFFFFFFFFFF)

/* Tag regions. */
#define BASL_NANBOX_TAG_INT      (BASL_NANBOX_QNAN | (UINT64_C(1) << 48))
#define BASL_NANBOX_TAG_UINT     (BASL_NANBOX_QNAN | (UINT64_C(2) << 48))
#define BASL_NANBOX_TAG_OBJ      (BASL_NANBOX_SIGN | BASL_NANBOX_QNAN)
#define BASL_NANBOX_TAG_BIGINT   (BASL_NANBOX_SIGN | BASL_NANBOX_QNAN | (UINT64_C(1) << 48))
#define BASL_NANBOX_TAG_BIGUINT  (BASL_NANBOX_SIGN | BASL_NANBOX_QNAN | (UINT64_C(2) << 48))
/* Upper 16 bits mask for tag comparison. */
#define BASL_NANBOX_TAG_MASK     UINT64_C(0xFFFF000000000000)

/* Singleton values. */
#define BASL_NANBOX_NIL          (BASL_NANBOX_QNAN | UINT64_C(1))
#define BASL_NANBOX_FALSE        (BASL_NANBOX_QNAN | UINT64_C(2))
#define BASL_NANBOX_TRUE         (BASL_NANBOX_QNAN | UINT64_C(3))

/* Inline integer limits. */
#define BASL_NANBOX_INT_MAX      INT64_C(140737488355327)    /* 2^47 - 1 */
#define BASL_NANBOX_INT_MIN      INT64_C(-140737488355328)   /* -2^47    */
#define BASL_NANBOX_UINT_MAX     UINT64_C(281474976710655)   /* 2^48 - 1 */

/* ── Type checks ─────────────────────────────────────────────────── */

static inline int basl_nanbox_is_double(uint64_t v) {
    return (v & BASL_NANBOX_QNAN) != BASL_NANBOX_QNAN;
}

static inline int basl_nanbox_is_nil(uint64_t v) {
    return v == BASL_NANBOX_NIL;
}

static inline int basl_nanbox_is_bool(uint64_t v) {
    return v == BASL_NANBOX_FALSE || v == BASL_NANBOX_TRUE;
}

static inline int basl_nanbox_is_object(uint64_t v) {
    return (v & BASL_NANBOX_TAG_MASK) == BASL_NANBOX_TAG_OBJ;
}

static inline int basl_nanbox_is_int_inline(uint64_t v) {
    return (v & BASL_NANBOX_TAG_MASK) == BASL_NANBOX_TAG_INT;
}

static inline int basl_nanbox_is_uint_inline(uint64_t v) {
    return (v & BASL_NANBOX_TAG_MASK) == BASL_NANBOX_TAG_UINT;
}

static inline int basl_nanbox_is_bigint(uint64_t v) {
    return (v & BASL_NANBOX_TAG_MASK) == BASL_NANBOX_TAG_BIGINT;
}

static inline int basl_nanbox_is_biguint(uint64_t v) {
    return (v & BASL_NANBOX_TAG_MASK) == BASL_NANBOX_TAG_BIGUINT;
}

static inline int basl_nanbox_is_int(uint64_t v) {
    return basl_nanbox_is_int_inline(v) || basl_nanbox_is_bigint(v);
}

static inline int basl_nanbox_is_uint(uint64_t v) {
    return basl_nanbox_is_uint_inline(v) || basl_nanbox_is_biguint(v);
}

/* Does this value hold a heap object (object, bigint, or biguint)? */
static inline int basl_nanbox_has_object(uint64_t v) {
    return (v & (BASL_NANBOX_SIGN | BASL_NANBOX_QNAN)) ==
           (BASL_NANBOX_SIGN | BASL_NANBOX_QNAN);
}

/* ── Encoding ────────────────────────────────────────────────────── */

static inline uint64_t basl_nanbox_encode_double(double d) {
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

static inline uint64_t basl_nanbox_from_bool(int b) {
    return b ? BASL_NANBOX_TRUE : BASL_NANBOX_FALSE;
}

static inline uint64_t basl_nanbox_encode_object(void *ptr) {
    return BASL_NANBOX_TAG_OBJ |
           ((uint64_t)(uintptr_t)ptr & BASL_NANBOX_PAYLOAD_MASK);
}

static inline uint64_t basl_nanbox_encode_int(int64_t v) {
    return BASL_NANBOX_TAG_INT | ((uint64_t)v & BASL_NANBOX_PAYLOAD_MASK);
}

static inline uint64_t basl_nanbox_encode_uint(uint64_t v) {
    return BASL_NANBOX_TAG_UINT | (v & BASL_NANBOX_PAYLOAD_MASK);
}

static inline uint64_t basl_nanbox_encode_bigint(void *ptr) {
    return BASL_NANBOX_TAG_BIGINT |
           ((uint64_t)(uintptr_t)ptr & BASL_NANBOX_PAYLOAD_MASK);
}

static inline uint64_t basl_nanbox_encode_biguint(void *ptr) {
    return BASL_NANBOX_TAG_BIGUINT |
           ((uint64_t)(uintptr_t)ptr & BASL_NANBOX_PAYLOAD_MASK);
}

/* ── Decoding ────────────────────────────────────────────────────── */

static inline double basl_nanbox_decode_double(uint64_t v) {
    double d;
    memcpy(&d, &v, sizeof(d));
    return d;
}

static inline int basl_nanbox_decode_bool(uint64_t v) {
    return v == BASL_NANBOX_TRUE;
}

static inline void *basl_nanbox_decode_ptr(uint64_t v) {
    return (void *)(uintptr_t)(v & BASL_NANBOX_PAYLOAD_MASK);
}

/* Sign-extend 48-bit payload to int64_t. */
static inline int64_t basl_nanbox_decode_int(uint64_t v) {
    uint64_t raw = v & BASL_NANBOX_PAYLOAD_MASK;
    if (raw & UINT64_C(0x0000800000000000)) {
        raw |= UINT64_C(0xFFFF000000000000);
    }
    return (int64_t)raw;
}

static inline uint64_t basl_nanbox_decode_uint(uint64_t v) {
    return v & BASL_NANBOX_PAYLOAD_MASK;
}

/* ── Inline range checks ─────────────────────────────────────────── */

static inline int basl_nanbox_int_fits_inline(int64_t v) {
    return v >= BASL_NANBOX_INT_MIN && v <= BASL_NANBOX_INT_MAX;
}

static inline int basl_nanbox_uint_fits_inline(uint64_t v) {
    return v <= BASL_NANBOX_UINT_MAX;
}

/* Fast i32 encode/decode — no sign-extension branch needed because
   i32 values always fit in the 48-bit payload. */
static inline uint64_t basl_nanbox_encode_i32(int32_t v) {
    return BASL_NANBOX_TAG_INT | ((uint64_t)(uint32_t)v & BASL_NANBOX_PAYLOAD_MASK);
}

static inline int32_t basl_nanbox_decode_i32(uint64_t v) {
    return (int32_t)(uint32_t)(v & 0xFFFFFFFFU);
}

#endif /* BASL_NANBOX_H */
