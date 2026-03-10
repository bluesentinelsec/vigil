#ifndef BASL_BASL_H
#define BASL_BASL_H

#ifdef _WIN32
#if defined(BASL_SHARED)
#if defined(BASL_EXPORTS)
#define BASL_API __declspec(dllexport)
#else
#define BASL_API __declspec(dllimport)
#endif
#else
#define BASL_API
#endif
#else
#define BASL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

BASL_API int basl_sum(int a, int b);

#ifdef __cplusplus
}
#endif

#endif
