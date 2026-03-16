#ifndef BASL_SEMANTIC_H
#define BASL_SEMANTIC_H

#include <stddef.h>

#include "basl/debug_info.h"
#include "basl/diagnostic.h"
#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Semantic analysis model for LSP support.
 *
 * Provides a queryable representation of a BASL program with:
 * - Type information at any source position
 * - Symbol definitions and references
 * - Scope-aware symbol enumeration
 * - Incremental updates on file changes
 */

/* ── Typed Node Kinds ─────────────────────────────────────── */

typedef enum basl_semantic_node_kind {
    BASL_NODE_INVALID = 0,

    /* Declarations */
    BASL_NODE_FUNCTION_DECL,
    BASL_NODE_CLASS_DECL,
    BASL_NODE_INTERFACE_DECL,
    BASL_NODE_ENUM_DECL,
    BASL_NODE_FIELD_DECL,
    BASL_NODE_METHOD_DECL,
    BASL_NODE_PARAM_DECL,
    BASL_NODE_VAR_DECL,
    BASL_NODE_CONST_DECL,

    /* Statements */
    BASL_NODE_BLOCK_STMT,
    BASL_NODE_IF_STMT,
    BASL_NODE_WHILE_STMT,
    BASL_NODE_FOR_STMT,
    BASL_NODE_RETURN_STMT,
    BASL_NODE_BREAK_STMT,
    BASL_NODE_CONTINUE_STMT,
    BASL_NODE_EXPR_STMT,
    BASL_NODE_ASSIGN_STMT,

    /* Expressions */
    BASL_NODE_LITERAL_EXPR,
    BASL_NODE_IDENTIFIER_EXPR,
    BASL_NODE_BINARY_EXPR,
    BASL_NODE_UNARY_EXPR,
    BASL_NODE_CALL_EXPR,
    BASL_NODE_METHOD_CALL_EXPR,
    BASL_NODE_INDEX_EXPR,
    BASL_NODE_FIELD_ACCESS_EXPR,
    BASL_NODE_ARRAY_LITERAL_EXPR,
    BASL_NODE_MAP_LITERAL_EXPR,
    BASL_NODE_NEW_EXPR,
    BASL_NODE_CAST_EXPR,
    BASL_NODE_TERNARY_EXPR
} basl_semantic_node_kind_t;

/* ── Semantic Type ────────────────────────────────────────── */

/*
 * Resolved type information. Mirrors basl_binding_type_t but is
 * part of the public semantic API.
 */
typedef struct basl_semantic_type {
    basl_type_kind_t kind;
    size_t definition_index;  /* Index into classes/interfaces/enums/etc */
} basl_semantic_type_t;

/* ── Semantic Node ────────────────────────────────────────── */

/*
 * A node in the typed AST. Every node has:
 * - A kind identifying what it represents
 * - A source span for position queries
 * - A resolved type (for expressions)
 * - Links to child nodes
 */
typedef struct basl_semantic_node basl_semantic_node_t;

struct basl_semantic_node {
    basl_semantic_node_kind_t kind;
    basl_source_span_t span;
    basl_semantic_type_t type;

    /* Node-specific data stored inline to avoid extra allocations */
    union {
        struct {
            size_t function_index;
            basl_semantic_node_t *body;
        } function_decl;

        struct {
            size_t class_index;
            basl_semantic_node_t **members;
            size_t member_count;
        } class_decl;

        struct {
            basl_semantic_node_t **statements;
            size_t statement_count;
        } block;

        struct {
            basl_semantic_node_t *condition;
            basl_semantic_node_t *then_branch;
            basl_semantic_node_t *else_branch;
        } if_stmt;

        struct {
            basl_semantic_node_t *condition;
            basl_semantic_node_t *body;
        } while_stmt;

        struct {
            basl_semantic_node_t *value;
        } return_stmt;

        struct {
            basl_semantic_node_t *expression;
        } expr_stmt;

        struct {
            basl_semantic_node_t *target;
            basl_semantic_node_t *value;
        } assign;

        struct {
            int64_t int_value;
            double float_value;
            const char *string_value;
            size_t string_length;
            int bool_value;
        } literal;

        struct {
            const char *name;
            size_t name_length;
            size_t resolved_index;  /* Local slot or global index */
            int is_local;
        } identifier;

        struct {
            basl_binary_operator_kind_t op;
            basl_semantic_node_t *left;
            basl_semantic_node_t *right;
        } binary;

        struct {
            basl_unary_operator_kind_t op;
            basl_semantic_node_t *operand;
        } unary;

        struct {
            basl_semantic_node_t *callee;
            basl_semantic_node_t **arguments;
            size_t argument_count;
            size_t function_index;
        } call;

        struct {
            basl_semantic_node_t *receiver;
            const char *method_name;
            size_t method_name_length;
            basl_semantic_node_t **arguments;
            size_t argument_count;
            size_t method_index;
        } method_call;

        struct {
            basl_semantic_node_t *object;
            basl_semantic_node_t *index;
        } index_expr;

        struct {
            basl_semantic_node_t *object;
            const char *field_name;
            size_t field_name_length;
            size_t field_index;
        } field_access;

        struct {
            const char *name;
            size_t name_length;
            basl_semantic_type_t var_type;
            basl_semantic_node_t *initializer;
            int is_const;
        } var_decl;
    } data;
};

/* ── Symbol Reference ─────────────────────────────────────── */

/*
 * Tracks where a symbol is used (not just defined).
 */
typedef struct basl_semantic_reference {
    basl_source_span_t span;
    basl_debug_symbol_kind_t symbol_kind;
    size_t symbol_index;
    int is_write;  /* true if this is an assignment target */
} basl_semantic_reference_t;

/* ── Semantic File ────────────────────────────────────────── */

/*
 * Semantic analysis result for a single source file.
 */
typedef struct basl_semantic_file {
    basl_runtime_t *runtime;
    basl_source_id_t source_id;

    /* Top-level declarations as typed nodes */
    basl_semantic_node_t **declarations;
    size_t declaration_count;
    size_t declaration_capacity;

    /* All nodes in span order for position queries */
    basl_semantic_node_t **nodes_by_position;
    size_t node_count;
    size_t node_capacity;

    /* References to symbols (for find-references) */
    basl_semantic_reference_t *references;
    size_t reference_count;
    size_t reference_capacity;

    /* Diagnostics from semantic analysis */
    basl_diagnostic_list_t diagnostics;
} basl_semantic_file_t;

/* ── Semantic Index ───────────────────────────────────────── */

/*
 * Cross-file semantic index for a workspace.
 */
typedef struct basl_semantic_index {
    basl_runtime_t *runtime;
    const basl_source_registry_t *sources;

    /* Per-file semantic data */
    basl_semantic_file_t **files;
    size_t file_count;
    size_t file_capacity;

    /* Global symbol table (merged from all files) */
    basl_debug_symbol_table_t symbols;
} basl_semantic_index_t;

/* ── File API ─────────────────────────────────────────────── */

BASL_API basl_status_t basl_semantic_file_create(
    basl_semantic_file_t **out,
    basl_runtime_t *runtime,
    basl_source_id_t source_id,
    basl_error_t *error
);

BASL_API void basl_semantic_file_destroy(basl_semantic_file_t **file);

/*
 * Find the innermost node containing the given position.
 * Returns NULL if no node contains the position.
 */
BASL_API const basl_semantic_node_t *basl_semantic_file_node_at(
    const basl_semantic_file_t *file,
    size_t offset
);

/*
 * Get the type at a source position.
 * Returns invalid type if position is not within a typed expression.
 */
BASL_API basl_semantic_type_t basl_semantic_file_type_at(
    const basl_semantic_file_t *file,
    size_t offset
);

/*
 * Get all symbols visible at a position (for completion).
 */
BASL_API basl_status_t basl_semantic_file_visible_symbols(
    const basl_semantic_file_t *file,
    size_t offset,
    basl_debug_symbol_table_t *out_symbols,
    basl_error_t *error
);

/* ── Index API ────────────────────────────────────────────── */

BASL_API basl_status_t basl_semantic_index_create(
    basl_semantic_index_t **out,
    basl_runtime_t *runtime,
    const basl_source_registry_t *sources,
    basl_error_t *error
);

BASL_API void basl_semantic_index_destroy(basl_semantic_index_t **index);

/*
 * Analyze a source file and add/update it in the index.
 */
BASL_API basl_status_t basl_semantic_index_analyze(
    basl_semantic_index_t *index,
    basl_source_id_t source_id,
    basl_error_t *error
);

/*
 * Remove a file from the index.
 */
BASL_API void basl_semantic_index_remove(
    basl_semantic_index_t *index,
    basl_source_id_t source_id
);

/*
 * Get semantic data for a file (NULL if not analyzed).
 */
BASL_API const basl_semantic_file_t *basl_semantic_index_get_file(
    const basl_semantic_index_t *index,
    basl_source_id_t source_id
);

/*
 * Find definition of symbol at position.
 */
BASL_API basl_status_t basl_semantic_index_definition_at(
    const basl_semantic_index_t *index,
    basl_source_id_t source_id,
    size_t offset,
    basl_source_span_t *out_definition,
    basl_error_t *error
);

/*
 * Find all references to symbol at position.
 */
BASL_API basl_status_t basl_semantic_index_references_at(
    const basl_semantic_index_t *index,
    basl_source_id_t source_id,
    size_t offset,
    basl_semantic_reference_t **out_references,
    size_t *out_count,
    basl_error_t *error
);

/* ── Type Utilities ───────────────────────────────────────── */

BASL_API basl_semantic_type_t basl_semantic_type_invalid(void);
BASL_API basl_semantic_type_t basl_semantic_type_primitive(basl_type_kind_t kind);
BASL_API int basl_semantic_type_is_valid(basl_semantic_type_t type);
BASL_API int basl_semantic_type_equal(
    basl_semantic_type_t left,
    basl_semantic_type_t right
);

/*
 * Format type as human-readable string.
 * Caller must free the returned string.
 */
BASL_API char *basl_semantic_type_to_string(
    const basl_semantic_index_t *index,
    basl_semantic_type_t type
);

#ifdef __cplusplus
}
#endif

#endif
