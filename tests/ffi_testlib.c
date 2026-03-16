/* Tiny shared library used by FFI tests. */
#include <string.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

EXPORT int    test_add(int a, int b)    { return a + b; }
EXPORT int    test_negate(int a)        { return -a; }
EXPORT double test_half(double a)       { return a / 2.0; }
EXPORT int    test_answer(void)         { return 42; }
EXPORT double test_pi(void)             { return 3.14159265358979; }
EXPORT const char *test_greeting(void)  { return "hello from C"; }
EXPORT void   test_noop(void)           { /* nothing */ }

/* Multi-arg: sum of 8 ints (exercises dynamic arg count). */
EXPORT int test_sum8(int a, int b, int c, int d,
                     int e, int f, int g, int h) {
    return a + b + c + d + e + f + g + h;
}

/* Float operations. */
EXPORT float test_addf(float a, float b) { return a + b; }

/* Pointer / struct operations. */
EXPORT void test_fill_buf(int *buf, int n, int val) {
    for (int i = 0; i < n; i++) buf[i] = val;
}
EXPORT int test_read_buf(const int *buf, int idx) {
    return buf[idx];
}
EXPORT int test_strlen_ptr(const char *s) {
    return s ? (int)strlen(s) : 0;
}
