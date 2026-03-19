#ifndef VIGIL_DEBUGGER_H
#define VIGIL_DEBUGGER_H

#include <stddef.h>

#include "vigil/debug_info.h"
#include "vigil/export.h"
#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/status.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Debugger state ──────────────────────────────────────────────── */

    typedef struct vigil_debugger vigil_debugger_t;

    typedef enum vigil_debug_action
    {
        VIGIL_DEBUG_CONTINUE = 0, /* resume execution */
        VIGIL_DEBUG_PAUSE = 1     /* stay paused (callback returns this) */
    } vigil_debug_action_t;

    typedef enum vigil_debug_stop_reason
    {
        VIGIL_DEBUG_STOP_BREAKPOINT = 0,
        VIGIL_DEBUG_STOP_STEP = 1,
        VIGIL_DEBUG_STOP_ENTRY = 2
    } vigil_debug_stop_reason_t;

    /**
     * Called when the VM pauses (breakpoint hit, step completed, etc.).
     * Return VIGIL_DEBUG_CONTINUE to resume, VIGIL_DEBUG_PAUSE to stay paused.
     * While paused, the caller can inspect locals, stack, and call frames.
     */
    typedef vigil_debug_action_t (*vigil_debug_callback_t)(vigil_debugger_t *debugger, vigil_debug_stop_reason_t reason,
                                                           void *userdata);

    /* ── Lifecycle ───────────────────────────────────────────────────── */

    VIGIL_API vigil_status_t vigil_debugger_create(vigil_debugger_t **out_debugger, vigil_vm_t *vm,
                                                   const vigil_source_registry_t *sources, vigil_error_t *error);
    VIGIL_API void vigil_debugger_destroy(vigil_debugger_t **debugger);

    /**
     * Attach the debugger to the VM. While attached, the VM checks for
     * breakpoints and step conditions before each opcode.
     * Call before vigil_vm_execute_function().
     */
    VIGIL_API void vigil_debugger_attach(vigil_debugger_t *debugger);

    /** Detach the debugger. VM runs at full speed. */
    VIGIL_API void vigil_debugger_detach(vigil_debugger_t *debugger);

    /** Set the callback invoked when the VM pauses. */
    VIGIL_API void vigil_debugger_set_callback(vigil_debugger_t *debugger, vigil_debug_callback_t callback,
                                               void *userdata);

    /* ── Breakpoints ─────────────────────────────────────────────────── */

    VIGIL_API vigil_status_t vigil_debugger_set_breakpoint(vigil_debugger_t *debugger, vigil_source_id_t source_id,
                                                           uint32_t line, size_t *out_breakpoint_id,
                                                           vigil_error_t *error);

    /**
     * Set a breakpoint on a function by name.
     * Requires symbol table to be set via vigil_debugger_set_symbols().
     */
    VIGIL_API vigil_status_t vigil_debugger_set_breakpoint_function(vigil_debugger_t *debugger,
                                                                    const char *function_name,
                                                                    size_t *out_breakpoint_id, vigil_error_t *error);

    VIGIL_API vigil_status_t vigil_debugger_clear_breakpoint(vigil_debugger_t *debugger, size_t breakpoint_id);
    VIGIL_API void vigil_debugger_clear_all_breakpoints(vigil_debugger_t *debugger);

    /**
     * Attach a symbol table for function-name breakpoints.
     * The debugger does not take ownership; caller must keep it alive.
     */
    VIGIL_API void vigil_debugger_set_symbols(vigil_debugger_t *debugger, const vigil_debug_symbol_table_t *symbols);

    /* ── Execution control ───────────────────────────────────────────── */

    /** Step one source line (step over function calls). */
    VIGIL_API void vigil_debugger_step_over(vigil_debugger_t *debugger);

    /** Step into the next function call. */
    VIGIL_API void vigil_debugger_step_into(vigil_debugger_t *debugger);

    /** Step out of the current function. */
    VIGIL_API void vigil_debugger_step_out(vigil_debugger_t *debugger);

    /** Resume execution until next breakpoint or completion. */
    VIGIL_API void vigil_debugger_continue(vigil_debugger_t *debugger);

    /** Request a pause at the next opportunity. */
    VIGIL_API void vigil_debugger_pause(vigil_debugger_t *debugger);

    /* ── Inspection (valid while paused) ─────────────────────────────── */

    /** Get the current source location where execution is paused. */
    VIGIL_API vigil_status_t vigil_debugger_current_location(const vigil_debugger_t *debugger,
                                                             vigil_source_id_t *out_source_id, uint32_t *out_line,
                                                             uint32_t *out_column);

    /** Get the call stack depth. */
    VIGIL_API size_t vigil_debugger_frame_count(const vigil_debugger_t *debugger);

    /** Get info about a call frame (0 = innermost). */
    VIGIL_API vigil_status_t vigil_debugger_frame_info(const vigil_debugger_t *debugger, size_t frame_index,
                                                       const char **out_function_name, size_t *out_name_length,
                                                       vigil_source_id_t *out_source_id, uint32_t *out_line,
                                                       uint32_t *out_column);

    /**
     * Get locals in scope for a given frame.
     * Returns the number of locals written to out_names/out_values.
     * out_names[i] points into debug info (do not free).
     * out_values[i] are copies (caller must release).
     */
    VIGIL_API size_t vigil_debugger_frame_locals(const vigil_debugger_t *debugger, size_t frame_index,
                                                 const char **out_names, size_t *out_name_lengths,
                                                 vigil_value_t *out_values, size_t max_locals);

    /**
     * Look up a local variable by name in the given frame.
     * Returns VIGIL_STATUS_OK if found, VIGIL_STATUS_INVALID_ARGUMENT otherwise.
     */
    VIGIL_API vigil_status_t vigil_debugger_get_local(const vigil_debugger_t *debugger, size_t frame_index,
                                                      const char *name, vigil_value_t *out_value);

#ifdef __cplusplus
}
#endif

#endif
