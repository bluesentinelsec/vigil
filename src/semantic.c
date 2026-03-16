#include "basl/semantic.h"

#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/stdlib.h"
#include "basl/value.h"
#include "internal/basl_internal.h"

/* ── Type Utilities ───────────────────────────────────────── */

basl_semantic_type_t basl_semantic_type_invalid(void) {
    basl_semantic_type_t type;
    type.kind = BASL_TYPE_INVALID;
    type.definition_index = SIZE_MAX;
    return type;
}

basl_semantic_type_t basl_semantic_type_primitive(basl_type_kind_t kind) {
    basl_semantic_type_t type;
    type.kind = kind;
    type.definition_index = SIZE_MAX;
    return type;
}

int basl_semantic_type_is_valid(basl_semantic_type_t type) {
    return type.kind != BASL_TYPE_INVALID;
}

int basl_semantic_type_equal(basl_semantic_type_t left, basl_semantic_type_t right) {
    return left.kind == right.kind && left.definition_index == right.definition_index;
}

/* ── Allocation Helpers ───────────────────────────────────── */

static void *sem_alloc(basl_runtime_t *runtime, size_t size, basl_error_t *error) {
    void *ptr = NULL;
    if (basl_runtime_alloc(runtime, size, &ptr, error) != BASL_STATUS_OK) {
        return NULL;
    }
    return ptr;
}

static void sem_free(basl_runtime_t *runtime, void *ptr) {
    void *p = ptr;
    basl_runtime_free(runtime, &p);
}

/* ── Node Allocation ──────────────────────────────────────── */

static basl_semantic_node_t *semantic_node_alloc(
    basl_runtime_t *runtime,
    basl_error_t *error
) {
    basl_semantic_node_t *node = sem_alloc(runtime, sizeof(basl_semantic_node_t), error);
    if (node != NULL) {
        memset(node, 0, sizeof(basl_semantic_node_t));
        node->kind = BASL_NODE_INVALID;
        node->type = basl_semantic_type_invalid();
    }
    return node;
}

/* Suppress unused warning until analyzer is implemented */
BASL_API basl_semantic_node_t *basl_semantic_node_create(
    basl_runtime_t *runtime,
    basl_semantic_node_kind_t kind,
    basl_source_span_t span,
    basl_error_t *error
) {
    basl_semantic_node_t *node = semantic_node_alloc(runtime, error);
    if (node != NULL) {
        node->kind = kind;
        node->span = span;
    }
    return node;
}

static void semantic_node_free(basl_runtime_t *runtime, basl_semantic_node_t *node) {
    size_t i;
    if (node == NULL) return;

    switch (node->kind) {
        case BASL_NODE_FUNCTION_DECL:
            semantic_node_free(runtime, node->data.function_decl.body);
            break;

        case BASL_NODE_CLASS_DECL:
            for (i = 0; i < node->data.class_decl.member_count; i++) {
                semantic_node_free(runtime, node->data.class_decl.members[i]);
            }
            sem_free(runtime, node->data.class_decl.members);
            break;

        case BASL_NODE_BLOCK_STMT:
            for (i = 0; i < node->data.block.statement_count; i++) {
                semantic_node_free(runtime, node->data.block.statements[i]);
            }
            sem_free(runtime, node->data.block.statements);
            break;

        case BASL_NODE_IF_STMT:
            semantic_node_free(runtime, node->data.if_stmt.condition);
            semantic_node_free(runtime, node->data.if_stmt.then_branch);
            semantic_node_free(runtime, node->data.if_stmt.else_branch);
            break;

        case BASL_NODE_WHILE_STMT:
            semantic_node_free(runtime, node->data.while_stmt.condition);
            semantic_node_free(runtime, node->data.while_stmt.body);
            break;

        case BASL_NODE_RETURN_STMT:
            semantic_node_free(runtime, node->data.return_stmt.value);
            break;

        case BASL_NODE_EXPR_STMT:
            semantic_node_free(runtime, node->data.expr_stmt.expression);
            break;

        case BASL_NODE_ASSIGN_STMT:
            semantic_node_free(runtime, node->data.assign.target);
            semantic_node_free(runtime, node->data.assign.value);
            break;

        case BASL_NODE_BINARY_EXPR:
            semantic_node_free(runtime, node->data.binary.left);
            semantic_node_free(runtime, node->data.binary.right);
            break;

        case BASL_NODE_UNARY_EXPR:
            semantic_node_free(runtime, node->data.unary.operand);
            break;

        case BASL_NODE_CALL_EXPR:
            semantic_node_free(runtime, node->data.call.callee);
            for (i = 0; i < node->data.call.argument_count; i++) {
                semantic_node_free(runtime, node->data.call.arguments[i]);
            }
            sem_free(runtime, node->data.call.arguments);
            break;

        case BASL_NODE_METHOD_CALL_EXPR:
            semantic_node_free(runtime, node->data.method_call.receiver);
            for (i = 0; i < node->data.method_call.argument_count; i++) {
                semantic_node_free(runtime, node->data.method_call.arguments[i]);
            }
            sem_free(runtime, node->data.method_call.arguments);
            break;

        case BASL_NODE_INDEX_EXPR:
            semantic_node_free(runtime, node->data.index_expr.object);
            semantic_node_free(runtime, node->data.index_expr.index);
            break;

        case BASL_NODE_FIELD_ACCESS_EXPR:
            semantic_node_free(runtime, node->data.field_access.object);
            break;

        case BASL_NODE_VAR_DECL:
            semantic_node_free(runtime, node->data.var_decl.initializer);
            break;

        default:
            break;
    }

    sem_free(runtime, node);
}

/* ── File API ─────────────────────────────────────────────── */

basl_status_t basl_semantic_file_create(
    basl_semantic_file_t **out,
    basl_runtime_t *runtime,
    basl_source_id_t source_id,
    basl_error_t *error
) {
    basl_semantic_file_t *file;

    if (out == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "out must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    file = sem_alloc(runtime, sizeof(basl_semantic_file_t), error);
    if (file == NULL) {
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memset(file, 0, sizeof(basl_semantic_file_t));
    file->runtime = runtime;
    file->source_id = source_id;
    basl_diagnostic_list_init(&file->diagnostics, runtime);

    *out = file;
    return BASL_STATUS_OK;
}

void basl_semantic_file_destroy(basl_semantic_file_t **file) {
    basl_semantic_file_t *f;
    size_t i;

    if (file == NULL || *file == NULL) return;
    f = *file;

    for (i = 0; i < f->declaration_count; i++) {
        semantic_node_free(f->runtime, f->declarations[i]);
    }
    sem_free(f->runtime, f->declarations);
    sem_free(f->runtime, f->nodes_by_position);
    sem_free(f->runtime, f->references);
    basl_diagnostic_list_free(&f->diagnostics);
    sem_free(f->runtime, f);
    *file = NULL;
}

/*
 * Binary search for node containing offset.
 */
const basl_semantic_node_t *basl_semantic_file_node_at(
    const basl_semantic_file_t *file,
    size_t offset
) {
    size_t lo, hi, mid;
    const basl_semantic_node_t *best = NULL;
    const basl_semantic_node_t *node;

    if (file == NULL || file->node_count == 0) return NULL;

    lo = 0;
    hi = file->node_count;

    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        node = file->nodes_by_position[mid];

        if (offset < node->span.start_offset) {
            hi = mid;
        } else if (offset >= node->span.end_offset) {
            lo = mid + 1;
        } else {
            /* offset is within this node's span */
            best = node;
            /* Look for a more specific (smaller) node */
            lo = mid + 1;
        }
    }

    return best;
}

basl_semantic_type_t basl_semantic_file_type_at(
    const basl_semantic_file_t *file,
    size_t offset
) {
    const basl_semantic_node_t *node = basl_semantic_file_node_at(file, offset);
    if (node == NULL) return basl_semantic_type_invalid();
    return node->type;
}

/* ── Index API ────────────────────────────────────────────── */

basl_status_t basl_semantic_index_create(
    basl_semantic_index_t **out,
    basl_runtime_t *runtime,
    const basl_source_registry_t *sources,
    basl_error_t *error
) {
    basl_semantic_index_t *index;

    if (out == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "out must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    index = sem_alloc(runtime, sizeof(basl_semantic_index_t), error);
    if (index == NULL) {
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memset(index, 0, sizeof(basl_semantic_index_t));
    index->runtime = runtime;
    index->sources = sources;
    basl_debug_symbol_table_init(&index->symbols, runtime);

    *out = index;
    return BASL_STATUS_OK;
}

void basl_semantic_index_destroy(basl_semantic_index_t **index) {
    basl_semantic_index_t *idx;
    size_t i;

    if (index == NULL || *index == NULL) return;
    idx = *index;

    for (i = 0; i < idx->file_count; i++) {
        basl_semantic_file_destroy(&idx->files[i]);
    }
    sem_free(idx->runtime, idx->files);
    basl_debug_symbol_table_free(&idx->symbols);
    sem_free(idx->runtime, idx);
    *index = NULL;
}

const basl_semantic_file_t *basl_semantic_index_get_file(
    const basl_semantic_index_t *index,
    basl_source_id_t source_id
) {
    size_t i;
    if (index == NULL) return NULL;

    for (i = 0; i < index->file_count; i++) {
        if (index->files[i]->source_id == source_id) {
            return index->files[i];
        }
    }
    return NULL;
}

void basl_semantic_index_remove(
    basl_semantic_index_t *index,
    basl_source_id_t source_id
) {
    size_t i;
    if (index == NULL) return;

    for (i = 0; i < index->file_count; i++) {
        if (index->files[i]->source_id == source_id) {
            basl_semantic_file_destroy(&index->files[i]);
            /* Shift remaining files down */
            for (; i + 1 < index->file_count; i++) {
                index->files[i] = index->files[i + 1];
            }
            index->file_count--;
            return;
        }
    }
}

/* ── Index Analysis ───────────────────────────────────────── */

static basl_status_t semantic_index_grow_files(
    basl_semantic_index_t *index,
    basl_error_t *error
) {
    size_t new_capacity;
    basl_semantic_file_t **new_files;

    if (index->file_count < index->file_capacity) {
        return BASL_STATUS_OK;
    }

    new_capacity = index->file_capacity == 0 ? 4 : index->file_capacity * 2;
    new_files = sem_alloc(index->runtime,
                          new_capacity * sizeof(basl_semantic_file_t *), error);
    if (new_files == NULL) {
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (index->files != NULL) {
        memcpy(new_files, index->files,
               index->file_count * sizeof(basl_semantic_file_t *));
        sem_free(index->runtime, index->files);
    }

    index->files = new_files;
    index->file_capacity = new_capacity;
    return BASL_STATUS_OK;
}

basl_status_t basl_semantic_index_analyze(
    basl_semantic_index_t *index,
    basl_source_id_t source_id,
    basl_error_t *error
) {
    basl_status_t status;
    basl_semantic_file_t *file = NULL;
    basl_native_registry_t natives;
    basl_object_t *function = NULL;
    basl_debug_symbol_table_t file_symbols;
    int symbols_initialized = 0;
    size_t i, count;

    if (index == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "index must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Remove existing analysis for this file. */
    basl_semantic_index_remove(index, source_id);

    /* Create new semantic file. */
    status = basl_semantic_file_create(&file, index->runtime, source_id, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    /* Initialize natives and file symbols. */
    basl_native_registry_init(&natives);
    basl_stdlib_register_all(&natives, error);
    basl_debug_symbol_table_init(&file_symbols, index->runtime);
    symbols_initialized = 1;

    /* Compile to get diagnostics and symbols. */
    status = basl_compile_source_with_debug_info(
        index->sources, source_id, &natives,
        &function, &file->diagnostics, &file_symbols, error
    );

    /* Copy symbols to index (even if compile failed, we want partial symbols). */
    count = basl_debug_symbol_table_count(&file_symbols);
    for (i = 0; i < count; i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&file_symbols, i);
        basl_debug_symbol_table_add(
            &index->symbols, sym->kind,
            sym->name, sym->name_length, sym->span,
            sym->is_public, sym->parent_index, error
        );
    }

    /* Clean up compilation artifacts. */
    if (function != NULL) {
        basl_object_release(&function);
    }
    basl_native_registry_free(&natives);
    if (symbols_initialized) {
        basl_debug_symbol_table_free(&file_symbols);
    }

    /* Add file to index. */
    status = semantic_index_grow_files(index, error);
    if (status != BASL_STATUS_OK) {
        basl_semantic_file_destroy(&file);
        return status;
    }

    index->files[index->file_count++] = file;
    return BASL_STATUS_OK;
}

/* ── Type Formatting ──────────────────────────────────────── */

char *basl_semantic_type_to_string(
    const basl_semantic_index_t *index,
    basl_semantic_type_t type
) {
    const char *name;
    char *result;
    size_t len;

    (void)index;  /* Will be used for class/interface names */

    if (!basl_semantic_type_is_valid(type)) {
        name = "<invalid>";
    } else {
        name = basl_type_kind_name(type.kind);
    }

    len = strlen(name);
    result = malloc(len + 1);
    if (result != NULL) {
        memcpy(result, name, len + 1);
    }
    return result;
}

/* ── Go to Definition ─────────────────────────────────────── */

basl_status_t basl_semantic_index_definition_at(
    const basl_semantic_index_t *index,
    basl_source_id_t source_id,
    size_t offset,
    basl_source_span_t *out_definition,
    basl_error_t *error
) {
    const basl_semantic_file_t *file = NULL;
    const basl_semantic_node_t *node;
    size_t i;

    if (index == NULL || out_definition == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "semantic: invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Find file */
    for (i = 0; i < index->file_count; i++) {
        if (index->files[i]->source_id == source_id) {
            file = index->files[i];
            break;
        }
    }

    if (file == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "semantic: source not found");
        return BASL_STATUS_INTERNAL;
    }

    node = basl_semantic_file_node_at(file, offset);
    if (node == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "semantic: no node at position");
        return BASL_STATUS_INTERNAL;
    }

    /* If node is an identifier expression, look up the definition */
    if (node->kind == BASL_NODE_IDENTIFIER_EXPR) {
        const char *name = node->data.identifier.name;
        size_t name_len = node->data.identifier.name_length;

        /* Search symbols for matching name */
        size_t sym_count = basl_debug_symbol_table_count(&index->symbols);
        for (i = 0; i < sym_count; i++) {
            const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&index->symbols, i);
            if (sym->name_length == name_len &&
                strncmp(sym->name, name, name_len) == 0) {
                *out_definition = sym->span;
                return BASL_STATUS_OK;
            }
        }
    }

    /* Node is already a definition or no definition found */
    basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                           "semantic: definition not found");
    return BASL_STATUS_INTERNAL;
}
