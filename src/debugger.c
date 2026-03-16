/* BASL debugger: breakpoints, stepping, and variable inspection.
 *
 * The debugger attaches to a VM via a lightweight hook function that
 * is checked before each opcode dispatch.  When no debugger is attached
 * the hook pointer is NULL and the VM runs at full speed.
 */
#include <string.h>

#include "basl/debugger.h"
#include "basl/chunk.h"
#include "basl/status.h"

#include "internal/basl_internal.h"

/* ── Internal types ──────────────────────────────────────────────── */

typedef enum basl_debug_mode {
    BASL_DEBUG_MODE_RUN = 0,
    BASL_DEBUG_MODE_PAUSE = 1,
    BASL_DEBUG_MODE_STEP_OVER = 2,
    BASL_DEBUG_MODE_STEP_INTO = 3,
    BASL_DEBUG_MODE_STEP_OUT = 4
} basl_debug_mode_t;

typedef struct basl_breakpoint {
    basl_source_id_t source_id;
    uint32_t line;
    int active;
} basl_breakpoint_t;

struct basl_debugger {
    basl_runtime_t *runtime;
    basl_vm_t *vm;
    const basl_source_registry_t *sources;
    const basl_debug_symbol_table_t *symbols;
    basl_debug_callback_t callback;
    void *callback_userdata;
    basl_debug_mode_t mode;
    /* For step-over/step-out: the frame depth when stepping started. */
    size_t step_frame_depth;
    /* For step-over: the source line when stepping started. */
    uint32_t step_start_line;
    basl_source_id_t step_start_source;
    /* Breakpoints. */
    basl_breakpoint_t *breakpoints;
    size_t breakpoint_count;
    size_t breakpoint_capacity;
    /* Cached pause state. */
    int is_paused;
};

/* ── Forward declarations ────────────────────────────────────────── */

static int basl_debugger_vm_hook(basl_vm_t *vm, void *userdata);

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Resolve the current IP to a source location. */
static int basl_debugger_resolve_ip(
    const basl_debugger_t *dbg,
    size_t frame_index,
    basl_source_id_t *out_source_id,
    uint32_t *out_line,
    uint32_t *out_column
) {
    size_t vm_frame_count;
    size_t vm_frame_idx;
    basl_source_span_t span;
    basl_source_location_t location;
    const basl_chunk_t *chunk;
    size_t ip;

    vm_frame_count = basl_vm_frame_depth(dbg->vm);
    if (vm_frame_count == 0U) return 0;

    /* frame_index 0 = innermost (top of stack). */
    vm_frame_idx = vm_frame_count - 1U - frame_index;
    chunk = basl_vm_frame_chunk(dbg->vm, vm_frame_idx);
    ip = basl_vm_frame_ip(dbg->vm, vm_frame_idx);

    if (chunk == NULL) return 0;
    span = basl_chunk_span_at(chunk, ip);

    basl_source_location_clear(&location);
    location.source_id = span.source_id;
    location.offset = span.start_offset;
    if (basl_source_registry_resolve_location(dbg->sources, &location, NULL) != BASL_STATUS_OK) {
        return 0;
    }

    if (out_source_id != NULL) *out_source_id = location.source_id;
    if (out_line != NULL) *out_line = location.line;
    if (out_column != NULL) *out_column = location.column;
    return 1;
}

static int basl_debugger_check_breakpoint(
    const basl_debugger_t *dbg,
    basl_source_id_t source_id,
    uint32_t line
) {
    size_t i;
    for (i = 0U; i < dbg->breakpoint_count; i += 1U) {
        if (dbg->breakpoints[i].active &&
            dbg->breakpoints[i].source_id == source_id &&
            dbg->breakpoints[i].line == line) {
            return 1;
        }
    }
    return 0;
}

static void basl_debugger_invoke_callback(
    basl_debugger_t *dbg,
    basl_debug_stop_reason_t reason
) {
    basl_debug_action_t action;

    dbg->is_paused = 1;
    if (dbg->callback != NULL) {
        action = dbg->callback(dbg, reason, dbg->callback_userdata);
        if (action == BASL_DEBUG_CONTINUE) {
            dbg->is_paused = 0;
            dbg->mode = BASL_DEBUG_MODE_RUN;
        }
    }
}

/* ── VM hook ─────────────────────────────────────────────────────── */

static int basl_debugger_vm_hook(basl_vm_t *vm, void *userdata) {
    basl_debugger_t *dbg = (basl_debugger_t *)userdata;
    basl_source_id_t source_id = 0U;
    uint32_t line = 0U;
    uint32_t column = 0U;
    size_t current_depth;
    int should_stop = 0;

    (void)vm;

    if (!basl_debugger_resolve_ip(dbg, 0U, &source_id, &line, &column)) {
        return 0;  /* can't resolve — keep running */
    }

    current_depth = basl_vm_frame_depth(dbg->vm);

    switch (dbg->mode) {
    case BASL_DEBUG_MODE_RUN:
        should_stop = basl_debugger_check_breakpoint(dbg, source_id, line);
        break;
    case BASL_DEBUG_MODE_PAUSE:
        should_stop = 1;
        break;
    case BASL_DEBUG_MODE_STEP_INTO:
        /* Stop at any new source line. */
        should_stop = (source_id != dbg->step_start_source ||
                       line != dbg->step_start_line);
        break;
    case BASL_DEBUG_MODE_STEP_OVER:
        /* Stop at a new source line at the same or shallower depth. */
        should_stop = (current_depth <= dbg->step_frame_depth &&
                       (source_id != dbg->step_start_source ||
                        line != dbg->step_start_line));
        break;
    case BASL_DEBUG_MODE_STEP_OUT:
        /* Stop when we return to a shallower frame. */
        should_stop = (current_depth < dbg->step_frame_depth);
        break;
    }

    /* Also check breakpoints in stepping modes. */
    if (!should_stop && dbg->mode != BASL_DEBUG_MODE_RUN) {
        should_stop = basl_debugger_check_breakpoint(dbg, source_id, line);
    }

    if (should_stop) {
        basl_debug_stop_reason_t reason =
            (dbg->mode == BASL_DEBUG_MODE_RUN)
                ? BASL_DEBUG_STOP_BREAKPOINT
                : BASL_DEBUG_STOP_STEP;
        basl_debugger_invoke_callback(dbg, reason);
    }

    return 0;  /* 0 = keep executing */
}

/* ── Lifecycle ───────────────────────────────────────────────────── */

basl_status_t basl_debugger_create(
    basl_debugger_t **out_debugger,
    basl_vm_t *vm,
    const basl_source_registry_t *sources,
    basl_error_t *error
) {
    void *memory = NULL;
    basl_runtime_t *runtime;
    basl_status_t status;

    if (out_debugger == NULL || vm == NULL || sources == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "debugger arguments must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_debugger = NULL;
    runtime = basl_vm_runtime(vm);
    status = basl_runtime_alloc(runtime, sizeof(basl_debugger_t), &memory, error);
    if (status != BASL_STATUS_OK) return status;

    memset(memory, 0, sizeof(basl_debugger_t));
    *out_debugger = (basl_debugger_t *)memory;
    (*out_debugger)->runtime = runtime;
    (*out_debugger)->vm = vm;
    (*out_debugger)->sources = sources;
    (*out_debugger)->mode = BASL_DEBUG_MODE_RUN;
    return BASL_STATUS_OK;
}

void basl_debugger_destroy(basl_debugger_t **debugger) {
    void *memory;
    if (debugger == NULL || *debugger == NULL) return;
    basl_debugger_detach(*debugger);
    if ((*debugger)->breakpoints != NULL) {
        memory = (*debugger)->breakpoints;
        basl_runtime_free((*debugger)->runtime, &memory);
    }
    memory = *debugger;
    basl_runtime_free((*debugger)->runtime, &memory);
    *debugger = NULL;
}

void basl_debugger_attach(basl_debugger_t *debugger) {
    if (debugger == NULL) return;
    basl_vm_set_debug_hook(debugger->vm, basl_debugger_vm_hook, debugger);
}

void basl_debugger_detach(basl_debugger_t *debugger) {
    if (debugger == NULL) return;
    basl_vm_set_debug_hook(debugger->vm, NULL, NULL);
    debugger->is_paused = 0;
}

void basl_debugger_set_callback(
    basl_debugger_t *debugger,
    basl_debug_callback_t callback,
    void *userdata
) {
    if (debugger == NULL) return;
    debugger->callback = callback;
    debugger->callback_userdata = userdata;
}

/* ── Breakpoints ─────────────────────────────────────────────────── */

basl_status_t basl_debugger_set_breakpoint(
    basl_debugger_t *debugger,
    basl_source_id_t source_id,
    uint32_t line,
    size_t *out_breakpoint_id,
    basl_error_t *error
) {
    basl_breakpoint_t *bp;

    if (debugger == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Reuse an inactive slot. */
    for (size_t i = 0U; i < debugger->breakpoint_count; i += 1U) {
        if (!debugger->breakpoints[i].active) {
            debugger->breakpoints[i].source_id = source_id;
            debugger->breakpoints[i].line = line;
            debugger->breakpoints[i].active = 1;
            if (out_breakpoint_id != NULL) *out_breakpoint_id = i;
            return BASL_STATUS_OK;
        }
    }

    /* Grow. */
    if (debugger->breakpoint_count >= debugger->breakpoint_capacity) {
        size_t new_cap = debugger->breakpoint_capacity == 0U
                             ? 8U
                             : debugger->breakpoint_capacity * 2U;
        void *memory = NULL;
        basl_status_t status = basl_runtime_alloc(
            debugger->runtime,
            new_cap * sizeof(basl_breakpoint_t),
            &memory, error
        );
        if (status != BASL_STATUS_OK) return status;
        if (debugger->breakpoints != NULL) {
            memcpy(memory, debugger->breakpoints,
                   debugger->breakpoint_count * sizeof(basl_breakpoint_t));
            void *old = debugger->breakpoints;
            basl_runtime_free(debugger->runtime, &old);
        }
        debugger->breakpoints = (basl_breakpoint_t *)memory;
        debugger->breakpoint_capacity = new_cap;
    }

    bp = &debugger->breakpoints[debugger->breakpoint_count];
    bp->source_id = source_id;
    bp->line = line;
    bp->active = 1;
    if (out_breakpoint_id != NULL) *out_breakpoint_id = debugger->breakpoint_count;
    debugger->breakpoint_count += 1U;
    return BASL_STATUS_OK;
}

basl_status_t basl_debugger_set_breakpoint_function(
    basl_debugger_t *debugger,
    const char *function_name,
    size_t *out_breakpoint_id,
    basl_error_t *error
) {
    size_t i, count;
    size_t name_len;
    const basl_debug_symbol_t *sym;
    basl_source_location_t loc;

    if (debugger == NULL || function_name == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "debugger and function_name must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (debugger->symbols == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "no symbol table attached to debugger");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    name_len = strlen(function_name);
    count = basl_debug_symbol_table_count(debugger->symbols);

    for (i = 0; i < count; i++) {
        sym = basl_debug_symbol_table_get(debugger->symbols, i);
        if (sym->kind != BASL_DEBUG_SYMBOL_FUNCTION &&
            sym->kind != BASL_DEBUG_SYMBOL_METHOD) {
            continue;
        }
        if (sym->name_length == name_len &&
            memcmp(sym->name, function_name, name_len) == 0) {
            /* Found it - resolve span to line number */
            basl_source_location_clear(&loc);
            loc.source_id = sym->span.source_id;
            loc.offset = sym->span.start_offset;
            if (basl_source_registry_resolve_location(debugger->sources, &loc, NULL) != BASL_STATUS_OK) {
                basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                                       "failed to resolve function location");
                return BASL_STATUS_INTERNAL;
            }
            /* Set breakpoint on line after declaration (first line of body) */
            return basl_debugger_set_breakpoint(debugger, loc.source_id,
                                                 loc.line + 1, out_breakpoint_id, error);
        }
    }

    basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                           "function not found");
    return BASL_STATUS_INVALID_ARGUMENT;
}

void basl_debugger_set_symbols(
    basl_debugger_t *debugger,
    const basl_debug_symbol_table_t *symbols
) {
    if (debugger != NULL) {
        debugger->symbols = symbols;
    }
}

basl_status_t basl_debugger_clear_breakpoint(
    basl_debugger_t *debugger,
    size_t breakpoint_id
) {
    if (debugger == NULL || breakpoint_id >= debugger->breakpoint_count) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    debugger->breakpoints[breakpoint_id].active = 0;
    return BASL_STATUS_OK;
}

void basl_debugger_clear_all_breakpoints(basl_debugger_t *debugger) {
    size_t i;
    if (debugger == NULL) return;
    for (i = 0U; i < debugger->breakpoint_count; i += 1U) {
        debugger->breakpoints[i].active = 0;
    }
}

/* ── Execution control ───────────────────────────────────────────── */

static void basl_debugger_begin_step(basl_debugger_t *debugger) {
    debugger->step_frame_depth = basl_vm_frame_depth(debugger->vm);
    debugger->step_start_line = 0U;
    debugger->step_start_source = 0U;
    basl_debugger_resolve_ip(
        debugger, 0U,
        &debugger->step_start_source,
        &debugger->step_start_line,
        NULL
    );
    debugger->is_paused = 0;
}

void basl_debugger_step_over(basl_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = BASL_DEBUG_MODE_STEP_OVER;
    basl_debugger_begin_step(debugger);
}

void basl_debugger_step_into(basl_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = BASL_DEBUG_MODE_STEP_INTO;
    basl_debugger_begin_step(debugger);
}

void basl_debugger_step_out(basl_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = BASL_DEBUG_MODE_STEP_OUT;
    basl_debugger_begin_step(debugger);
}

void basl_debugger_continue(basl_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = BASL_DEBUG_MODE_RUN;
    debugger->is_paused = 0;
}

void basl_debugger_pause(basl_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = BASL_DEBUG_MODE_PAUSE;
}

/* ── Inspection ──────────────────────────────────────────────────── */

basl_status_t basl_debugger_current_location(
    const basl_debugger_t *debugger,
    basl_source_id_t *out_source_id,
    uint32_t *out_line,
    uint32_t *out_column
) {
    if (debugger == NULL) return BASL_STATUS_INVALID_ARGUMENT;
    if (!basl_debugger_resolve_ip(debugger, 0U, out_source_id, out_line, out_column)) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    return BASL_STATUS_OK;
}

size_t basl_debugger_frame_count(const basl_debugger_t *debugger) {
    if (debugger == NULL) return 0U;
    return basl_vm_frame_depth(debugger->vm);
}

basl_status_t basl_debugger_frame_info(
    const basl_debugger_t *debugger,
    size_t frame_index,
    const char **out_function_name,
    size_t *out_name_length,
    basl_source_id_t *out_source_id,
    uint32_t *out_line,
    uint32_t *out_column
) {
    size_t vm_frame_count;
    size_t vm_frame_idx;

    if (debugger == NULL) return BASL_STATUS_INVALID_ARGUMENT;

    vm_frame_count = basl_vm_frame_depth(debugger->vm);
    if (frame_index >= vm_frame_count) return BASL_STATUS_INVALID_ARGUMENT;

    vm_frame_idx = vm_frame_count - 1U - frame_index;

    /* Get function name from the frame's function object. */
    {
        const basl_object_t *fn_obj = basl_vm_frame_function(debugger->vm, vm_frame_idx);
        if (fn_obj != NULL) {
            const char *name = basl_function_object_name(fn_obj);
            if (out_function_name != NULL) *out_function_name = name;
            if (out_name_length != NULL) *out_name_length = name != NULL ? strlen(name) : 0U;
        } else {
            if (out_function_name != NULL) *out_function_name = NULL;
            if (out_name_length != NULL) *out_name_length = 0U;
        }
    }

    basl_debugger_resolve_ip(debugger, frame_index, out_source_id, out_line, out_column);
    return BASL_STATUS_OK;
}

size_t basl_debugger_frame_locals(
    const basl_debugger_t *debugger,
    size_t frame_index,
    const char **out_names,
    size_t *out_name_lengths,
    basl_value_t *out_values,
    size_t max_locals
) {
    size_t vm_frame_count;
    size_t vm_frame_idx;
    const basl_chunk_t *chunk;
    size_t ip;
    size_t base_slot;
    size_t count = 0U;
    size_t i;

    if (debugger == NULL || max_locals == 0U) return 0U;

    vm_frame_count = basl_vm_frame_depth(debugger->vm);
    if (frame_index >= vm_frame_count) return 0U;
    vm_frame_idx = vm_frame_count - 1U - frame_index;

    chunk = basl_vm_frame_chunk(debugger->vm, vm_frame_idx);
    ip = basl_vm_frame_ip(debugger->vm, vm_frame_idx);
    base_slot = basl_vm_frame_base_slot(debugger->vm, vm_frame_idx);

    if (chunk == NULL) return 0U;

    for (i = 0U; i < basl_debug_local_table_count(&chunk->debug_locals); i += 1U) {
        const basl_debug_local_t *local = basl_debug_local_table_get(&chunk->debug_locals, i);
        if (local == NULL) continue;
        if (ip >= local->scope_start_ip &&
            (local->scope_end_ip == SIZE_MAX || ip < local->scope_end_ip)) {
            if (count >= max_locals) break;
            if (out_names != NULL) out_names[count] = local->name;
            if (out_name_lengths != NULL) out_name_lengths[count] = local->name_length;
            if (out_values != NULL) {
                size_t stack_slot = base_slot + local->slot;
                out_values[count] = basl_vm_stack_get(debugger->vm, stack_slot);
            }
            count += 1U;
        }
    }

    return count;
}

basl_status_t basl_debugger_get_local(
    const basl_debugger_t *debugger,
    size_t frame_index,
    const char *name,
    basl_value_t *out_value
) {
    size_t vm_frame_count;
    size_t vm_frame_idx;
    const basl_chunk_t *chunk;
    size_t ip;
    size_t base_slot;
    size_t name_len;
    size_t i;

    if (debugger == NULL || name == NULL || out_value == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    name_len = strlen(name);
    vm_frame_count = basl_vm_frame_depth(debugger->vm);
    if (frame_index >= vm_frame_count) return BASL_STATUS_INVALID_ARGUMENT;
    vm_frame_idx = vm_frame_count - 1U - frame_index;

    chunk = basl_vm_frame_chunk(debugger->vm, vm_frame_idx);
    ip = basl_vm_frame_ip(debugger->vm, vm_frame_idx);
    base_slot = basl_vm_frame_base_slot(debugger->vm, vm_frame_idx);

    if (chunk == NULL) return BASL_STATUS_INVALID_ARGUMENT;

    for (i = 0U; i < basl_debug_local_table_count(&chunk->debug_locals); i += 1U) {
        const basl_debug_local_t *local = basl_debug_local_table_get(&chunk->debug_locals, i);
        if (local == NULL) continue;
        if (ip >= local->scope_start_ip &&
            (local->scope_end_ip == SIZE_MAX || ip < local->scope_end_ip)) {
            if (local->name_length == name_len &&
                memcmp(local->name, name, name_len) == 0) {
                size_t stack_slot = base_slot + local->slot;
                *out_value = basl_vm_stack_get(debugger->vm, stack_slot);
                return BASL_STATUS_OK;
            }
        }
    }

    return BASL_STATUS_INVALID_ARGUMENT;
}
