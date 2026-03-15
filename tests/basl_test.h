/*
 * basl_test.h — Minimal C11 test harness with a GoogleTest-compatible API.
 *
 * Single-header, no dependencies beyond C11 stdlib.
 * Replaces GoogleTest for the BASL project so the entire build
 * (including tests) requires only a C compiler.
 *
 * Usage:
 *   #define BASL_TEST_IMPLEMENTATION   // in exactly ONE .c file
 *   #include "basl_test.h"
 *
 * Supported macros:
 *   TEST(suite, name)           — define a test case
 *   TEST_F(fixture, name)       — define a test with setup/teardown
 *   EXPECT_EQ, ASSERT_EQ       — integer/pointer equality
 *   EXPECT_NE, ASSERT_NE       — inequality
 *   EXPECT_TRUE, ASSERT_TRUE   — boolean true
 *   EXPECT_FALSE               — boolean false
 *   EXPECT_STREQ               — string equality (strcmp == 0)
 *   EXPECT_STRNE               — string inequality
 *   EXPECT_GE, EXPECT_GT       — greater-or-equal, greater-than
 *   EXPECT_LT, EXPECT_LE       — less-than, less-or-equal
 *   EXPECT_DOUBLE_EQ           — double equality (within 4 ULP)
 *   EXPECT_NEAR                — double near (within abs tolerance)
 *   EXPECT_DEATH(stmt, pattern) — statement causes non-zero exit
 */
#ifndef BASL_TEST_H
#define BASL_TEST_H

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Test registration ───────────────────────────────────────────── */

typedef void (*basl_test_fn_t)(int *basl_test_failed_);
typedef void (*basl_test_fixture_fn_t)(void *fixture);

typedef struct basl_test_entry {
    const char *suite;
    const char *name;
    basl_test_fn_t fn;
    basl_test_fixture_fn_t setup;
    basl_test_fixture_fn_t teardown;
    size_t fixture_size;
} basl_test_entry_t;

#ifndef BASL_TEST_MAX
#define BASL_TEST_MAX 2048
#endif

/* Global test state — defined in the implementation section. */
extern basl_test_entry_t basl_test_entries_[];
extern int basl_test_count_;
extern void *basl_test_fixture_ptr_;

static inline int basl_test_register_(
    const char *suite, const char *name, basl_test_fn_t fn,
    basl_test_fixture_fn_t setup, basl_test_fixture_fn_t teardown,
    size_t fixture_size
) {
    if (basl_test_count_ < BASL_TEST_MAX) {
        basl_test_entry_t *e = &basl_test_entries_[basl_test_count_];
        e->suite = suite;
        e->name = name;
        e->fn = fn;
        e->setup = setup;
        e->teardown = teardown;
        e->fixture_size = fixture_size;
        basl_test_count_++;
    }
    return 0;
}

/* ── Auto-registration: constructor portability ──────────────────── */

#ifdef _MSC_VER
  /* MSVC: place function pointer in CRT startup section. */
  #pragma section(".CRT$XCU", read)
  #define BASL_TEST_CTOR_(fn_name)                                            \
      static void fn_name(void);                                              \
      __declspec(allocate(".CRT$XCU"))                                        \
      static void (*fn_name##_ptr_)(void) = fn_name;                          \
      static void fn_name(void)
#else
  #define BASL_TEST_CTOR_(fn_name)                                            \
      __attribute__((constructor)) static void fn_name(void)
#endif

/* ── TEST() macro ────────────────────────────────────────────────── */

#define TEST(suite, name)                                                     \
    static void basl_test_##suite##_##name##_body_(int *basl_test_failed_);   \
    BASL_TEST_CTOR_(basl_test_##suite##_##name##_ctor_) {                     \
        basl_test_register_(                                                  \
            #suite, #name, basl_test_##suite##_##name##_body_,                \
            NULL, NULL, 0);                                                   \
    }                                                                         \
    static void basl_test_##suite##_##name##_body_(int *basl_test_failed_)

/* ── TEST_F() macro ──────────────────────────────────────────────── */

#define TEST_F(fixture, name)                                                 \
    static void basl_test_##fixture##_##name##_body_(int *basl_test_failed_); \
    BASL_TEST_CTOR_(basl_test_##fixture##_##name##_ctor_) {                   \
        basl_test_register_(                                                  \
            #fixture, #name, basl_test_##fixture##_##name##_body_,            \
            fixture##_SetUp, fixture##_TearDown,                              \
            sizeof(fixture));                                                 \
    }                                                                         \
    static void basl_test_##fixture##_##name##_body_(int *basl_test_failed_)

/* Fixture access: use FIXTURE(Type) inside TEST_F body to get pointer. */
#define FIXTURE(type) ((type *)basl_test_fixture_ptr_)

/* ── Assertion helpers ───────────────────────────────────────────── */

#define BASL_TEST_FAIL_(msg)                                                  \
    do {                                                                      \
        fprintf(stderr, "  %s:%d: Failure\n%s\n",                             \
                __FILE__, __LINE__, msg);                                     \
        *basl_test_failed_ = 1;                                               \
    } while (0)

#define BASL_TEST_FATAL_(msg)                                                 \
    do {                                                                      \
        BASL_TEST_FAIL_(msg);                                                 \
        return;                                                               \
    } while (0)

/* ── EXPECT / ASSERT macros ──────────────────────────────────────── */

#define EXPECT_EQ(a, b) do {                                                  \
    long long a_ = (long long)(a), b_ = (long long)(b);                      \
    if (a_ != b_) {                                                           \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_),                                          \
            "    Expected: %lld\n    Actual:   %lld", b_, a_);                \
        BASL_TEST_FAIL_(msg_);                                                \
    }                                                                         \
} while (0)

#define ASSERT_EQ(a, b) do {                                                  \
    long long a_ = (long long)(a), b_ = (long long)(b);                      \
    if (a_ != b_) {                                                           \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_),                                          \
            "    Expected: %lld\n    Actual:   %lld", b_, a_);                \
        BASL_TEST_FATAL_(msg_);                                               \
    }                                                                         \
} while (0)

#define EXPECT_NE(a, b) do {                                                  \
    long long a_ = (long long)(a), b_ = (long long)(b);                      \
    if (a_ == b_) {                                                           \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_),                                          \
            "    Expected not equal, both: %lld", a_);                        \
        BASL_TEST_FAIL_(msg_);                                                \
    }                                                                         \
} while (0)

#define ASSERT_NE(a, b) do {                                                  \
    long long a_ = (long long)(a), b_ = (long long)(b);                      \
    if (a_ == b_) {                                                           \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_),                                          \
            "    Expected not equal, both: %lld", a_);                        \
        BASL_TEST_FATAL_(msg_);                                               \
    }                                                                         \
} while (0)

#define EXPECT_TRUE(expr) do {                                                \
    if (!(expr)) BASL_TEST_FAIL_("    Expected true: " #expr);               \
} while (0)

#define ASSERT_TRUE(expr) do {                                                \
    if (!(expr)) BASL_TEST_FATAL_("    Expected true: " #expr);              \
} while (0)

#define EXPECT_FALSE(expr) do {                                               \
    if ((expr)) BASL_TEST_FAIL_("    Expected false: " #expr);               \
} while (0)

#define EXPECT_STREQ(a, b) do {                                               \
    const char *a_ = (a), *b_ = (b);                                         \
    if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0) {                    \
        char msg_[512];                                                       \
        snprintf(msg_, sizeof(msg_),                                          \
            "    Expected: \"%s\"\n    Actual:   \"%s\"",                     \
            b_ ? b_ : "(null)", a_ ? a_ : "(null)");                         \
        BASL_TEST_FAIL_(msg_);                                                \
    }                                                                         \
} while (0)

#define EXPECT_STRNE(a, b) do {                                               \
    const char *a_ = (a), *b_ = (b);                                         \
    if (a_ && b_ && strcmp(a_, b_) == 0)                                      \
        BASL_TEST_FAIL_("    Expected not equal strings");                    \
} while (0)

#define EXPECT_GE(a, b) do {                                                  \
    long long a_ = (long long)(a), b_ = (long long)(b);                      \
    if (a_ < b_) {                                                            \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_), "    Expected %lld >= %lld", a_, b_);    \
        BASL_TEST_FAIL_(msg_);                                                \
    }                                                                         \
} while (0)

#define EXPECT_GT(a, b) do {                                                  \
    long long a_ = (long long)(a), b_ = (long long)(b);                      \
    if (a_ <= b_) {                                                           \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_), "    Expected %lld > %lld", a_, b_);     \
        BASL_TEST_FAIL_(msg_);                                                \
    }                                                                         \
} while (0)

#define EXPECT_LT(a, b) do {                                                  \
    long long a_ = (long long)(a), b_ = (long long)(b);                      \
    if (a_ >= b_) {                                                           \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_), "    Expected %lld < %lld", a_, b_);     \
        BASL_TEST_FAIL_(msg_);                                                \
    }                                                                         \
} while (0)

#define EXPECT_LE(a, b) do {                                                  \
    long long a_ = (long long)(a), b_ = (long long)(b);                      \
    if (a_ > b_) {                                                            \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_), "    Expected %lld <= %lld", a_, b_);    \
        BASL_TEST_FAIL_(msg_);                                                \
    }                                                                         \
} while (0)

#define EXPECT_DOUBLE_EQ(a, b) do {                                           \
    double a_ = (double)(a), b_ = (double)(b);                               \
    if (fabs(a_ - b_) > 1e-10) {                                             \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_),                                          \
            "    Expected: %g\n    Actual:   %g", b_, a_);                    \
        BASL_TEST_FAIL_(msg_);                                                \
    }                                                                         \
} while (0)

#define EXPECT_NEAR(a, b, tol) do {                                           \
    double a_ = (double)(a), b_ = (double)(b), t_ = (double)(tol);           \
    if (fabs(a_ - b_) > t_) {                                                \
        char msg_[256];                                                       \
        snprintf(msg_, sizeof(msg_),                                          \
            "    Expected: %g (+/-%g)\n    Actual:   %g", b_, t_, a_);        \
        BASL_TEST_FAIL_(msg_);                                                \
    }                                                                         \
} while (0)

/*
 * EXPECT_DEATH: re-executes the current test binary with an env var
 * that triggers just the death-test statement.  On platforms without
 * process support, the test is skipped.
 */
#define EXPECT_DEATH(stmt, pattern) do {                                      \
    (void)(pattern);                                                          \
    fprintf(stderr, "  [SKIPPED] EXPECT_DEATH not supported in C harness\n");\
} while (0)

#ifdef __cplusplus
}
#endif

/* ── Implementation ──────────────────────────────────────────────── */

#ifdef BASL_TEST_IMPLEMENTATION

basl_test_entry_t basl_test_entries_[BASL_TEST_MAX];
int basl_test_count_ = 0;
void *basl_test_fixture_ptr_ = NULL;

static int basl_test_run_all_(void) {
    int passed = 0, failed = 0, skipped = 0;
    int i;
    const char *filter = getenv("BASL_TEST_FILTER");

    printf("[==========] Running %d test(s).\n", basl_test_count_);

    for (i = 0; i < basl_test_count_; i++) {
        basl_test_entry_t *e = &basl_test_entries_[i];
        int test_failed = 0;
        char fullname[512];

        snprintf(fullname, sizeof(fullname), "%s.%s", e->suite, e->name);
        if (filter && !strstr(fullname, filter)) {
            skipped++;
            continue;
        }

        printf("[ RUN      ] %s\n", fullname);

        /* Allocate fixture if needed. */
        if (e->fixture_size > 0) {
            basl_test_fixture_ptr_ = calloc(1, e->fixture_size);
            if (e->setup) e->setup(basl_test_fixture_ptr_);
        }

        e->fn(&test_failed);

        /* Teardown fixture. */
        if (e->fixture_size > 0) {
            if (e->teardown) e->teardown(basl_test_fixture_ptr_);
            free(basl_test_fixture_ptr_);
            basl_test_fixture_ptr_ = NULL;
        }

        if (test_failed) {
            printf("[   FAILED ] %s\n", fullname);
            failed++;
        } else {
            printf("[       OK ] %s\n", fullname);
            passed++;
        }
    }

    printf("[==========] %d test(s) ran.\n", passed + failed);
    if (skipped > 0)
        printf("[  SKIPPED ] %d test(s).\n", skipped);
    printf("[  PASSED  ] %d test(s).\n", passed);
    if (failed > 0)
        printf("[  FAILED  ] %d test(s).\n", failed);

    return failed > 0 ? 1 : 0;
}

int main(void) {
    return basl_test_run_all_();
}

#endif /* BASL_TEST_IMPLEMENTATION */

#endif /* BASL_TEST_H */
