#ifndef BASL_DEBUGGER_H
#define BASL_DEBUGGER_H

#include <stddef.h>

#include "basl/debug_info.h"
#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/value.h"
#include "basl/vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Debugger state ──────────────────────────────────────────────── */

typedef struct basl_debugger basl_debugger_t;

typedef enum basl_debug_action {
    BASL_DEBUG_CONTINUE = 0,    /* resume execution */
    BASL_DEBUG_PAUSE = 1        /* stay paused (callback returns this) */
} basl_debug_action_t;

typedef enum basl_debug_stop_reason {
    BASL_DEBUG_STOP_BREAKPOINT = 0,
    BASL_DEBUG_STOP_STEP = 1,
    BASL_DEBUG_STOP_ENTRY = 2
} basl_debug_stop_reason_t;

/**
 * Called when the VM pauses (breakpoint hit, step completed, etc.).
 * Return BASL_DEBUG_CONTINUE to resume, BASL_DEBUG_PAUSE to stay paused.
 * While paused, the caller can inspect locals, stack, and call frames.
 */
typedef basl_debug_action_t (*basl_debug_callback_t)(
    basl_debugger_t *debugger,
    basl_debug_stop_reason_t reason,
    void *userdata
);

/* ── Lifecycle ───────────────────────────────────────────────────── */

BASL_API basl_status_t basl_debugger_create(
    basl_debugger_t **out_debugger,
    basl_vm_t *vm,
    const basl_source_registry_t *sources,
    basl_error_t *error
);
BASL_API void basl_debugger_destroy(basl_debugger_t **debugger);

/**
 * Attach the debugger to the VM. While attached, the VM checks for
 * breakpoints and step conditions before each opcode.
 * Call before basl_vm_execute_function().
 */
BASL_API void basl_debugger_attach(basl_debugger_t *debugger);

/** Detach the debugger. VM runs at full speed. */
BASL_API void basl_debugger_detach(basl_debugger_t *debugger);

/** Set the callback invoked when the VM pauses. */
BASL_API void basl_debugger_set_callback(
    basl_debugger_t *debugger,
    basl_debug_callback_t callback,
    void *userdata
);

/* ── Breakpoints ─────────────────────────────────────────────────── */

BASL_API basl_status_t basl_debugger_set_breakpoint(
    basl_debugger_t *debugger,
    basl_source_id_t source_id,
    uint32_t line,
    size_t *out_breakpoint_id,
    basl_error_t *error
);
BASL_API basl_status_t basl_debugger_clear_breakpoint(
    basl_debugger_t *debugger,
    size_t breakpoint_id
);
BASL_API void basl_debugger_clear_all_breakpoints(
    basl_debugger_t *debugger
);

/* ── Execution control ───────────────────────────────────────────── */

/** Step one source line (step over function calls). */
BASL_API void basl_debugger_step_over(basl_debugger_t *debugger);

/** Step into the next function call. */
BASL_API void basl_debugger_step_into(basl_debugger_t *debugger);

/** Step out of the current function. */
BASL_API void basl_debugger_step_out(basl_debugger_t *debugger);

/** Resume execution until next breakpoint or completion. */
BASL_API void basl_debugger_continue(basl_debugger_t *debugger);

/** Request a pause at the next opportunity. */
BASL_API void basl_debugger_pause(basl_debugger_t *debugger);

/* ── Inspection (valid while paused) ─────────────────────────────── */

/** Get the current source location where execution is paused. */
BASL_API basl_status_t basl_debugger_current_location(
    const basl_debugger_t *debugger,
    basl_source_id_t *out_source_id,
    uint32_t *out_line,
    uint32_t *out_column
);

/** Get the call stack depth. */
BASL_API size_t basl_debugger_frame_count(
    const basl_debugger_t *debugger
);

/** Get info about a call frame (0 = innermost). */
BASL_API basl_status_t basl_debugger_frame_info(
    const basl_debugger_t *debugger,
    size_t frame_index,
    const char **out_function_name,
    size_t *out_name_length,
    basl_source_id_t *out_source_id,
    uint32_t *out_line,
    uint32_t *out_column
);

/**
 * Get locals in scope for a given frame.
 * Returns the number of locals written to out_names/out_values.
 * out_names[i] points into debug info (do not free).
 * out_values[i] are copies (caller must release).
 */
BASL_API size_t basl_debugger_frame_locals(
    const basl_debugger_t *debugger,
    size_t frame_index,
    const char **out_names,
    size_t *out_name_lengths,
    basl_value_t *out_values,
    size_t max_locals
);

#ifdef __cplusplus
}
#endif

#endif
