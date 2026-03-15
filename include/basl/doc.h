#ifndef BASL_DOC_H
#define BASL_DOC_H

#include <stddef.h>

#include "basl/allocator.h"
#include "basl/export.h"
#include "basl/status.h"
#include "basl/token.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Doc comment ─────────────────────────────────────────────────── */

typedef struct basl_doc_comment {
    char *text;
    size_t length;
} basl_doc_comment_t;

/* ── Doc parameter ───────────────────────────────────────────────── */

typedef struct basl_doc_param {
    char *type_text;
    size_t type_length;
    char *name;
    size_t name_length;
} basl_doc_param_t;

/* ── Doc symbol kind ─────────────────────────────────────────────── */

typedef enum basl_doc_kind {
    BASL_DOC_FUNCTION = 0,
    BASL_DOC_CLASS = 1,
    BASL_DOC_INTERFACE = 2,
    BASL_DOC_ENUM = 3,
    BASL_DOC_CONSTANT = 4,
    BASL_DOC_VARIABLE = 5
} basl_doc_kind_t;

/* ── Doc symbol ──────────────────────────────────────────────────── */

typedef struct basl_doc_symbol {
    basl_doc_kind_t kind;
    char *name;
    size_t name_length;
    basl_doc_comment_t comment;

    /* Functions, class methods, interface methods. */
    basl_doc_param_t *params;
    size_t param_count;
    char *return_text;
    size_t return_length;

    /* Classes. */
    struct basl_doc_symbol *fields;
    size_t field_count;
    struct basl_doc_symbol *methods;
    size_t method_count;
    char *implements_text;
    size_t implements_length;

    /* Interfaces. */
    struct basl_doc_symbol *iface_methods;
    size_t iface_method_count;

    /* Enums. */
    char **variant_names;
    size_t *variant_name_lengths;
    size_t variant_count;

    /* Constants and variables. */
    char *type_text;
    size_t type_length;
} basl_doc_symbol_t;

/* ── Doc module ──────────────────────────────────────────────────── */

typedef struct basl_doc_module {
    char *name;
    size_t name_length;
    basl_doc_comment_t summary;
    basl_doc_symbol_t *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    const basl_allocator_t *allocator;
} basl_doc_module_t;

/* ── Public API ──────────────────────────────────────────────────── */

/*
 * Extract documentation from source text and its token stream.
 * The token list must have been produced by basl_lex_source for the
 * same source text.  Only public declarations are included.
 */
BASL_API basl_status_t basl_doc_extract(
    const basl_allocator_t *allocator,
    const char *filename,
    size_t filename_length,
    const char *source_text,
    size_t source_length,
    const basl_token_list_t *tokens,
    basl_doc_module_t *out_module,
    basl_error_t *error
);

/*
 * Render the full module documentation to a string.
 * If symbol is non-NULL, render only that symbol (supports "Class.method").
 * Caller must free *out_text with the allocator (or free() if NULL).
 */
BASL_API basl_status_t basl_doc_render(
    const basl_doc_module_t *module,
    const char *symbol,
    char **out_text,
    size_t *out_length,
    basl_error_t *error
);

/*
 * Free all memory owned by a doc module.
 */
BASL_API void basl_doc_module_free(basl_doc_module_t *module);

#ifdef __cplusplus
}
#endif

#endif
