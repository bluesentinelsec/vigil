#include "basl/semantic.h"

#include <stdlib.h>
#include <string.h>

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
