/* VIGIL debugger: breakpoints, stepping, and variable inspection.
 *
 * The debugger attaches to a VM via a lightweight hook function that
 * is checked before each opcode dispatch.  When no debugger is attached
 * the hook pointer is NULL and the VM runs at full speed.
 */
#include <string.h>

#include "vigil/debugger.h"
#include "vigil/chunk.h"
#include "vigil/status.h"

#include "internal/vigil_internal.h"

/* ── Internal types ──────────────────────────────────────────────── */

typedef enum vigil_debug_mode {
    VIGIL_DEBUG_MODE_RUN = 0,
    VIGIL_DEBUG_MODE_PAUSE = 1,
    VIGIL_DEBUG_MODE_STEP_OVER = 2,
    VIGIL_DEBUG_MODE_STEP_INTO = 3,
    VIGIL_DEBUG_MODE_STEP_OUT = 4
} vigil_debug_mode_t;

typedef struct vigil_breakpoint {
    vigil_source_id_t source_id;
    uint32_t line;
    int active;
} vigil_breakpoint_t;

struct vigil_debugger {
    vigil_runtime_t *runtime;
    vigil_vm_t *vm;
    const vigil_source_registry_t *sources;
    const vigil_debug_symbol_table_t *symbols;
    vigil_debug_callback_t callback;
    void *callback_userdata;
    vigil_debug_mode_t mode;
    /* For step-over/step-out: the frame depth when stepping started. */
    size_t step_frame_depth;
    /* For step-over: the source line when stepping started. */
    uint32_t step_start_line;
    vigil_source_id_t step_start_source;
    /* Breakpoints. */
    vigil_breakpoint_t *breakpoints;
    size_t breakpoint_count;
    size_t breakpoint_capacity;
    /* Cached pause state. */
    int is_paused;
};

/* ── Forward declarations ────────────────────────────────────────── */

static int vigil_debugger_vm_hook(vigil_vm_t *vm, void *userdata);

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Resolve the current IP to a source location. */
static int vigil_debugger_resolve_ip(
    const vigil_debugger_t *dbg,
    size_t frame_index,
    vigil_source_id_t *out_source_id,
    uint32_t *out_line,
    uint32_t *out_column
) {
    size_t vm_frame_count;
    size_t vm_frame_idx;
    vigil_source_span_t span;
    vigil_source_location_t location;
    const vigil_chunk_t *chunk;
    size_t ip;

    vm_frame_count = vigil_vm_frame_depth(dbg->vm);
    if (vm_frame_count == 0U) return 0;

    /* frame_index 0 = innermost (top of stack). */
    vm_frame_idx = vm_frame_count - 1U - frame_index;
    chunk = vigil_vm_frame_chunk(dbg->vm, vm_frame_idx);
    ip = vigil_vm_frame_ip(dbg->vm, vm_frame_idx);

    if (chunk == NULL) return 0;
    span = vigil_chunk_span_at(chunk, ip);

    vigil_source_location_clear(&location);
    location.source_id = span.source_id;
    location.offset = span.start_offset;
    if (vigil_source_registry_resolve_location(dbg->sources, &location, NULL) != VIGIL_STATUS_OK) {
        return 0;
    }

    if (out_source_id != NULL) *out_source_id = location.source_id;
    if (out_line != NULL) *out_line = location.line;
    if (out_column != NULL) *out_column = location.column;
    return 1;
}

static int vigil_debugger_check_breakpoint(
    const vigil_debugger_t *dbg,
    vigil_source_id_t source_id,
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

static void vigil_debugger_invoke_callback(
    vigil_debugger_t *dbg,
    vigil_debug_stop_reason_t reason
) {
    vigil_debug_action_t action;

    dbg->is_paused = 1;
    if (dbg->callback != NULL) {
        action = dbg->callback(dbg, reason, dbg->callback_userdata);
        if (action == VIGIL_DEBUG_CONTINUE) {
            dbg->is_paused = 0;
            dbg->mode = VIGIL_DEBUG_MODE_RUN;
        }
    }
}

/* ── VM hook ─────────────────────────────────────────────────────── */

static int vigil_debugger_vm_hook(vigil_vm_t *vm, void *userdata) {
    vigil_debugger_t *dbg = (vigil_debugger_t *)userdata;
    vigil_source_id_t source_id = 0U;
    uint32_t line = 0U;
    uint32_t column = 0U;
    size_t current_depth;
    int should_stop = 0;

    (void)vm;

    if (!vigil_debugger_resolve_ip(dbg, 0U, &source_id, &line, &column)) {
        return 0;  /* can't resolve — keep running */
    }

    current_depth = vigil_vm_frame_depth(dbg->vm);

    switch (dbg->mode) {
    case VIGIL_DEBUG_MODE_RUN:
        should_stop = vigil_debugger_check_breakpoint(dbg, source_id, line);
        break;
    case VIGIL_DEBUG_MODE_PAUSE:
        should_stop = 1;
        break;
    case VIGIL_DEBUG_MODE_STEP_INTO:
        /* Stop at any new source line. */
        should_stop = (source_id != dbg->step_start_source ||
                       line != dbg->step_start_line);
        break;
    case VIGIL_DEBUG_MODE_STEP_OVER:
        /* Stop at a new source line at the same or shallower depth. */
        should_stop = (current_depth <= dbg->step_frame_depth &&
                       (source_id != dbg->step_start_source ||
                        line != dbg->step_start_line));
        break;
    case VIGIL_DEBUG_MODE_STEP_OUT:
        /* Stop when we return to a shallower frame. */
        should_stop = (current_depth < dbg->step_frame_depth);
        break;
    }

    /* Also check breakpoints in stepping modes. */
    if (!should_stop && dbg->mode != VIGIL_DEBUG_MODE_RUN) {
        should_stop = vigil_debugger_check_breakpoint(dbg, source_id, line);
    }

    if (should_stop) {
        vigil_debug_stop_reason_t reason =
            (dbg->mode == VIGIL_DEBUG_MODE_RUN)
                ? VIGIL_DEBUG_STOP_BREAKPOINT
                : VIGIL_DEBUG_STOP_STEP;
        vigil_debugger_invoke_callback(dbg, reason);
    }

    return 0;  /* 0 = keep executing */
}

/* ── Lifecycle ───────────────────────────────────────────────────── */

vigil_status_t vigil_debugger_create(
    vigil_debugger_t **out_debugger,
    vigil_vm_t *vm,
    const vigil_source_registry_t *sources,
    vigil_error_t *error
) {
    void *memory = NULL;
    vigil_runtime_t *runtime;
    vigil_status_t status;

    if (out_debugger == NULL || vm == NULL || sources == NULL) {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                               "debugger arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_debugger = NULL;
    runtime = vigil_vm_runtime(vm);
    status = vigil_runtime_alloc(runtime, sizeof(vigil_debugger_t), &memory, error);
    if (status != VIGIL_STATUS_OK) return status;

    memset(memory, 0, sizeof(vigil_debugger_t));
    *out_debugger = (vigil_debugger_t *)memory;
    (*out_debugger)->runtime = runtime;
    (*out_debugger)->vm = vm;
    (*out_debugger)->sources = sources;
    (*out_debugger)->mode = VIGIL_DEBUG_MODE_RUN;
    return VIGIL_STATUS_OK;
}

void vigil_debugger_destroy(vigil_debugger_t **debugger) {
    void *memory;
    if (debugger == NULL || *debugger == NULL) return;
    vigil_debugger_detach(*debugger);
    if ((*debugger)->breakpoints != NULL) {
        memory = (*debugger)->breakpoints;
        vigil_runtime_free((*debugger)->runtime, &memory);
    }
    memory = *debugger;
    vigil_runtime_free((*debugger)->runtime, &memory);
    *debugger = NULL;
}

void vigil_debugger_attach(vigil_debugger_t *debugger) {
    if (debugger == NULL) return;
    vigil_vm_set_debug_hook(debugger->vm, vigil_debugger_vm_hook, debugger);
}

void vigil_debugger_detach(vigil_debugger_t *debugger) {
    if (debugger == NULL) return;
    vigil_vm_set_debug_hook(debugger->vm, NULL, NULL);
    debugger->is_paused = 0;
}

void vigil_debugger_set_callback(
    vigil_debugger_t *debugger,
    vigil_debug_callback_t callback,
    void *userdata
) {
    if (debugger == NULL) return;
    debugger->callback = callback;
    debugger->callback_userdata = userdata;
}

/* ── Breakpoints ─────────────────────────────────────────────────── */

vigil_status_t vigil_debugger_set_breakpoint(
    vigil_debugger_t *debugger,
    vigil_source_id_t source_id,
    uint32_t line,
    size_t *out_breakpoint_id,
    vigil_error_t *error
) {
    vigil_breakpoint_t *bp;

    if (debugger == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    /* Reuse an inactive slot. */
    for (size_t i = 0U; i < debugger->breakpoint_count; i += 1U) {
        if (!debugger->breakpoints[i].active) {
            debugger->breakpoints[i].source_id = source_id;
            debugger->breakpoints[i].line = line;
            debugger->breakpoints[i].active = 1;
            if (out_breakpoint_id != NULL) *out_breakpoint_id = i;
            return VIGIL_STATUS_OK;
        }
    }

    /* Grow. */
    if (debugger->breakpoint_count >= debugger->breakpoint_capacity) {
        size_t new_cap = debugger->breakpoint_capacity == 0U
                             ? 8U
                             : debugger->breakpoint_capacity * 2U;
        void *memory = NULL;
        vigil_status_t status = vigil_runtime_alloc(
            debugger->runtime,
            new_cap * sizeof(vigil_breakpoint_t),
            &memory, error
        );
        if (status != VIGIL_STATUS_OK) return status;
        if (debugger->breakpoints != NULL) {
            memcpy(memory, debugger->breakpoints,
                   debugger->breakpoint_count * sizeof(vigil_breakpoint_t));
            void *old = debugger->breakpoints;
            vigil_runtime_free(debugger->runtime, &old);
        }
        debugger->breakpoints = (vigil_breakpoint_t *)memory;
        debugger->breakpoint_capacity = new_cap;
    }

    bp = &debugger->breakpoints[debugger->breakpoint_count];
    bp->source_id = source_id;
    bp->line = line;
    bp->active = 1;
    if (out_breakpoint_id != NULL) *out_breakpoint_id = debugger->breakpoint_count;
    debugger->breakpoint_count += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_debugger_set_breakpoint_function(
    vigil_debugger_t *debugger,
    const char *function_name,
    size_t *out_breakpoint_id,
    vigil_error_t *error
) {
    size_t i, count;
    size_t name_len;
    const vigil_debug_symbol_t *sym;
    vigil_source_location_t loc;

    if (debugger == NULL || function_name == NULL) {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                               "debugger and function_name must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (debugger->symbols == NULL) {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                               "no symbol table attached to debugger");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    name_len = strlen(function_name);
    count = vigil_debug_symbol_table_count(debugger->symbols);

    for (i = 0; i < count; i++) {
        sym = vigil_debug_symbol_table_get(debugger->symbols, i);
        if (sym->kind != VIGIL_DEBUG_SYMBOL_FUNCTION &&
            sym->kind != VIGIL_DEBUG_SYMBOL_METHOD) {
            continue;
        }
        if (sym->name_length == name_len &&
            memcmp(sym->name, function_name, name_len) == 0) {
            /* Found it - resolve span to line number */
            vigil_source_location_clear(&loc);
            loc.source_id = sym->span.source_id;
            loc.offset = sym->span.start_offset;
            if (vigil_source_registry_resolve_location(debugger->sources, &loc, NULL) != VIGIL_STATUS_OK) {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL,
                                       "failed to resolve function location");
                return VIGIL_STATUS_INTERNAL;
            }
            /* Set breakpoint on line after declaration (first line of body) */
            return vigil_debugger_set_breakpoint(debugger, loc.source_id,
                                                 loc.line + 1, out_breakpoint_id, error);
        }
    }

    vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                           "function not found");
    return VIGIL_STATUS_INVALID_ARGUMENT;
}

void vigil_debugger_set_symbols(
    vigil_debugger_t *debugger,
    const vigil_debug_symbol_table_t *symbols
) {
    if (debugger != NULL) {
        debugger->symbols = symbols;
    }
}

vigil_status_t vigil_debugger_clear_breakpoint(
    vigil_debugger_t *debugger,
    size_t breakpoint_id
) {
    if (debugger == NULL || breakpoint_id >= debugger->breakpoint_count) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    debugger->breakpoints[breakpoint_id].active = 0;
    return VIGIL_STATUS_OK;
}

void vigil_debugger_clear_all_breakpoints(vigil_debugger_t *debugger) {
    size_t i;
    if (debugger == NULL) return;
    for (i = 0U; i < debugger->breakpoint_count; i += 1U) {
        debugger->breakpoints[i].active = 0;
    }
}

/* ── Execution control ───────────────────────────────────────────── */

static void vigil_debugger_begin_step(vigil_debugger_t *debugger) {
    debugger->step_frame_depth = vigil_vm_frame_depth(debugger->vm);
    debugger->step_start_line = 0U;
    debugger->step_start_source = 0U;
    vigil_debugger_resolve_ip(
        debugger, 0U,
        &debugger->step_start_source,
        &debugger->step_start_line,
        NULL
    );
    debugger->is_paused = 0;
}

void vigil_debugger_step_over(vigil_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = VIGIL_DEBUG_MODE_STEP_OVER;
    vigil_debugger_begin_step(debugger);
}

void vigil_debugger_step_into(vigil_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = VIGIL_DEBUG_MODE_STEP_INTO;
    vigil_debugger_begin_step(debugger);
}

void vigil_debugger_step_out(vigil_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = VIGIL_DEBUG_MODE_STEP_OUT;
    vigil_debugger_begin_step(debugger);
}

void vigil_debugger_continue(vigil_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = VIGIL_DEBUG_MODE_RUN;
    debugger->is_paused = 0;
}

void vigil_debugger_pause(vigil_debugger_t *debugger) {
    if (debugger == NULL) return;
    debugger->mode = VIGIL_DEBUG_MODE_PAUSE;
}

/* ── Inspection ──────────────────────────────────────────────────── */

vigil_status_t vigil_debugger_current_location(
    const vigil_debugger_t *debugger,
    vigil_source_id_t *out_source_id,
    uint32_t *out_line,
    uint32_t *out_column
) {
    if (debugger == NULL) return VIGIL_STATUS_INVALID_ARGUMENT;
    if (!vigil_debugger_resolve_ip(debugger, 0U, out_source_id, out_line, out_column)) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    return VIGIL_STATUS_OK;
}

size_t vigil_debugger_frame_count(const vigil_debugger_t *debugger) {
    if (debugger == NULL) return 0U;
    return vigil_vm_frame_depth(debugger->vm);
}

vigil_status_t vigil_debugger_frame_info(
    const vigil_debugger_t *debugger,
    size_t frame_index,
    const char **out_function_name,
    size_t *out_name_length,
    vigil_source_id_t *out_source_id,
    uint32_t *out_line,
    uint32_t *out_column
) {
    size_t vm_frame_count;
    size_t vm_frame_idx;

    if (debugger == NULL) return VIGIL_STATUS_INVALID_ARGUMENT;

    vm_frame_count = vigil_vm_frame_depth(debugger->vm);
    if (frame_index >= vm_frame_count) return VIGIL_STATUS_INVALID_ARGUMENT;

    vm_frame_idx = vm_frame_count - 1U - frame_index;

    /* Get function name from the frame's function object. */
    {
        const vigil_object_t *fn_obj = vigil_vm_frame_function(debugger->vm, vm_frame_idx);
        if (fn_obj != NULL) {
            const char *name = vigil_function_object_name(fn_obj);
            if (out_function_name != NULL) *out_function_name = name;
            if (out_name_length != NULL) *out_name_length = name != NULL ? strlen(name) : 0U;
        } else {
            if (out_function_name != NULL) *out_function_name = NULL;
            if (out_name_length != NULL) *out_name_length = 0U;
        }
    }

    vigil_debugger_resolve_ip(debugger, frame_index, out_source_id, out_line, out_column);
    return VIGIL_STATUS_OK;
}

size_t vigil_debugger_frame_locals(
    const vigil_debugger_t *debugger,
    size_t frame_index,
    const char **out_names,
    size_t *out_name_lengths,
    vigil_value_t *out_values,
    size_t max_locals
) {
    size_t vm_frame_count;
    size_t vm_frame_idx;
    const vigil_chunk_t *chunk;
    size_t ip;
    size_t base_slot;
    size_t count = 0U;
    size_t i;

    if (debugger == NULL || max_locals == 0U) return 0U;

    vm_frame_count = vigil_vm_frame_depth(debugger->vm);
    if (frame_index >= vm_frame_count) return 0U;
    vm_frame_idx = vm_frame_count - 1U - frame_index;

    chunk = vigil_vm_frame_chunk(debugger->vm, vm_frame_idx);
    ip = vigil_vm_frame_ip(debugger->vm, vm_frame_idx);
    base_slot = vigil_vm_frame_base_slot(debugger->vm, vm_frame_idx);

    if (chunk == NULL) return 0U;

    for (i = 0U; i < vigil_debug_local_table_count(&chunk->debug_locals); i += 1U) {
        const vigil_debug_local_t *local = vigil_debug_local_table_get(&chunk->debug_locals, i);
        if (local == NULL) continue;
        if (ip >= local->scope_start_ip &&
            (local->scope_end_ip == SIZE_MAX || ip < local->scope_end_ip)) {
            if (count >= max_locals) break;
            if (out_names != NULL) out_names[count] = local->name;
            if (out_name_lengths != NULL) out_name_lengths[count] = local->name_length;
            if (out_values != NULL) {
                size_t stack_slot = base_slot + local->slot;
                out_values[count] = vigil_vm_stack_get(debugger->vm, stack_slot);
            }
            count += 1U;
        }
    }

    return count;
}

vigil_status_t vigil_debugger_get_local(
    const vigil_debugger_t *debugger,
    size_t frame_index,
    const char *name,
    vigil_value_t *out_value
) {
    size_t vm_frame_count;
    size_t vm_frame_idx;
    const vigil_chunk_t *chunk;
    size_t ip;
    size_t base_slot;
    size_t name_len;
    size_t i;

    if (debugger == NULL || name == NULL || out_value == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    name_len = strlen(name);
    vm_frame_count = vigil_vm_frame_depth(debugger->vm);
    if (frame_index >= vm_frame_count) return VIGIL_STATUS_INVALID_ARGUMENT;
    vm_frame_idx = vm_frame_count - 1U - frame_index;

    chunk = vigil_vm_frame_chunk(debugger->vm, vm_frame_idx);
    ip = vigil_vm_frame_ip(debugger->vm, vm_frame_idx);
    base_slot = vigil_vm_frame_base_slot(debugger->vm, vm_frame_idx);

    if (chunk == NULL) return VIGIL_STATUS_INVALID_ARGUMENT;

    for (i = 0U; i < vigil_debug_local_table_count(&chunk->debug_locals); i += 1U) {
        const vigil_debug_local_t *local = vigil_debug_local_table_get(&chunk->debug_locals, i);
        if (local == NULL) continue;
        if (ip >= local->scope_start_ip &&
            (local->scope_end_ip == SIZE_MAX || ip < local->scope_end_ip)) {
            if (local->name_length == name_len &&
                memcmp(local->name, name, name_len) == 0) {
                size_t stack_slot = base_slot + local->slot;
                *out_value = vigil_vm_stack_get(debugger->vm, stack_slot);
                return VIGIL_STATUS_OK;
            }
        }
    }

    return VIGIL_STATUS_INVALID_ARGUMENT;
}
