#ifndef VIGIL_STDLIB_H
#define VIGIL_STDLIB_H

#include <string.h>

#include "vigil/native_module.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Custom log handler for embedders ────────────────────────────── */

    /**
     * Custom log handler callback type.
     * @param level     Log level: 0=debug, 1=info, 2=warn, 3=error
     * @param msg       The log message
     * @param attrs     JSON-encoded key-value attributes (e.g., {"key":"value"})
     * @param user_data User-provided context from vigil_log_set_handler
     */
    typedef void (*vigil_log_handler_t)(int level, const char *msg, const char *attrs, void *user_data);

    /**
     * Set a custom log handler to intercept all VIGIL log calls.
     * Pass NULL to restore the default handler.
     */
    VIGIL_API void vigil_log_set_handler(vigil_log_handler_t handler, void *user_data);

    /* Each stdlib module exports a const descriptor. */
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_args;
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_atomic;
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_compress;
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_crypto;
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_csv;
#ifdef VIGIL_HAS_STDLIB_FFI
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_ffi;
#endif
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_fmt;
#ifdef VIGIL_HAS_STDLIB_FS
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_fs;
#endif
#ifdef VIGIL_HAS_STDLIB_HTTP
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_http;
#endif
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_log;
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_math;
#ifdef VIGIL_HAS_STDLIB_NET
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_net;
#endif
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_parse;
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_random;
#ifdef VIGIL_HAS_STDLIB_READLINE
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_readline;
#endif
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_regex;
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_test;
#ifdef VIGIL_HAS_STDLIB_THREAD
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_thread;
#endif
#ifdef VIGIL_HAS_STDLIB_TIME
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_time;
#endif
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_unsafe;
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_url;
    extern VIGIL_API const vigil_native_module_t vigil_stdlib_yaml;

    /* ── Table-driven stdlib registry ───────────────────────────────── */

    typedef struct vigil_stdlib_entry
    {
        const char *name;
        size_t name_length;
        const vigil_native_module_t *module;
    } vigil_stdlib_entry_t;

    /* Return the stdlib module table.  The table is built inside a function
       body so that address-of-extern initializers work on MSVC. */
    static inline const vigil_stdlib_entry_t *vigil_stdlib_modules(size_t *out_count)
    {
        // clang-format off
        static const vigil_stdlib_entry_t kModules[] = {
            {"args",     4U, &vigil_stdlib_args},
            {"atomic",   6U, &vigil_stdlib_atomic},
            {"compress", 8U, &vigil_stdlib_compress},
            {"crypto",   6U, &vigil_stdlib_crypto},
            {"csv",      3U, &vigil_stdlib_csv},
#ifdef VIGIL_HAS_STDLIB_FFI
            {"ffi",      3U, &vigil_stdlib_ffi},
#else
            {"ffi",      3U, NULL},
#endif
            {"fmt",      3U, &vigil_stdlib_fmt},
#ifdef VIGIL_HAS_STDLIB_FS
            {"fs",       2U, &vigil_stdlib_fs},
#else
            {"fs",       2U, NULL},
#endif
#ifdef VIGIL_HAS_STDLIB_HTTP
            {"http",     4U, &vigil_stdlib_http},
#else
            {"http",     4U, NULL},
#endif
            {"log",      3U, &vigil_stdlib_log},
            {"math",     4U, &vigil_stdlib_math},
#ifdef VIGIL_HAS_STDLIB_NET
            {"net",      3U, &vigil_stdlib_net},
#else
            {"net",      3U, NULL},
#endif
            {"parse",    5U, &vigil_stdlib_parse},
            {"random",   6U, &vigil_stdlib_random},
#ifdef VIGIL_HAS_STDLIB_READLINE
            {"readline", 8U, &vigil_stdlib_readline},
#else
            {"readline", 8U, NULL},
#endif
            {"regex",    5U, &vigil_stdlib_regex},
            {"test",     4U, &vigil_stdlib_test},
#ifdef VIGIL_HAS_STDLIB_THREAD
            {"thread",   6U, &vigil_stdlib_thread},
#else
            {"thread",   6U, NULL},
#endif
#ifdef VIGIL_HAS_STDLIB_TIME
            {"time",     4U, &vigil_stdlib_time},
#else
            {"time",     4U, NULL},
#endif
            {"unsafe",   6U, &vigil_stdlib_unsafe},
            {"url",      3U, &vigil_stdlib_url},
            {"yaml",     4U, &vigil_stdlib_yaml},
        };
        // clang-format on
        *out_count = sizeof(kModules) / sizeof(kModules[0]);
        return kModules;
    }

    static inline int vigil_stdlib_name_equals(const char *name, size_t name_length, const char *literal,
                                               size_t literal_length)
    {
        return name_length == literal_length && memcmp(name, literal, literal_length) == 0;
    }

    /* Register all built-in stdlib modules into a native registry. */
    static inline vigil_status_t vigil_stdlib_register_all(vigil_native_registry_t *registry, vigil_error_t *error)
    {
        size_t count = 0;
        const vigil_stdlib_entry_t *mods = vigil_stdlib_modules(&count);
        size_t i;
        for (i = 0U; i < count; i++)
        {
            if (mods[i].module != NULL)
            {
                vigil_status_t status = vigil_native_registry_add(registry, mods[i].module, error);
                if (status != VIGIL_STATUS_OK)
                    return status;
            }
        }
        return VIGIL_STATUS_OK;
    }

    /* Check if an import name refers to any known stdlib module. */
    static inline int vigil_stdlib_is_known_module(const char *name, size_t name_length)
    {
        size_t count = 0;
        const vigil_stdlib_entry_t *mods = vigil_stdlib_modules(&count);
        size_t i;
        for (i = 0U; i < count; i++)
        {
            if (vigil_stdlib_name_equals(name, name_length, mods[i].name, mods[i].name_length))
                return 1;
        }
        return 0;
    }

    /* Check if an import name is a stdlib module available in this build. */
    static inline int vigil_stdlib_is_available_module(const char *name, size_t name_length)
    {
        size_t count = 0;
        const vigil_stdlib_entry_t *mods = vigil_stdlib_modules(&count);
        size_t i;
        for (i = 0U; i < count; i++)
        {
            if (mods[i].module != NULL &&
                vigil_stdlib_name_equals(name, name_length, mods[i].name, mods[i].name_length))
                return 1;
        }
        return 0;
    }

    /* Backward-compatible alias for existing callers. */
    static inline int vigil_stdlib_is_native_module(const char *name, size_t name_length)
    {
        return vigil_stdlib_is_available_module(name, name_length);
    }

#ifdef __cplusplus
}
#endif

#endif
