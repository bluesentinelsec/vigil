#ifndef BASL_DOC_REGISTRY_H
#define BASL_DOC_REGISTRY_H

#include <stddef.h>

#include "basl/doc.h"
#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Doc Entry ────────────────────────────────────────────── */

typedef struct basl_doc_entry {
    const char *name;           /* "print" or "math.sqrt" */
    const char *signature;      /* "print(value: any) -> void" */
    const char *summary;        /* One-line description */
    const char *description;    /* Full description (may be NULL) */
    const char *example;        /* Example code (may be NULL) */
} basl_doc_entry_t;

/* ── Doc Registry ─────────────────────────────────────────── */

/*
 * Lookup documentation by name.
 * Supports: "print", "math", "math.sqrt", "MyClass.method"
 * Returns NULL if not found.
 */
BASL_API const basl_doc_entry_t *basl_doc_lookup(const char *name);

/*
 * List all documented modules.
 * Returns array of module names, sets *count.
 */
BASL_API const char **basl_doc_list_modules(size_t *count);

/*
 * List all entries in a module.
 * Returns array of entries, sets *count.
 * Returns NULL if module not found.
 */
BASL_API const basl_doc_entry_t *basl_doc_list_module(
    const char *module_name,
    size_t *count
);

/*
 * Render a doc entry to formatted text.
 * Caller must free *out_text.
 */
BASL_API basl_status_t basl_doc_entry_render(
    const basl_doc_entry_t *entry,
    char **out_text,
    size_t *out_length,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
