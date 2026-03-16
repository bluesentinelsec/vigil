/* Tiny shared library used by FFI tests. */
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

EXPORT int    test_add(int a, int b)    { return a + b; }
EXPORT int    test_negate(int a)        { return -a; }
EXPORT double test_half(double a)       { return a / 2.0; }
EXPORT int    test_answer(void)         { return 42; }
