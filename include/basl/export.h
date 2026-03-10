#ifndef BASL_EXPORT_H
#define BASL_EXPORT_H

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

#endif
