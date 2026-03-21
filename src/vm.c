/*
 * vm.c — VIGIL bytecode virtual machine
 *
 * ═══════════════════════════════════════════════════════════════════
 *  PERFORMANCE-CRITICAL FILE — READ BEFORE MAKING CHANGES
 * ═══════════════════════════════════════════════════════════════════
 *
 * This file contains the bytecode dispatch loop, which is the single
 * hottest code path in the entire runtime.  Several deliberate
 * trade-offs have been made to reduce per-opcode overhead:
 *
 *   1. COMPUTED GOTO DISPATCH (GCC/Clang only)
 *      Instead of a switch statement, the dispatch loop uses a jump
 *      table (goto *dispatch_table[opcode]).  This avoids the branch
 *      predictor penalty of a single indirect branch that a switch
 *      compiles to.  An ISO C11 switch fallback is always compiled
 *      when VIGIL_VM_COMPUTED_GOTO == 0 (e.g. MSVC).
 *
 *   2. INLINE VALUE MACROS (VIGIL_VM_VALUE_*)
 *      The public API functions (vigil_value_kind, vigil_value_as_int,
 *      vigil_value_copy, vigil_value_release, etc.) perform NULL checks
 *      and go through a function-call boundary.  Inside the dispatch
 *      loop we access struct fields directly and skip ref-counting
 *      for non-object values (int, uint, float, bool, nil).  This is
 *      safe because the compiler has already validated types.
 *
 *      TRADEOFF: Direct field access bypasses the safety checks in
 *      the public API.  If the VM ever executes untrusted bytecode,
 *      these macros would need bounds/type guards added back.
 *
 *   3. INLINE STACK / BYTECODE MACROS (VIGIL_VM_PUSH, _POP, _READ_U32)
 *      Replace vigil_vm_push(), vigil_vm_pop_or_nil(), and
 *      vigil_vm_read_u32() function calls with inline operations.
 *      The pre-allocated stack (256 slots) means the capacity check
 *      in VIGIL_VM_PUSH almost never triggers.
 *
 *   4. FAST-PATH RETURN
 *      The RETURN opcode has an inlined fast path for the common case:
 *      single return value, no defers, returning to a caller frame.
 *      This avoids a heap allocation (vigil_vm_grow_value_array) and
 *      the full vigil_vm_complete_return() call on every return.
 *
 *      TRADEOFF: This duplicates return logic.  If return semantics
 *      change, BOTH the fast path and vigil_vm_complete_return() must
 *      be updated.  The fast path is guarded by:
 *        - operand == 1  (single return value)
 *        - defer_count == 0  (no defers on this frame)
 *        - !draining_defers  (not mid-defer execution)
 *        - frame_count > 1  (not the top-level frame)
 *        - caller not draining defers
 *      If ANY of these conditions is false, the slow path runs.
 *
 * ─── DEVELOPER CHECKLIST: ADDING A NEW OPCODE ─────────────────────
 *
 *   [ ] Add the VM_CASE(OPNAME) handler in the dispatch loop
 *   [ ] Add [VIGIL_OPCODE_OPNAME] = &&op_OPNAME to the dispatch table
 *       (inside the #if VIGIL_VM_COMPUTED_GOTO block, ~line 2590)
 *   [ ] Use VM_BREAK() at the end (or VM_BREAK_RELOAD() if the
 *       opcode changes the call frame — see CALL, RETURN)
 *   [ ] Use the VIGIL_VM_* macros for stack/value ops, not the
 *       public API functions, for consistency and performance
 *   [ ] NEVER use bare "break;" inside an opcode handler in the
 *       computed-goto path — it breaks out of the while(1) loop
 *       instead of dispatching the next opcode.  Use VM_BREAK().
 *       (break inside an inner for/while/switch is fine.)
 *
 * ─── DEVELOPER CHECKLIST: CHANGING RETURN SEMANTICS ───────────────
 *
 *   [ ] Update vigil_vm_complete_return() (the general path)
 *   [ ] Update the fast-path RETURN in VM_CASE(RETURN)
 *   [ ] If adding new frame cleanup steps, ensure the fast-path
 *       guard excludes frames that need the new cleanup
 *   [ ] Run the defer tests — they are the canary for fast-path bugs
 *
 * ─── PORTABILITY ──────────────────────────────────────────────────
 *
 *   - All macros are ISO C11.  The only extension is computed goto.
 *   - VIGIL_VM_COMPUTED_GOTO is auto-detected: 1 for GCC/Clang,
 *     0 for everything else.  The #else branch MUST remain 0.
 *   - _Pragma("GCC diagnostic push/pop") suppresses -Wpedantic for
 *     the computed-goto block only.  MSVC ignores it (warning C4068).
 *   - To test the switch fallback locally, temporarily set
 *     VIGIL_VM_COMPUTED_GOTO to 0 and run the full test suite.
 *
 * ═══════════════════════════════════════════════════════════════════
 */

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "value_internal.h"
#include "vigil/string.h"
#include "vigil/vm.h"

/* ── Computed-goto detection ───────────────────────────────────────
   GCC/Clang: use dispatch table.  Everything else: switch fallback.
   Per docs/stdlib-portability.md the core VM must compile as ISO C11;
   the extension is gated behind a compiler check. */
#if defined(__GNUC__) || defined(__clang__)
#define VIGIL_VM_COMPUTED_GOTO 1
#else
#define VIGIL_VM_COMPUTED_GOTO 0
#endif

/* Portable overflow-checked i32 arithmetic.
   GCC/Clang: single-instruction __builtin_*_overflow.
   MSVC: manual range checks (i32 ops can't overflow i64). */
#if defined(__GNUC__) || defined(__clang__)
#define VIGIL_I32_ADD_OVERFLOW(a, b, r) __builtin_add_overflow(a, b, r)
#define VIGIL_I32_SUB_OVERFLOW(a, b, r) __builtin_sub_overflow(a, b, r)
#define VIGIL_I32_MUL_OVERFLOW(a, b, r) __builtin_mul_overflow(a, b, r)
#else
#define VIGIL_I32_ADD_OVERFLOW(a, b, r)                                                                                \
    (*(r) = (int32_t)((int64_t)(a) + (int64_t)(b)),                                                                    \
     (int64_t)(a) + (int64_t)(b) < (int64_t)INT32_MIN || (int64_t)(a) + (int64_t)(b) > (int64_t)INT32_MAX)
#define VIGIL_I32_SUB_OVERFLOW(a, b, r)                                                                                \
    (*(r) = (int32_t)((int64_t)(a) - (int64_t)(b)),                                                                    \
     (int64_t)(a) - (int64_t)(b) < (int64_t)INT32_MIN || (int64_t)(a) - (int64_t)(b) > (int64_t)INT32_MAX)
#define VIGIL_I32_MUL_OVERFLOW(a, b, r)                                                                                \
    (*(r) = (int32_t)((int64_t)(a) * (int64_t)(b)),                                                                    \
     (int64_t)(a) * (int64_t)(b) < (int64_t)INT32_MIN || (int64_t)(a) * (int64_t)(b) > (int64_t)INT32_MAX)
#endif

#define VIGIL_VM_INITIAL_STACK_CAPACITY 256U
#define VIGIL_VM_INITIAL_FRAME_CAPACITY 64U

/* ── Inline value helpers ───────────────────────────────────────────
   These replace the public vigil_value_* API inside the dispatch loop.
   For non-object values (int, uint, float, bool, nil) they skip the
   function call, NULL check, and ref-counting overhead entirely.
   Objects still go through vigil_object_retain / vigil_object_release.

   WARNING: These bypass the safety checks in value.h.  They assume
   the caller has already validated the value kind.  Do NOT use
   outside the dispatch loop without equivalent guards. */

#define VIGIL_VM_VALUE_INIT_NIL(v)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        *(v) = VIGIL_NANBOX_NIL;                                                                                       \
    } while (0)

#define VIGIL_VM_VALUE_COPY(dst, src)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        *(dst) = *(src);                                                                                               \
        if (vigil_nanbox_has_object(*(dst)))                                                                           \
            vigil_object_retain((vigil_object_t *)vigil_nanbox_decode_ptr(*(dst)));                                    \
    } while (0)

#define VIGIL_VM_VALUE_RELEASE(v)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (vigil_nanbox_has_object(*(v)))                                                                             \
        {                                                                                                              \
            vigil_object_t *_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(*(v));                                    \
            vigil_object_release(&_obj);                                                                               \
        }                                                                                                              \
        *(v) = VIGIL_NANBOX_NIL;                                                                                       \
    } while (0)

/* Fast stack push — caller must ensure capacity (pre-allocated). */
#define VIGIL_VM_PUSH(vm, val)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((vm)->stack_count >= (vm)->stack_capacity)                                                                 \
        {                                                                                                              \
            status = vigil_vm_grow_stack((vm), (vm)->stack_count + 1U, error);                                         \
            if (status != VIGIL_STATUS_OK)                                                                             \
                goto cleanup;                                                                                          \
        }                                                                                                              \
        VIGIL_VM_VALUE_COPY(&(vm)->stack[(vm)->stack_count], (val));                                                   \
        (vm)->stack_count += 1U;                                                                                       \
    } while (0)

/* Fast stack pop — returns value, clears slot. */
#define VIGIL_VM_POP(vm, out)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        (vm)->stack_count -= 1U;                                                                                       \
        (out) = (vm)->stack[(vm)->stack_count];                                                                        \
        (vm)->stack[(vm)->stack_count] = VIGIL_NANBOX_NIL;                                                             \
    } while (0)

/* Fast peek — no NULL check, caller knows stack is non-empty. */
#define VIGIL_VM_PEEK(vm, dist) (&(vm)->stack[(vm)->stack_count - 1U - (dist)])

/* Fast bytecode read — reads u32 operand after the opcode byte.
   Advances ip past opcode + 4 operand bytes (total 5). */
#define VIGIL_VM_READ_U32(code, ip, out)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        (out) = (uint32_t)(code)[(ip) + 1U];                                                                           \
        (out) |= (uint32_t)(code)[(ip) + 2U] << 8U;                                                                    \
        (out) |= (uint32_t)(code)[(ip) + 3U] << 16U;                                                                   \
        (out) |= (uint32_t)(code)[(ip) + 4U] << 24U;                                                                   \
        (ip) += 5U;                                                                                                    \
    } while (0)

/* Fast raw u32 read — reads 4 bytes at current ip (no opcode skip).
   Advances ip by 4. */
#define VIGIL_VM_READ_RAW_U32(code, ip, out)                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        (out) = (uint32_t)(code)[(ip)];                                                                                \
        (out) |= (uint32_t)(code)[(ip) + 1U] << 8U;                                                                    \
        (out) |= (uint32_t)(code)[(ip) + 2U] << 16U;                                                                   \
        (out) |= (uint32_t)(code)[(ip) + 3U] << 24U;                                                                   \
        (ip) += 4U;                                                                                                    \
    } while (0)

/* Direct chunk field access — avoids function-call overhead for
   vigil_chunk_code(), vigil_chunk_code_size(), vigil_chunk_constant(). */
#define VIGIL_VM_CHUNK_CODE(chunk) ((chunk)->code.data)
#define VIGIL_VM_CHUNK_CODE_SIZE(chunk) ((chunk)->code.length)
#define VIGIL_VM_CHUNK_CONSTANT(chunk, idx) ((idx) < (chunk)->constant_count ? &(chunk)->constants[(idx)] : NULL)

typedef struct vigil_vm_frame
{
    const vigil_object_t *callable;
    const vigil_object_t *function;
    const vigil_chunk_t *chunk;
    size_t ip;
    size_t base_slot;
    struct vigil_vm_defer_action *defers;
    size_t defer_count;
    size_t defer_capacity;
    vigil_value_t *pending_returns;
    size_t pending_return_count;
    size_t pending_return_capacity;
    int draining_defers;
} vigil_vm_frame_t;

typedef enum vigil_vm_defer_kind
{
    VIGIL_VM_DEFER_CALL = 0,
    VIGIL_VM_DEFER_CALL_VALUE = 1,
    VIGIL_VM_DEFER_NEW_INSTANCE = 2,
    VIGIL_VM_DEFER_CALL_INTERFACE = 3,
    VIGIL_VM_DEFER_CALL_NATIVE = 4
} vigil_vm_defer_kind_t;

typedef struct vigil_vm_defer_action
{
    vigil_vm_defer_kind_t kind;
    uint32_t operand_a;
    uint32_t operand_b;
    uint32_t arg_count;
    vigil_value_t *values;
    size_t value_count;
} vigil_vm_defer_action_t;

struct vigil_vm
{
    vigil_runtime_t *runtime;
    vigil_value_t *stack;
    size_t stack_count;
    size_t stack_capacity;
    vigil_vm_frame_t *frames;
    size_t frame_count;
    size_t frame_capacity;
    /* Debug hook — NULL when no debugger attached (zero overhead). */
    int (*debug_hook)(vigil_vm_t *vm, void *userdata);
    void *debug_hook_userdata;
    /* Script arguments for args.get(). */
    const char *const *argv;
    size_t argc;
};

static vigil_status_t vigil_vm_fail_at_ip(vigil_vm_t *vm, vigil_status_t status, const char *message,
                                          vigil_error_t *error);
static vigil_value_t vigil_vm_pop_or_nil(vigil_vm_t *vm);
static void vigil_vm_defer_action_clear(vigil_runtime_t *runtime, vigil_vm_defer_action_t *action);
static vigil_status_t vigil_vm_complete_return(vigil_vm_t *vm, vigil_value_t *returned_values, size_t return_count,
                                               vigil_value_t *out_value, vigil_error_t *error);

static vigil_status_t vigil_vm_validate(const vigil_vm_t *vm, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (vm == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "vm must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vm->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "vm runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static void vigil_vm_release_stack(vigil_vm_t *vm)
{
    size_t i;

    if (vm == NULL)
    {
        return;
    }

    for (i = 0U; i < vm->stack_count; ++i)
    {
        vigil_value_release(&vm->stack[i]);
    }

    vm->stack_count = 0U;
}

static void vigil_vm_defer_action_clear(vigil_runtime_t *runtime, vigil_vm_defer_action_t *action)
{
    size_t i;
    void *memory;

    if (action == NULL)
    {
        return;
    }

    for (i = 0U; i < action->value_count; i += 1U)
    {
        vigil_value_release(&action->values[i]);
    }
    memory = action->values;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }
    memset(action, 0, sizeof(*action));
}

static vigil_status_t vigil_vm_grow_value_array(vigil_runtime_t *runtime, vigil_value_t **values, size_t *capacity,
                                                size_t minimum_capacity, vigil_error_t *error)
{
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= *capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = *capacity;
    next_capacity = old_capacity == 0U ? 2U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / sizeof(**values))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm value array overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = *values;
    if (memory == NULL)
    {
        status = vigil_runtime_alloc(runtime, next_capacity * sizeof(**values), &memory, error);
    }
    else
    {
        status = vigil_runtime_realloc(runtime, &memory, next_capacity * sizeof(**values), error);
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *values = (vigil_value_t *)memory;
    *capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static void vigil_vm_frame_clear(vigil_runtime_t *runtime, vigil_vm_frame_t *frame)
{
    size_t i;
    void *memory;

    if (frame == NULL)
    {
        return;
    }

    for (i = 0U; i < frame->defer_count; i += 1U)
    {
        vigil_vm_defer_action_clear(runtime, &frame->defers[i]);
    }
    memory = frame->defers;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }
    for (i = 0U; i < frame->pending_return_count; i += 1U)
    {
        vigil_value_release(&frame->pending_returns[i]);
    }
    memory = frame->pending_returns;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }
    memset(frame, 0, sizeof(*frame));
}

static void vigil_vm_clear_frames(vigil_vm_t *vm)
{
    size_t i;

    if (vm == NULL)
    {
        return;
    }

    for (i = 0U; i < vm->frame_count; i += 1U)
    {
        vigil_vm_frame_clear(vm->runtime, &vm->frames[i]);
    }
    vm->frame_count = 0U;
}

static void vigil_vm_unwind_stack_to(vigil_vm_t *vm, size_t target_count)
{
    vigil_value_t value;

    if (vm == NULL)
    {
        return;
    }

    while (vm->stack_count > target_count)
    {
        value = vigil_vm_pop_or_nil(vm);
        VIGIL_VM_VALUE_RELEASE(&value);
    }
}

static vigil_status_t vigil_vm_grow_stack(vigil_vm_t *vm, size_t minimum_capacity, vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= vm->stack_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = vm->stack_capacity;
    next_capacity = old_capacity == 0U ? 16U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*vm->stack)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm stack allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (vm->stack == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(vm->runtime, next_capacity * sizeof(*vm->stack), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else
    {
        memory = vm->stack;
        status = vigil_runtime_realloc(vm->runtime, &memory, next_capacity * sizeof(*vm->stack), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        memset((vigil_value_t *)memory + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*vm->stack));
    }

    vm->stack = (vigil_value_t *)memory;
    vm->stack_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_grow_frames(vigil_vm_t *vm, size_t minimum_capacity, vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (minimum_capacity <= vm->frame_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = vm->frame_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > (SIZE_MAX / 2U))
        {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*vm->frames)))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm frame allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (vm->frames == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(vm->runtime, next_capacity * sizeof(*vm->frames), &memory, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    else
    {
        memory = vm->frames;
        status = vigil_runtime_realloc(vm->runtime, &memory, next_capacity * sizeof(*vm->frames), error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        memset((vigil_vm_frame_t *)memory + old_capacity, 0, (next_capacity - old_capacity) * sizeof(*vm->frames));
    }

    vm->frames = (vigil_vm_frame_t *)memory;
    vm->frame_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_vm_frame_t *vigil_vm_current_frame(vigil_vm_t *vm)
{
    if (vm == NULL || vm->frame_count == 0U)
    {
        return NULL;
    }

    return &vm->frames[vm->frame_count - 1U];
}

static vigil_status_t vigil_vm_push_frame(vigil_vm_t *vm, const vigil_object_t *callable,
                                          const vigil_object_t *function, const vigil_chunk_t *chunk, size_t base_slot,
                                          vigil_error_t *error)
{
    vigil_status_t status;
    vigil_vm_frame_t *frame;

    if (vm->frame_count == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm frame stack overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_vm_grow_frames(vm, vm->frame_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    frame = &vm->frames[vm->frame_count];
    memset(frame, 0, sizeof(*frame));
    frame->callable = callable;
    frame->function = function;
    frame->chunk = chunk;
    frame->ip = 0U;
    frame->base_slot = base_slot;
    vm->frame_count += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_push(vigil_vm_t *vm, const vigil_value_t *value, vigil_error_t *error)
{
    vigil_status_t status;

    if (vm->stack_count == SIZE_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm stack overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_vm_grow_stack(vm, vm->stack_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vm->stack[vm->stack_count] = vigil_value_copy(value);
    vm->stack_count += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_value_t vigil_vm_pop_or_nil(vigil_vm_t *vm)
{
    vigil_value_t value;

    VIGIL_VM_VALUE_INIT_NIL(&value);
    if (vm == NULL || vm->stack_count == 0U)
    {
        return value;
    }

    value = vm->stack[vm->stack_count - 1U];
    VIGIL_VM_VALUE_INIT_NIL(&vm->stack[vm->stack_count - 1U]);
    vm->stack_count -= 1U;
    return value;
}

static const vigil_value_t *vigil_vm_peek(const vigil_vm_t *vm, size_t distance)
{
    if (vm == NULL || distance >= vm->stack_count)
    {
        return NULL;
    }

    return &vm->stack[vm->stack_count - 1U - distance];
}

static vigil_status_t vigil_vm_frame_grow_defers(vigil_vm_t *vm, vigil_vm_frame_t *frame, size_t minimum_capacity,
                                                 vigil_error_t *error)
{
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    vigil_status_t status;

    if (frame == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "vm frame must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (minimum_capacity <= frame->defer_capacity)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    old_capacity = frame->defer_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity)
    {
        if (next_capacity > SIZE_MAX / 2U)
        {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / sizeof(*frame->defers))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm defer allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (frame->defers == NULL)
    {
        memory = NULL;
        status = vigil_runtime_alloc(vm->runtime, next_capacity * sizeof(*frame->defers), &memory, error);
    }
    else
    {
        memory = frame->defers;
        status = vigil_runtime_realloc(vm->runtime, &memory, next_capacity * sizeof(*frame->defers), error);
        if (status == VIGIL_STATUS_OK)
        {
            memset((vigil_vm_defer_action_t *)memory + old_capacity, 0,
                   (next_capacity - old_capacity) * sizeof(*frame->defers));
        }
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    frame->defers = (vigil_vm_defer_action_t *)memory;
    frame->defer_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_copy_values(vigil_vm_t *vm, const vigil_value_t *values, size_t value_count,
                                           vigil_value_t **out_values, vigil_error_t *error)
{
    vigil_status_t status;
    void *memory;
    size_t i;

    if (out_values == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_values must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_values = NULL;
    if (value_count == 0U)
    {
        return VIGIL_STATUS_OK;
    }

    if (value_count > SIZE_MAX / sizeof(*values))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "vm defer value allocation overflow");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = NULL;
    status = vigil_runtime_alloc(vm->runtime, value_count * sizeof(*values), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    *out_values = (vigil_value_t *)memory;
    for (i = 0U; i < value_count; i += 1U)
    {
        (*out_values)[i] = vigil_value_copy(&values[i]);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_invoke_call(vigil_vm_t *vm, vigil_vm_frame_t *frame, size_t function_index,
                                           size_t arg_count, vigil_error_t *error)
{
    const vigil_object_t *callee;
    size_t base_slot;

    if (frame == NULL || frame->function == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "call requires a function-backed frame");
        return VIGIL_STATUS_INTERNAL;
    }

    callee = vigil_function_object_sibling(frame->function, function_index);
    if (callee == NULL || vigil_object_type(callee) != VIGIL_OBJECT_FUNCTION)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "call target is invalid");
        return VIGIL_STATUS_INTERNAL;
    }
    if (vigil_function_object_arity(callee) != arg_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "call arity does not match function signature");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (arg_count > vm->stack_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "call arguments are missing from the stack");
        return VIGIL_STATUS_INTERNAL;
    }

    base_slot = vm->stack_count - arg_count;
    return vigil_vm_push_frame(vm, callee, callee, vigil_function_object_chunk(callee), base_slot, error);
}

static vigil_status_t vigil_vm_invoke_value_call(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_value_t callee_value;
    vigil_object_t *callee;
    const vigil_object_t *function;
    size_t callee_slot;
    vigil_status_t status;

    if (arg_count + 1U > vm->stack_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "call arguments are missing from the stack");
        return VIGIL_STATUS_INTERNAL;
    }

    callee_slot = vm->stack_count - (arg_count + 1U);
    callee_value = vm->stack[callee_slot];
    callee = ((vigil_object_t *)vigil_nanbox_decode_ptr(callee_value));
    if (!vigil_nanbox_is_object(callee_value) || callee == NULL ||
        (vigil_object_type(callee) != VIGIL_OBJECT_FUNCTION && vigil_object_type(callee) != VIGIL_OBJECT_CLOSURE))
    {
        vigil_value_release(&callee_value);
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "call target is not a function");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (vigil_callable_object_arity(callee) != arg_count)
    {
        vigil_value_release(&callee_value);
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "call arity does not match function signature");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_object_retain(callee);
    function = vigil_callable_object_function(callee);
    if (arg_count != 0U)
    {
        memmove(&vm->stack[callee_slot], &vm->stack[callee_slot + 1U], arg_count * sizeof(*vm->stack));
        vm->stack_count -= 1U;
    }
    else
    {
        vm->stack_count -= 1U;
    }
    vigil_value_release(&callee_value);
    status = vigil_vm_push_frame(vm, callee, function, vigil_callable_object_chunk(callee), callee_slot, error);
    vigil_object_release(&callee);
    return status;
}

static vigil_status_t vigil_vm_invoke_interface_call(vigil_vm_t *vm, vigil_vm_frame_t *frame, size_t interface_index,
                                                     size_t method_index, size_t arg_count, vigil_error_t *error)
{
    const vigil_object_t *callee;
    const vigil_value_t *receiver;
    size_t base_slot;
    size_t class_index;

    if (arg_count + 1U > vm->stack_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "call arguments are missing from the stack");
        return VIGIL_STATUS_INTERNAL;
    }

    base_slot = vm->stack_count - (arg_count + 1U);
    receiver = &vm->stack[base_slot];
    if (!vigil_nanbox_is_object(*receiver) ||
        vigil_object_type((vigil_object_t *)vigil_nanbox_decode_ptr(*receiver)) != VIGIL_OBJECT_INSTANCE)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "interface call requires a class instance receiver");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (frame == NULL || frame->function == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "call requires a function-backed frame");
        return VIGIL_STATUS_INTERNAL;
    }

    class_index = vigil_instance_object_class_index((vigil_object_t *)vigil_nanbox_decode_ptr(*receiver));
    callee =
        vigil_function_object_resolve_interface_method(frame->function, class_index, interface_index, method_index);
    if (callee == NULL || vigil_object_type(callee) != VIGIL_OBJECT_FUNCTION)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "interface call target is invalid");
        return VIGIL_STATUS_INTERNAL;
    }
    if (vigil_function_object_arity(callee) != arg_count + 1U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "call arity does not match function signature");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_vm_push_frame(vm, callee, callee, vigil_function_object_chunk(callee), base_slot, error);
}

static vigil_status_t vigil_vm_invoke_new_instance(vigil_vm_t *vm, size_t class_index, size_t field_count,
                                                   int discard_result, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *instance;
    vigil_value_t value;
    size_t base_slot;

    if (field_count > vm->stack_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "constructor arguments are missing from the stack");
        return VIGIL_STATUS_INTERNAL;
    }

    base_slot = vm->stack_count - field_count;
    instance = NULL;
    status = vigil_instance_object_new(vm->runtime, class_index, field_count > 0U ? vm->stack + base_slot : NULL,
                                       field_count, &instance, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (vm->stack_count > base_slot)
    {
        value = vigil_vm_pop_or_nil(vm);
        VIGIL_VM_VALUE_RELEASE(&value);
    }

    vigil_value_init_object(&value, &instance);
    if (discard_result)
    {
        VIGIL_VM_VALUE_RELEASE(&value);
        return VIGIL_STATUS_OK;
    }
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

static vigil_status_t vigil_vm_invoke_new_array(vigil_vm_t *vm, size_t type_index, size_t item_count,
                                                vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *array_object;
    vigil_value_t value;
    size_t base_slot;

    (void)type_index;
    if (item_count > vm->stack_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "array elements are missing from the stack");
        return VIGIL_STATUS_INTERNAL;
    }

    base_slot = vm->stack_count - item_count;
    array_object = NULL;
    status = vigil_array_object_new(vm->runtime, base_slot == vm->stack_count ? NULL : vm->stack + base_slot,
                                    item_count, &array_object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_vm_unwind_stack_to(vm, base_slot);
    vigil_value_init_object(&value, &array_object);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

static vigil_status_t vigil_vm_invoke_new_map(vigil_vm_t *vm, size_t type_index, size_t pair_count,
                                              vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *map_object;
    const vigil_value_t *key_value;
    const vigil_value_t *entry_value;
    vigil_value_t value;
    size_t base_slot;
    size_t pair_index;

    (void)type_index;
    if (pair_count > SIZE_MAX / 2U || pair_count * 2U > vm->stack_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "map entries are missing from the stack");
        return VIGIL_STATUS_INTERNAL;
    }

    base_slot = vm->stack_count - (pair_count * 2U);
    map_object = NULL;
    status = vigil_map_object_new(vm->runtime, &map_object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    for (pair_index = 0U; pair_index < pair_count; pair_index += 1U)
    {
        key_value = &vm->stack[base_slot + (pair_index * 2U)];
        entry_value = &vm->stack[base_slot + (pair_index * 2U) + 1U];
        status = vigil_map_object_set(map_object, key_value, entry_value, error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_object_release(&map_object);
            return status;
        }
    }

    vigil_vm_unwind_stack_to(vm, base_slot);
    vigil_value_init_object(&value, &map_object);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

static vigil_status_t vigil_vm_schedule_defer(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_vm_defer_kind_t kind,
                                              uint32_t operand_a, uint32_t operand_b, uint32_t arg_count,
                                              size_t value_count, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_vm_defer_action_t *action;
    size_t base_slot;
    vigil_value_t value;

    if (frame == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "vm frame is missing");
        return VIGIL_STATUS_INTERNAL;
    }
    if (value_count > vm->stack_count)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "defer arguments are missing from the stack");
        return VIGIL_STATUS_INTERNAL;
    }

    status = vigil_vm_frame_grow_defers(vm, frame, frame->defer_count + 1U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    action = &frame->defers[frame->defer_count];
    memset(action, 0, sizeof(*action));
    action->kind = kind;
    action->operand_a = operand_a;
    action->operand_b = operand_b;
    action->arg_count = arg_count;
    action->value_count = value_count;

    base_slot = vm->stack_count - value_count;
    status = vigil_vm_copy_values(vm, vm->stack + base_slot, value_count, &action->values, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_defer_action_clear(vm->runtime, action);
        return status;
    }

    while (vm->stack_count > base_slot)
    {
        value = vigil_vm_pop_or_nil(vm);
        VIGIL_VM_VALUE_RELEASE(&value);
    }
    frame->defer_count += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_execute_next_defer(vigil_vm_t *vm, vigil_vm_frame_t *frame, int *out_pushed_frame,
                                                  vigil_error_t *error)
{
    vigil_status_t status;
    vigil_vm_defer_action_t action;
    size_t i;

    if (out_pushed_frame != NULL)
    {
        *out_pushed_frame = 0;
    }
    if (frame == NULL || frame->defer_count == 0U)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "no deferred call is available");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    action = frame->defers[frame->defer_count - 1U];
    memset(&frame->defers[frame->defer_count - 1U], 0, sizeof(frame->defers[frame->defer_count - 1U]));
    frame->defer_count -= 1U;

    for (i = 0U; i < action.value_count; i += 1U)
    {
        status = vigil_vm_push(vm, &action.values[i], error);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_vm_defer_action_clear(vm->runtime, &action);
            return status;
        }
    }

    switch (action.kind)
    {
    case VIGIL_VM_DEFER_CALL:
        status = vigil_vm_invoke_call(vm, frame, (size_t)action.operand_a, (size_t)action.arg_count, error);
        if (status == VIGIL_STATUS_OK && out_pushed_frame != NULL)
        {
            *out_pushed_frame = 1;
        }
        break;
    case VIGIL_VM_DEFER_CALL_VALUE:
        status = vigil_vm_invoke_value_call(vm, (size_t)action.arg_count, error);
        if (status == VIGIL_STATUS_OK && out_pushed_frame != NULL)
        {
            *out_pushed_frame = 1;
        }
        break;
    case VIGIL_VM_DEFER_CALL_INTERFACE:
        status = vigil_vm_invoke_interface_call(vm, frame, (size_t)action.operand_a, (size_t)action.operand_b,
                                                (size_t)action.arg_count, error);
        if (status == VIGIL_STATUS_OK && out_pushed_frame != NULL)
        {
            *out_pushed_frame = 1;
        }
        break;
    case VIGIL_VM_DEFER_NEW_INSTANCE:
        status = vigil_vm_invoke_new_instance(vm, (size_t)action.operand_a, (size_t)action.arg_count, 1, error);
        break;
    case VIGIL_VM_DEFER_CALL_NATIVE: {
        const vigil_value_t *nval;
        vigil_object_t *nobj;
        vigil_native_fn_t nfn;
        nval = VIGIL_VM_CHUNK_CONSTANT(frame->chunk, (size_t)action.operand_a);
        nobj = (vigil_object_t *)vigil_nanbox_decode_ptr(*nval);
        nfn = vigil_native_function_get(nobj);
        if (nfn == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "deferred call target is not a native function");
            status = VIGIL_STATUS_INTERNAL;
        }
        else
        {
            status = nfn(vm, (size_t)action.arg_count, error);
        }
        break;
    }
    default:
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "defer target is invalid");
        status = VIGIL_STATUS_INTERNAL;
        break;
    }

    vigil_vm_defer_action_clear(vm->runtime, &action);
    return status;
}

static vigil_status_t vigil_vm_complete_return(vigil_vm_t *vm, vigil_value_t *returned_values, size_t return_count,
                                               vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t *current_values;
    size_t current_count;
    size_t i;

    current_values = returned_values;
    current_count = return_count;
    while (1)
    {
        vigil_vm_frame_t *frame;
        size_t base_slot;
        int pushed_frame;

        frame = vigil_vm_current_frame(vm);
        if (frame == NULL)
        {
            for (i = 0U; i < current_count; i += 1U)
            {
                vigil_value_release(&current_values[i]);
            }
            if (current_values != NULL)
            {
                void *memory = current_values;
                vigil_runtime_free(vm->runtime, &memory);
            }
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "vm frame is missing");
            return VIGIL_STATUS_INTERNAL;
        }

        if (frame->pending_return_count == 0U)
        {
            status = vigil_vm_grow_value_array(vm->runtime, &frame->pending_returns, &frame->pending_return_capacity,
                                               current_count, error);
            if (status != VIGIL_STATUS_OK)
            {
                for (i = 0U; i < current_count; i += 1U)
                {
                    vigil_value_release(&current_values[i]);
                }
                if (current_values != NULL)
                {
                    void *memory = current_values;
                    vigil_runtime_free(vm->runtime, &memory);
                }
                return status;
            }
            for (i = 0U; i < current_count; i += 1U)
            {
                frame->pending_returns[i] = current_values[i];
                VIGIL_VM_VALUE_INIT_NIL(&current_values[i]);
            }
            frame->pending_return_count = current_count;
            frame->draining_defers = 1;
        }
        else
        {
            for (i = 0U; i < current_count; i += 1U)
            {
                vigil_value_release(&current_values[i]);
            }
        }
        if (current_values != NULL)
        {
            void *memory = current_values;
            vigil_runtime_free(vm->runtime, &memory);
        }
        current_values = NULL;
        current_count = 0U;

        while (frame->defer_count > 0U)
        {
            status = vigil_vm_execute_next_defer(vm, frame, &pushed_frame, error);
            if (status != VIGIL_STATUS_OK)
            {
                return status;
            }
            if (pushed_frame)
            {
                return VIGIL_STATUS_OK;
            }
        }

        current_values = frame->pending_returns;
        current_count = frame->pending_return_count;
        frame->pending_returns = NULL;
        frame->pending_return_count = 0U;
        frame->pending_return_capacity = 0U;
        frame->draining_defers = 0;
        base_slot = frame->base_slot;
        vm->frame_count -= 1U;
        vigil_vm_frame_clear(vm->runtime, &vm->frames[vm->frame_count]);
        vigil_vm_unwind_stack_to(vm, base_slot);
        if (vm->frame_count == 0U)
        {
            if (current_count == 0U)
            {
                VIGIL_VM_VALUE_INIT_NIL(out_value);
            }
            else
            {
                *out_value = current_values[0];
                VIGIL_VM_VALUE_INIT_NIL(&current_values[0]);
                for (i = 1U; i < current_count; i += 1U)
                {
                    vigil_value_release(&current_values[i]);
                }
            }
            if (current_values != NULL)
            {
                void *memory = current_values;
                vigil_runtime_free(vm->runtime, &memory);
            }
            return VIGIL_STATUS_OK;
        }
        frame = vigil_vm_current_frame(vm);
        if (frame != NULL && frame->draining_defers)
        {
            continue;
        }

        for (i = 0U; i < current_count; i += 1U)
        {
            status = vigil_vm_push(vm, &current_values[i], error);
            if (status != VIGIL_STATUS_OK)
            {
                for (; i < current_count; i += 1U)
                {
                    vigil_value_release(&current_values[i]);
                }
                if (current_values != NULL)
                {
                    void *memory = current_values;
                    vigil_runtime_free(vm->runtime, &memory);
                }
                return status;
            }
            vigil_value_release(&current_values[i]);
        }
        if (current_values != NULL)
        {
            void *memory = current_values;
            vigil_runtime_free(vm->runtime, &memory);
        }
        return VIGIL_STATUS_OK;
    }
}

static vigil_status_t vigil_vm_checked_add(int64_t left, int64_t right, int64_t *out_result)
{
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left + right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_uadd(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (left > UINT64_MAX - right)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left + right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_subtract(int64_t left, int64_t right, int64_t *out_result)
{
    if ((right > 0 && left < INT64_MIN + right) || (right < 0 && left > INT64_MAX + right))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left - right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_usubtract(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (left < right)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left - right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_multiply(int64_t left, int64_t right, int64_t *out_result)
{
    if (left == 0 || right == 0)
    {
        *out_result = 0;
        return VIGIL_STATUS_OK;
    }

    if ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (left > 0)
    {
        if (right > 0)
        {
            if (left > INT64_MAX / right)
            {
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
        }
        else if (right < INT64_MIN / left)
        {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    }
    else if (right > 0)
    {
        if (left < INT64_MIN / right)
        {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    }
    else if (left != 0 && right < INT64_MAX / left)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left * right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_umultiply(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (left == 0U || right == 0U)
    {
        *out_result = 0U;
        return VIGIL_STATUS_OK;
    }
    if (left > UINT64_MAX / right)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left * right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_divide(int64_t left, int64_t right, int64_t *out_result)
{
    if (right == 0)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (left == INT64_MIN && right == -1)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left / right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_udivide(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (right == 0U)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left / right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_modulo(int64_t left, int64_t right, int64_t *out_result)
{
    if (right == 0)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (left == INT64_MIN && right == -1)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left % right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_umodulo(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (right == 0U)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left % right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_negate(int64_t value, int64_t *out_result)
{
    if (value == INT64_MIN)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = -value;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_shift_left(int64_t left, int64_t right, int64_t *out_result)
{
    if (right < 0 || right >= 64)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = (int64_t)(((uint64_t)left) << (uint32_t)right);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_shift_right(int64_t left, int64_t right, int64_t *out_result)
{
    uint64_t shifted;

    if (right < 0 || right >= 64)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (right == 0)
    {
        *out_result = left;
        return VIGIL_STATUS_OK;
    }

    shifted = ((uint64_t)left) >> (uint32_t)right;
    if (left < 0)
    {
        shifted |= UINT64_MAX << (64U - (uint32_t)right);
    }

    *out_result = (int64_t)shifted;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_ushift_left(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (right >= 64U)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left << (uint32_t)right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_checked_ushift_right(uint64_t left, uint64_t right, uint64_t *out_result)
{
    if (right >= 64U)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left >> (uint32_t)right;
    return VIGIL_STATUS_OK;
}

static int vigil_vm_value_is_integer(const vigil_value_t *value)
{
    return value != NULL && (vigil_nanbox_is_int(*value) || vigil_nanbox_is_uint(*value));
}

static int vigil_vm_values_equal(const vigil_value_t *left, const vigil_value_t *right)
{
    const vigil_object_t *left_object;
    const vigil_object_t *right_object;

    if (left == NULL || right == NULL)
    {
        return 0;
    }

    if (vigil_value_kind(left) != vigil_value_kind(right))
    {
        return 0;
    }

    switch (vigil_value_kind(left))
    {
    case VIGIL_VALUE_NIL:
        return 1;
    case VIGIL_VALUE_BOOL:
        return vigil_nanbox_decode_bool(*left) == vigil_nanbox_decode_bool(*right);
    case VIGIL_VALUE_INT:
        return vigil_value_as_int(left) == vigil_value_as_int(right);
    case VIGIL_VALUE_UINT:
        return vigil_value_as_uint(left) == vigil_value_as_uint(right);
    case VIGIL_VALUE_FLOAT:
        return vigil_nanbox_decode_double(*left) == vigil_nanbox_decode_double(*right);
    case VIGIL_VALUE_OBJECT:
        left_object = ((vigil_object_t *)vigil_nanbox_decode_ptr(*left));
        right_object = ((vigil_object_t *)vigil_nanbox_decode_ptr(*right));
        if (left_object == right_object)
        {
            return 1;
        }
        if (left_object == NULL || right_object == NULL)
        {
            return 0;
        }
        if (vigil_object_type(left_object) == VIGIL_OBJECT_STRING &&
            vigil_object_type(right_object) == VIGIL_OBJECT_STRING)
        {
            size_t left_length = vigil_string_object_length(left_object);
            size_t right_length = vigil_string_object_length(right_object);
            const char *left_text = vigil_string_object_c_str(left_object);
            const char *right_text = vigil_string_object_c_str(right_object);

            return left_length == right_length && left_text != NULL && right_text != NULL &&
                   memcmp(left_text, right_text, left_length) == 0;
        }
        if (vigil_object_type(left_object) == VIGIL_OBJECT_ERROR &&
            vigil_object_type(right_object) == VIGIL_OBJECT_ERROR)
        {
            size_t left_length = vigil_error_object_message_length(left_object);
            size_t right_length = vigil_error_object_message_length(right_object);
            const char *left_text = vigil_error_object_message(left_object);
            const char *right_text = vigil_error_object_message(right_object);

            return vigil_error_object_kind(left_object) == vigil_error_object_kind(right_object) &&
                   left_length == right_length && left_text != NULL && right_text != NULL &&
                   memcmp(left_text, right_text, left_length) == 0;
        }
        return 0;
    default:
        return 0;
    }
}

static int vigil_vm_value_is_supported_map_key(const vigil_value_t *value)
{
    const vigil_object_t *object;

    if (value == NULL)
    {
        return 0;
    }

    switch (vigil_value_kind(value))
    {
    case VIGIL_VALUE_BOOL:
    case VIGIL_VALUE_INT:
    case VIGIL_VALUE_UINT:
        return 1;
    case VIGIL_VALUE_OBJECT:
        object = ((vigil_object_t *)vigil_nanbox_decode_ptr(*value));
        return object != NULL && vigil_object_type(object) == VIGIL_OBJECT_STRING;
    default:
        return 0;
    }
}

static vigil_status_t vigil_vm_concat_strings(vigil_vm_t *vm, const vigil_value_t *left, const vigil_value_t *right,
                                              vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_string_t text;
    const vigil_object_t *left_object;
    const vigil_object_t *right_object;
    vigil_object_t *object;

    if (vm == NULL || left == NULL || right == NULL || out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string operands are required");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    left_object = (vigil_object_t *)vigil_nanbox_decode_ptr(*left);
    right_object = (vigil_object_t *)vigil_nanbox_decode_ptr(*right);
    if (left_object == NULL || right_object == NULL || vigil_object_type(left_object) != VIGIL_OBJECT_STRING ||
        vigil_object_type(right_object) != VIGIL_OBJECT_STRING)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string operands are required");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    vigil_string_init(&text, vm->runtime);
    status = vigil_string_append(&text, vigil_string_object_c_str(left_object), vigil_string_object_length(left_object),
                                 error);
    if (status == VIGIL_STATUS_OK)
    {
        status = vigil_string_append(&text, vigil_string_object_c_str(right_object),
                                     vigil_string_object_length(right_object), error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        status =
            vigil_string_object_new(vm->runtime, vigil_string_c_str(&text), vigil_string_length(&text), &object, error);
    }
    if (status == VIGIL_STATUS_OK)
    {
        vigil_value_init_object(out_value, &object);
    }
    vigil_object_release(&object);
    vigil_string_free(&text);
    return status;
}

static vigil_status_t vigil_vm_stringify_value(vigil_vm_t *vm, const vigil_value_t *value, vigil_value_t *out_value,
                                               vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object;
    const char *text;
    char buffer[128];
    int written;

    if (vm == NULL || value == NULL || out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "vm string conversion arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    switch (vigil_value_kind(value))
    {
    case VIGIL_VALUE_BOOL:
        text = vigil_nanbox_decode_bool(*value) ? "true" : "false";
        status = vigil_string_object_new_cstr(vm->runtime, text, &object, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        break;
    case VIGIL_VALUE_INT:
        written = snprintf(buffer, sizeof(buffer), "%lld", (long long)vigil_value_as_int(value));
        if (written < 0 || (size_t)written >= sizeof(buffer))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format integer string conversion");
            return VIGIL_STATUS_INTERNAL;
        }
        status = vigil_string_object_new(vm->runtime, buffer, (size_t)written, &object, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        break;
    case VIGIL_VALUE_UINT:
        written = snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)vigil_value_as_uint(value));
        if (written < 0 || (size_t)written >= sizeof(buffer))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format integer string conversion");
            return VIGIL_STATUS_INTERNAL;
        }
        status = vigil_string_object_new(vm->runtime, buffer, (size_t)written, &object, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        break;
    case VIGIL_VALUE_FLOAT:
        written = snprintf(buffer, sizeof(buffer), "%.17g", vigil_nanbox_decode_double(*value));
        if (written < 0 || (size_t)written >= sizeof(buffer))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to format float string conversion");
            return VIGIL_STATUS_INTERNAL;
        }
        status = vigil_string_object_new(vm->runtime, buffer, (size_t)written, &object, error);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        break;
    case VIGIL_VALUE_OBJECT:
        if (((vigil_object_t *)vigil_nanbox_decode_ptr(*value)) == NULL ||
            vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(*value))) != VIGIL_OBJECT_STRING)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                    "string conversion requires a primitive or string operand");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        *out_value = vigil_value_copy(value);
        return VIGIL_STATUS_OK;
    default:
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                "string conversion requires a primitive or string operand");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

/* ── FORMAT_SPEC helpers ─────────────────────────────────────────── */

/* Encoding for word1:
   bits  0-7:  fill character (ASCII, 0 = space default)
   bits  8-9:  alignment  0=default 1=left 2=right 3=center
   bits 10-13: format type 0=str 1=dec 2=hex 3=HEX 4=bin 5=oct 6=float_f
   bit  14:    grouping (thousands separator)
*/
#define FSPEC_FILL(w) ((char)((w) & 0xFFU))
#define FSPEC_ALIGN(w) (((w) >> 8U) & 0x3U)
#define FSPEC_TYPE(w) (((w) >> 10U) & 0xFU)
#define FSPEC_GROUP(w) (((w) >> 14U) & 0x1U)
#define FSPEC_WIDTH(w) ((w) & 0xFFFFU)
#define FSPEC_PREC(w) (((w) >> 16U) & 0xFFFFU)

static vigil_status_t vigil_vm_format_spec_value(vigil_vm_t *vm, const vigil_value_t *val, uint32_t word1,
                                                 uint32_t word2, vigil_value_t *out_value, vigil_error_t *error)
{
    char fill;
    unsigned int align;
    unsigned int fmt_type;
    unsigned int grouping;
    unsigned int width;
    unsigned int precision;
    char buf[256];
    int len;
    vigil_status_t status;
    vigil_object_t *object;
    void *memory;

    fill = FSPEC_FILL(word1);
    if (fill == 0)
        fill = ' ';
    align = FSPEC_ALIGN(word1);
    fmt_type = FSPEC_TYPE(word1);
    grouping = FSPEC_GROUP(word1);
    width = FSPEC_WIDTH(word2);
    precision = FSPEC_PREC(word2);

    /* Step 1: format the value into buf[] based on fmt_type. */
    if (fmt_type == 6U)
    {
        /* float_f */
        if (!vigil_nanbox_is_double(*val))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "float format specifier requires f64 value");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        char fmt[32];
        snprintf(fmt, sizeof(fmt), "%%.%uf", precision);
        len = snprintf(buf, sizeof(buf), fmt, vigil_nanbox_decode_double(*val));
    }
    else if (fmt_type >= 1U && fmt_type <= 5U)
    {
        /* integer formats */
        int64_t ival;
        if (vigil_nanbox_is_int(*val))
        {
            ival = vigil_nanbox_decode_int(*val);
        }
        else
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                    "integer format specifier requires an integer value");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        if (fmt_type == 1U)
        {
            /* decimal */
            len = snprintf(buf, sizeof(buf), "%lld", (long long)ival);
        }
        else if (fmt_type == 2U)
        {
            /* hex lower */
            if (ival < 0)
            {
                len = snprintf(buf, sizeof(buf), "-%llx", (unsigned long long)(-ival));
            }
            else
            {
                len = snprintf(buf, sizeof(buf), "%llx", (unsigned long long)ival);
            }
        }
        else if (fmt_type == 3U)
        {
            /* hex upper */
            if (ival < 0)
            {
                len = snprintf(buf, sizeof(buf), "-%llX", (unsigned long long)(-ival));
            }
            else
            {
                len = snprintf(buf, sizeof(buf), "%llX", (unsigned long long)ival);
            }
        }
        else if (fmt_type == 4U)
        {
            /* binary */
            uint64_t uval = (uint64_t)ival;
            int pos = 0;
            if (ival < 0)
            {
                buf[pos++] = '-';
                uval = (uint64_t)(-ival);
            }
            if (uval == 0U)
            {
                buf[pos++] = '0';
            }
            else
            {
                char tmp[65];
                int ti = 0;
                while (uval > 0U)
                {
                    tmp[ti++] = '0' + (char)(uval & 1U);
                    uval >>= 1U;
                }
                while (ti > 0)
                {
                    buf[pos++] = tmp[--ti];
                }
            }
            len = pos;
            buf[len] = '\0';
        }
        else
        {
            /* octal */
            if (ival < 0)
            {
                len = snprintf(buf, sizeof(buf), "-%llo", (unsigned long long)(-ival));
            }
            else
            {
                len = snprintf(buf, sizeof(buf), "%llo", (unsigned long long)ival);
            }
        }
        /* Apply thousands grouping to decimal format. */
        if (grouping && fmt_type == 1U && len > 0 && len < 200)
        {
            char tmp[256];
            int src = 0;
            int dst = 0;
            int start;
            if (buf[0] == '-')
            {
                tmp[dst++] = '-';
                src = 1;
            }
            start = src;
            while (src < len)
            {
                int remaining = len - src;
                if (remaining > 0 && remaining % 3 == 0 && src > start)
                {
                    tmp[dst++] = ',';
                }
                tmp[dst++] = buf[src++];
            }
            memcpy(buf, tmp, (size_t)dst);
            len = dst;
            buf[len] = '\0';
        }
    }
    else
    {
        /* string (type 0) — stringify the value */
        const char *text = NULL;
        size_t text_len = 0U;
        if (vigil_nanbox_is_object(*val))
        {
            const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(*val);
            if (obj != NULL && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
            {
                text = vigil_string_object_c_str(obj);
                text_len = vigil_string_object_length(obj);
            }
        }
        if (text == NULL)
        {
            if (vigil_nanbox_is_int(*val))
            {
                len = snprintf(buf, sizeof(buf), "%lld", (long long)vigil_nanbox_decode_int(*val));
                text = buf;
                text_len = (size_t)len;
            }
            else if (vigil_nanbox_is_double(*val))
            {
                len = snprintf(buf, sizeof(buf), "%g", vigil_nanbox_decode_double(*val));
                text = buf;
                text_len = (size_t)len;
            }
            else if (vigil_nanbox_is_bool(*val))
            {
                text = vigil_nanbox_decode_bool(*val) ? "true" : "false";
                text_len = vigil_nanbox_decode_bool(*val) ? 4U : 5U;
            }
            else
            {
                text = "";
                text_len = 0U;
            }
        }
        /* Apply width/alignment directly to the string. */
        if (width > 0U && text_len < width)
        {
            size_t pad = width - text_len;
            size_t total = width;
            status = vigil_runtime_alloc(vm->runtime, total + 1U, &memory, error);
            if (status != VIGIL_STATUS_OK)
                return status;
            char *out = (char *)memory;
            size_t lpad = 0U;
            size_t rpad = 0U;
            if (align == 1U)
            {
                rpad = pad;
            }
            else if (align == 3U)
            {
                lpad = pad / 2U;
                rpad = pad - lpad;
            }
            else
            {
                lpad = pad;
            } /* default/right */
            memset(out, fill, lpad);
            memcpy(out + lpad, text, text_len);
            memset(out + lpad + text_len, fill, rpad);
            object = NULL;
            status = vigil_string_object_new(vm->runtime, out, total, &object, error);
            vigil_runtime_free(vm->runtime, &memory);
            if (status != VIGIL_STATUS_OK)
                return status;
            vigil_value_init_object(out_value, &object);
            vigil_object_release(&object);
            return VIGIL_STATUS_OK;
        }
        /* No padding needed — just create string object. */
        object = NULL;
        status = vigil_string_object_new(vm->runtime, text, text_len, &object, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_value_init_object(out_value, &object);
        vigil_object_release(&object);
        return VIGIL_STATUS_OK;
    }

    if (len < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "format specifier produced invalid output");
        return VIGIL_STATUS_INTERNAL;
    }

    /* Step 2: apply width/alignment padding. */
    if (width > 0U && (size_t)len < width)
    {
        size_t pad = width - (size_t)len;
        size_t total = width;
        status = vigil_runtime_alloc(vm->runtime, total + 1U, &memory, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        char *out = (char *)memory;
        size_t lpad = 0U;
        size_t rpad = 0U;
        if (align == 1U)
        {
            rpad = pad;
        }
        else if (align == 3U)
        {
            lpad = pad / 2U;
            rpad = pad - lpad;
        }
        else
        {
            lpad = pad;
        } /* default/right */
        memset(out, fill, lpad);
        memcpy(out + lpad, buf, (size_t)len);
        memset(out + lpad + (size_t)len, fill, rpad);
        object = NULL;
        status = vigil_string_object_new(vm->runtime, out, total, &object, error);
        vigil_runtime_free(vm->runtime, &memory);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_value_init_object(out_value, &object);
        vigil_object_release(&object);
        return VIGIL_STATUS_OK;
    }

    object = NULL;
    status = vigil_string_object_new(vm->runtime, buf, (size_t)len, &object, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_format_f64_value(vigil_vm_t *vm, const vigil_value_t *value, uint32_t precision,
                                                vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_status_t status;
    char format[32];
    int written;
    int length;
    void *memory;
    char *buffer;
    vigil_object_t *object;

    if (vm == NULL || value == NULL || out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "f64 format arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (!vigil_nanbox_is_double(*value))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "f64 formatting requires an f64 operand");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    written = snprintf(format, sizeof(format), "%%.%uf", (unsigned int)precision);
    if (written < 0 || (size_t)written >= sizeof(format))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to build float format specifier");
        return VIGIL_STATUS_INTERNAL;
    }

    length = snprintf(NULL, 0, format, vigil_nanbox_decode_double(*value));
    if (length < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to measure formatted float output");
        return VIGIL_STATUS_INTERNAL;
    }

    object = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(vm->runtime, (size_t)length + 1U, &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    buffer = (char *)memory;
    written = snprintf(buffer, (size_t)length + 1U, format, vigil_nanbox_decode_double(*value));
    if (written != length)
    {
        vigil_runtime_free(vm->runtime, &memory);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "failed to write formatted float output");
        return VIGIL_STATUS_INTERNAL;
    }

    status = vigil_string_object_new(vm->runtime, buffer, (size_t)length, &object, error);
    vigil_runtime_free(vm->runtime, &memory);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

static int vigil_vm_get_string_parts(const vigil_value_t *value, const char **out_text, size_t *out_length)
{
    const vigil_object_t *object;

    if (out_text != NULL)
    {
        *out_text = NULL;
    }
    if (out_length != NULL)
    {
        *out_length = 0U;
    }
    if (value == NULL || !vigil_nanbox_is_object(*value))
    {
        return 0;
    }
    object = ((vigil_object_t *)vigil_nanbox_decode_ptr(*value));
    if (object == NULL || vigil_object_type(object) != VIGIL_OBJECT_STRING)
    {
        return 0;
    }
    if (out_text != NULL)
    {
        *out_text = vigil_string_object_c_str(object);
    }
    if (out_length != NULL)
    {
        *out_length = vigil_string_object_length(object);
    }
    return 1;
}

static vigil_status_t vigil_vm_new_string_value(vigil_vm_t *vm, const char *text, size_t length,
                                                vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object;

    if (vm == NULL || out_value == NULL || (length != 0U && text == NULL))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "string creation arguments are invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    status = vigil_string_object_new(vm->runtime, text == NULL ? "" : text, length, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_make_error_value(vigil_vm_t *vm, int64_t kind, const char *message,
                                                size_t message_length, vigil_value_t *out_value, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_object_t *object;

    object = NULL;
    status = vigil_error_object_new(vm->runtime, message, message_length, kind, &object, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_value_init_object(out_value, &object);
    vigil_object_release(&object);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_make_ok_error_value(vigil_vm_t *vm, vigil_value_t *out_value, vigil_error_t *error)
{
    return vigil_vm_make_error_value(vm, 0, "", 0U, out_value, error);
}

static vigil_status_t vigil_vm_make_bounds_error_value(vigil_vm_t *vm, const char *message, vigil_value_t *out_value,
                                                       vigil_error_t *error)
{
    size_t length;

    length = message == NULL ? 0U : strlen(message);
    return vigil_vm_make_error_value(vm, 7, message == NULL ? "" : message, length, out_value, error);
}

static int vigil_vm_find_substring(const char *text, size_t text_length, const char *needle, size_t needle_length,
                                   size_t *out_index)
{
    size_t index;

    if (out_index != NULL)
    {
        *out_index = 0U;
    }
    if (text == NULL || needle == NULL)
    {
        return 0;
    }
    if (needle_length == 0U)
    {
        return 1;
    }
    if (needle_length > text_length)
    {
        return 0;
    }

    for (index = 0U; index + needle_length <= text_length; index += 1U)
    {
        if (memcmp(text + index, needle, needle_length) == 0)
        {
            if (out_index != NULL)
            {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

static vigil_status_t vigil_vm_push_checked_signed_integer(vigil_vm_t *vm, int64_t integer_value, int64_t minimum_value,
                                                           int64_t maximum_value, const char *error_message,
                                                           vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value;

    if (integer_value < minimum_value || integer_value > maximum_value)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, error_message);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    do
    {
        vigil_value_init_int(&(value), integer_value);
    } while (0);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

static vigil_status_t vigil_vm_push_checked_unsigned_integer(vigil_vm_t *vm, uint64_t integer_value,
                                                             uint64_t maximum_value, const char *error_message,
                                                             vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value;

    if (integer_value > maximum_value)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, error_message);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    do
    {
        vigil_value_init_uint(&(value), integer_value);
    } while (0);
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    return status;
}

static vigil_status_t vigil_vm_convert_to_signed_integer_type(vigil_vm_t *vm, const vigil_value_t *value,
                                                              int64_t minimum_value, int64_t maximum_value,
                                                              const char *operand_error, const char *range_error,
                                                              vigil_error_t *error)
{
    int64_t integer_value;
    double float_value;

    if (vm == NULL || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "integer conversion arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_nanbox_is_int(*value))
    {
        return vigil_vm_push_checked_signed_integer(vm, vigil_value_as_int(value), minimum_value, maximum_value,
                                                    range_error, error);
    }
    if (vigil_nanbox_is_uint(*value))
    {
        if (vigil_value_as_uint(value) > (uint64_t)maximum_value)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, range_error);
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        return vigil_vm_push_checked_signed_integer(vm, (int64_t)vigil_value_as_uint(value), minimum_value,
                                                    maximum_value, range_error, error);
    }
    if (!vigil_nanbox_is_double(*value))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, operand_error);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    float_value = vigil_nanbox_decode_double(*value);
    if (!isfinite(float_value) || float_value > (double)INT64_MAX || float_value < (double)INT64_MIN)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, range_error);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    integer_value = (int64_t)float_value;
    return vigil_vm_push_checked_signed_integer(vm, integer_value, minimum_value, maximum_value, range_error, error);
}

static vigil_status_t vigil_vm_convert_to_unsigned_integer_type(vigil_vm_t *vm, const vigil_value_t *value,
                                                                uint64_t maximum_value, const char *operand_error,
                                                                const char *range_error, vigil_error_t *error)
{
    uint64_t integer_value;
    double float_value;

    if (vm == NULL || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "integer conversion arguments must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_nanbox_is_uint(*value))
    {
        return vigil_vm_push_checked_unsigned_integer(vm, vigil_value_as_uint(value), maximum_value, range_error,
                                                      error);
    }
    if (vigil_nanbox_is_int(*value))
    {
        if (vigil_value_as_int(value) < 0)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, range_error);
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        return vigil_vm_push_checked_unsigned_integer(vm, (uint64_t)vigil_value_as_int(value), maximum_value,
                                                      range_error, error);
    }
    if (!vigil_nanbox_is_double(*value))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, operand_error);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    float_value = vigil_nanbox_decode_double(*value);
    if (!isfinite(float_value) || float_value < 0.0 || float_value > (double)UINT64_MAX)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, range_error);
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    integer_value = (uint64_t)float_value;
    return vigil_vm_push_checked_unsigned_integer(vm, integer_value, maximum_value, range_error, error);
}

static vigil_status_t vigil_vm_fail_at_ip(vigil_vm_t *vm, vigil_status_t status, const char *message,
                                          vigil_error_t *error)
{
    vigil_source_span_t span;
    vigil_vm_frame_t *frame;

    vigil_error_set_literal(error, status, message);
    frame = vigil_vm_current_frame(vm);
    if (error != NULL && frame != NULL)
    {
        span = vigil_chunk_span_at(frame->chunk, frame->ip);
        error->location.source_id = span.source_id;
        error->location.offset = span.start_offset;
    }

    return status;
}

static vigil_status_t vigil_vm_read_u32(vigil_vm_t *vm, uint32_t *out_value, vigil_error_t *error)
{
    vigil_vm_frame_t *frame;
    const uint8_t *code;
    size_t code_size;
    size_t ip;

    frame = vigil_vm_current_frame(vm);
    if (frame == NULL)
    {
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "vm frame is missing", error);
    }

    code = VIGIL_VM_CHUNK_CODE(frame->chunk);
    code_size = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
    ip = frame->ip;
    if (code == NULL || ip + 4U >= code_size)
    {
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "truncated operand in chunk", error);
    }

    *out_value = (uint32_t)code[ip + 1U];
    *out_value |= (uint32_t)code[ip + 2U] << 8U;
    *out_value |= (uint32_t)code[ip + 3U] << 16U;
    *out_value |= (uint32_t)code[ip + 4U] << 24U;
    frame->ip += 5U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_vm_read_raw_u32(vigil_vm_t *vm, uint32_t *out_value, vigil_error_t *error)
{
    vigil_vm_frame_t *frame;
    const uint8_t *code;
    size_t code_size;
    size_t ip;

    frame = vigil_vm_current_frame(vm);
    if (frame == NULL)
    {
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "vm frame is missing", error);
    }

    code = VIGIL_VM_CHUNK_CODE(frame->chunk);
    code_size = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
    ip = frame->ip;
    if (code == NULL || ip + 3U >= code_size)
    {
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "truncated operand in chunk", error);
    }

    *out_value = (uint32_t)code[ip];
    *out_value |= (uint32_t)code[ip + 1U] << 8U;
    *out_value |= (uint32_t)code[ip + 2U] << 16U;
    *out_value |= (uint32_t)code[ip + 3U] << 24U;
    frame->ip += 4U;
    return VIGIL_STATUS_OK;
}

void vigil_vm_options_init(vigil_vm_options_t *options)
{
    if (options == NULL)
    {
        return;
    }

    memset(options, 0, sizeof(*options));
}

vigil_status_t vigil_vm_open(vigil_vm_t **out_vm, vigil_runtime_t *runtime, const vigil_vm_options_t *options,
                             vigil_error_t *error)
{
    vigil_vm_t *vm;
    void *memory;
    vigil_status_t status;
    size_t initial_stack_capacity;

    vigil_error_clear(error);
    if (out_vm == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_vm must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_vm = NULL;
    memory = NULL;
    status = vigil_runtime_alloc(runtime, sizeof(*vm), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vm = (vigil_vm_t *)memory;
    vm->runtime = runtime;
    initial_stack_capacity = options == NULL ? 0U : options->initial_stack_capacity;
    if (initial_stack_capacity < VIGIL_VM_INITIAL_STACK_CAPACITY)
    {
        initial_stack_capacity = VIGIL_VM_INITIAL_STACK_CAPACITY;
    }
    status = vigil_vm_grow_stack(vm, initial_stack_capacity, error);
    if (status != VIGIL_STATUS_OK)
    {
        memory = vm;
        vigil_runtime_free(runtime, &memory);
        return status;
    }
    status = vigil_vm_grow_frames(vm, VIGIL_VM_INITIAL_FRAME_CAPACITY, error);
    if (status != VIGIL_STATUS_OK)
    {
        memory = vm->stack;
        vigil_runtime_free(runtime, &memory);
        memory = vm;
        vigil_runtime_free(runtime, &memory);
        return status;
    }

    *out_vm = vm;
    return VIGIL_STATUS_OK;
}

void vigil_vm_close(vigil_vm_t **vm)
{
    vigil_vm_t *resolved_vm;
    vigil_runtime_t *runtime;
    void *memory;

    if (vm == NULL || *vm == NULL)
    {
        return;
    }

    resolved_vm = *vm;
    *vm = NULL;
    runtime = resolved_vm->runtime;
    vigil_vm_release_stack(resolved_vm);
    vigil_vm_clear_frames(resolved_vm);
    memory = resolved_vm->stack;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }

    memory = resolved_vm->frames;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }

    memory = resolved_vm;
    if (runtime != NULL)
    {
        vigil_runtime_free(runtime, &memory);
    }
}

vigil_runtime_t *vigil_vm_runtime(const vigil_vm_t *vm)
{
    if (vm == NULL)
    {
        return NULL;
    }

    return vm->runtime;
}

size_t vigil_vm_stack_depth(const vigil_vm_t *vm)
{
    if (vm == NULL)
    {
        return 0U;
    }

    return vm->stack_count;
}

size_t vigil_vm_frame_depth(const vigil_vm_t *vm)
{
    if (vm == NULL)
    {
        return 0U;
    }

    return vm->frame_count;
}

vigil_value_t vigil_vm_stack_get(const vigil_vm_t *vm, size_t index)
{
    if (vm == NULL || index >= vm->stack_count)
    {
        vigil_value_t nil;
        vigil_value_init_nil(&nil);
        return nil;
    }
    return vm->stack[index];
}

vigil_status_t vigil_vm_stack_push(vigil_vm_t *vm, const vigil_value_t *value, vigil_error_t *error)
{
    if (vm == NULL || value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "vm and value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    return vigil_vm_push(vm, value, error);
}

void vigil_vm_stack_pop_n(vigil_vm_t *vm, size_t count)
{
    size_t i;

    if (vm == NULL || count == 0U)
    {
        return;
    }
    if (count > vm->stack_count)
    {
        count = vm->stack_count;
    }
    for (i = 0U; i < count; i++)
    {
        vm->stack_count -= 1U;
        VIGIL_VM_VALUE_RELEASE(&vm->stack[vm->stack_count]);
    }
}

void vigil_vm_set_debug_hook(vigil_vm_t *vm, int (*hook)(vigil_vm_t *vm, void *userdata), void *userdata)
{
    if (vm == NULL)
        return;
    vm->debug_hook = hook;
    vm->debug_hook_userdata = userdata;
}

void vigil_vm_set_args(vigil_vm_t *vm, const char *const *argv, size_t argc)
{
    if (vm == NULL)
        return;
    vm->argv = argv;
    vm->argc = argc;
}

void vigil_vm_get_args(const vigil_vm_t *vm, const char *const **out_argv, size_t *out_argc)
{
    if (out_argv != NULL)
        *out_argv = vm != NULL ? vm->argv : NULL;
    if (out_argc != NULL)
        *out_argc = vm != NULL ? vm->argc : 0;
}

const vigil_chunk_t *vigil_vm_frame_chunk(const vigil_vm_t *vm, size_t frame_index)
{
    if (vm == NULL || frame_index >= vm->frame_count)
        return NULL;
    return vm->frames[frame_index].chunk;
}

size_t vigil_vm_frame_ip(const vigil_vm_t *vm, size_t frame_index)
{
    if (vm == NULL || frame_index >= vm->frame_count)
        return 0U;
    return vm->frames[frame_index].ip;
}

size_t vigil_vm_frame_base_slot(const vigil_vm_t *vm, size_t frame_index)
{
    if (vm == NULL || frame_index >= vm->frame_count)
        return 0U;
    return vm->frames[frame_index].base_slot;
}

const vigil_object_t *vigil_vm_frame_function(const vigil_vm_t *vm, size_t frame_index)
{
    if (vm == NULL || frame_index >= vm->frame_count)
        return NULL;
    return vm->frames[frame_index].function;
}

vigil_status_t vigil_vm_execute(vigil_vm_t *vm, const vigil_chunk_t *chunk, vigil_value_t *out_value,
                                vigil_error_t *error)
{
    vigil_status_t status;

    status = vigil_vm_validate(vm, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (chunk == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "chunk must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_vm_release_stack(vm);
    vigil_vm_clear_frames(vm);
    status = vigil_vm_push_frame(vm, NULL, NULL, chunk, 0U, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    return vigil_vm_execute_function(vm, NULL, out_value, error);
}

/* ── CALL_EXTERN runtime handler ─────────────────────────────────── */

/*
 * vigil_extern_call is implemented in ffi.c when the ffi stdlib module
 * is enabled. Reduced builds keep a stub here so the core VM does not
 * depend on that optional module at link time.
 */
#ifdef VIGIL_HAS_STDLIB_FFI
extern vigil_status_t vigil_extern_call(vigil_vm_t *vm, const char *desc, size_t desc_len, size_t arg_count,
                                        vigil_error_t *error);
#else
static vigil_status_t vigil_extern_call(vigil_vm_t *vm, const char *desc, size_t desc_len, size_t arg_count,
                                        vigil_error_t *error)
{
    (void)vm;
    (void)desc;
    (void)desc_len;
    (void)arg_count;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED,
                            "extern calls require a build with the ffi stdlib module enabled");
    return VIGIL_STATUS_UNSUPPORTED;
}
#endif

static vigil_status_t vigil_vm_call_extern(vigil_vm_t *vm, const char *desc, size_t desc_len, size_t arg_count,
                                           vigil_error_t *error)
{
    return vigil_extern_call(vm, desc, desc_len, arg_count, error);
}

static void vigil_vm_math_sin(vigil_vm_t *vm)
{
    vigil_value_t r;
    vigil_value_init_float(&r, sin(vigil_nanbox_decode_double(vigil_vm_pop_or_nil(vm))));
    VIGIL_VM_VALUE_COPY(&vm->stack[vm->stack_count], &r);
    vm->stack_count += 1U;
}
static void vigil_vm_math_cos(vigil_vm_t *vm)
{
    vigil_value_t r;
    vigil_value_init_float(&r, cos(vigil_nanbox_decode_double(vigil_vm_pop_or_nil(vm))));
    VIGIL_VM_VALUE_COPY(&vm->stack[vm->stack_count], &r);
    vm->stack_count += 1U;
}
static void vigil_vm_math_sqrt(vigil_vm_t *vm)
{
    vigil_value_t r;
    vigil_value_init_float(&r, sqrt(vigil_nanbox_decode_double(vigil_vm_pop_or_nil(vm))));
    VIGIL_VM_VALUE_COPY(&vm->stack[vm->stack_count], &r);
    vm->stack_count += 1U;
}
static void vigil_vm_math_log(vigil_vm_t *vm)
{
    vigil_value_t r;
    vigil_value_init_float(&r, log(vigil_nanbox_decode_double(vigil_vm_pop_or_nil(vm))));
    VIGIL_VM_VALUE_COPY(&vm->stack[vm->stack_count], &r);
    vm->stack_count += 1U;
}
static void vigil_vm_math_pow(vigil_vm_t *vm)
{
    vigil_value_t b, a, r;
    b = vigil_vm_pop_or_nil(vm);
    a = vigil_vm_pop_or_nil(vm);
    vigil_value_init_float(&r, pow(vigil_nanbox_decode_double(a), vigil_nanbox_decode_double(b)));
    VIGIL_VM_VALUE_COPY(&vm->stack[vm->stack_count], &r);
    vm->stack_count += 1U;
}
static void vigil_vm_math_dispatch(vigil_vm_t *vm, vigil_opcode_t op)
{
    switch (op)
    {
    case VIGIL_OPCODE_MATH_SIN_F64:
        vigil_vm_math_sin(vm);
        break;
    case VIGIL_OPCODE_MATH_COS_F64:
        vigil_vm_math_cos(vm);
        break;
    case VIGIL_OPCODE_MATH_SQRT_F64:
        vigil_vm_math_sqrt(vm);
        break;
    case VIGIL_OPCODE_MATH_LOG_F64:
        vigil_vm_math_log(vm);
        break;
    default:
        vigil_vm_math_pow(vm);
        break;
    }
}

/* Dispatch macro for math intrinsic handlers — avoids #if inside the
   dispatch loop (which would add 1 lizard CCN). */
#if VIGIL_VM_COMPUTED_GOTO
#define VIGIL_VM_MATH_NEXT(dt, code, ip) goto *(dt)[(code)[(ip)]]
#else
#define VIGIL_VM_MATH_NEXT(dt, code, ip) VM_BREAK()
#endif

vigil_status_t vigil_vm_execute_function(vigil_vm_t *vm, const vigil_object_t *function, vigil_value_t *out_value,
                                         vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value = {0};
    vigil_value_t left = {0};
    vigil_value_t right = {0};
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;
    const vigil_value_t *constant;
    const vigil_value_t *left_peek;
    const vigil_value_t *peeked;
    uint32_t constant_index;
    uint32_t operand;
    vigil_object_t *object;
    vigil_vm_frame_t *frame;
    const uint8_t *code;
    size_t code_size;
    size_t local_index;
    object = NULL;
    status = vigil_vm_validate(vm, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (out_value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (function != NULL)
    {
        if (vigil_object_type(function) != VIGIL_OBJECT_FUNCTION && vigil_object_type(function) != VIGIL_OBJECT_CLOSURE)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                    "function must be a function or closure object");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        const vigil_object_t *inner_fn = vigil_callable_object_function(function);
        size_t arity = vigil_function_object_arity(inner_fn);
        if (vm->stack_count < arity)
        {
            /* Zero-arity: clear stack and frames as before. */
            if (arity == 0U)
            {
                vigil_vm_release_stack(vm);
                vigil_vm_clear_frames(vm);
                status = vigil_vm_push_frame(vm, function, inner_fn, vigil_callable_object_chunk(function), 0U, error);
            }
            else
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT,
                                        "not enough arguments on stack for function arity");
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
        }
        else
        {
            /* Arguments already on the stack. */
            size_t base = vm->stack_count - arity;
            vigil_vm_clear_frames(vm);
            status = vigil_vm_push_frame(vm, function, inner_fn, vigil_callable_object_chunk(function), base, error);
        }
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    while (1)
    {
        frame = &vm->frames[vm->frame_count - 1U];
        if (frame->chunk == NULL)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "vm frame chunk must not be null");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        code = VIGIL_VM_CHUNK_CODE(frame->chunk);
        code_size = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
        if (frame->ip >= code_size)
        {
            break;
        }
#if VIGIL_VM_COMPUTED_GOTO
        /* Dispatch table — one label per opcode.  Entries for unused
           indices fall through to the default (unknown opcode) path.
           This is a GCC/Clang extension; the ISO C11 switch fallback
           is below.  See docs/stdlib-portability.md. */
        _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wpedantic\"")
        {
            static const void *dispatch_table[256] = {
                [VIGIL_OPCODE_ADD] = &&op_ADD,
                [VIGIL_OPCODE_ARRAY_CONTAINS] = &&op_ARRAY_CONTAINS,
                [VIGIL_OPCODE_ARRAY_GET_SAFE] = &&op_ARRAY_GET_SAFE,
                [VIGIL_OPCODE_ARRAY_POP] = &&op_ARRAY_POP,
                [VIGIL_OPCODE_ARRAY_PUSH] = &&op_ARRAY_PUSH,
                [VIGIL_OPCODE_ARRAY_SET_SAFE] = &&op_ARRAY_SET_SAFE,
                [VIGIL_OPCODE_ARRAY_SLICE] = &&op_ARRAY_SLICE,
                [VIGIL_OPCODE_BITWISE_AND] = &&op_BITWISE_AND,
                [VIGIL_OPCODE_BITWISE_NOT] = &&op_BITWISE_NOT,
                [VIGIL_OPCODE_BITWISE_OR] = &&op_BITWISE_OR,
                [VIGIL_OPCODE_BITWISE_XOR] = &&op_BITWISE_XOR,
                [VIGIL_OPCODE_CALL] = &&op_CALL,
                [VIGIL_OPCODE_CALL_INTERFACE] = &&op_CALL_INTERFACE,
                [VIGIL_OPCODE_CALL_VALUE] = &&op_CALL_VALUE,
                [VIGIL_OPCODE_CONSTANT] = &&op_CONSTANT,
                [VIGIL_OPCODE_DEFER_CALL] = &&op_DEFER_CALL,
                [VIGIL_OPCODE_DEFER_CALL_INTERFACE] = &&op_DEFER_CALL_INTERFACE,
                [VIGIL_OPCODE_DEFER_CALL_VALUE] = &&op_DEFER_CALL_VALUE,
                [VIGIL_OPCODE_DEFER_NEW_INSTANCE] = &&op_DEFER_NEW_INSTANCE,
                [VIGIL_OPCODE_DIVIDE] = &&op_DIVIDE,
                [VIGIL_OPCODE_DUP] = &&op_DUP,
                [VIGIL_OPCODE_DUP_TWO] = &&op_DUP_TWO,
                [VIGIL_OPCODE_EQUAL] = &&op_EQUAL,
                [VIGIL_OPCODE_FALSE] = &&op_FALSE,
                [VIGIL_OPCODE_FORMAT_F64] = &&op_FORMAT_F64,
                [VIGIL_OPCODE_GET_CAPTURE] = &&op_GET_CAPTURE,
                [VIGIL_OPCODE_GET_COLLECTION_SIZE] = &&op_GET_COLLECTION_SIZE,
                [VIGIL_OPCODE_GET_ERROR_KIND] = &&op_GET_ERROR_KIND,
                [VIGIL_OPCODE_GET_ERROR_MESSAGE] = &&op_GET_ERROR_MESSAGE,
                [VIGIL_OPCODE_GET_FIELD] = &&op_GET_FIELD,
                [VIGIL_OPCODE_GET_FUNCTION] = &&op_GET_FUNCTION,
                [VIGIL_OPCODE_GET_GLOBAL] = &&op_GET_GLOBAL,
                [VIGIL_OPCODE_GET_INDEX] = &&op_GET_INDEX,
                [VIGIL_OPCODE_GET_LOCAL] = &&op_GET_LOCAL,
                [VIGIL_OPCODE_GET_MAP_KEY_AT] = &&op_GET_MAP_KEY_AT,
                [VIGIL_OPCODE_GET_MAP_VALUE_AT] = &&op_GET_MAP_VALUE_AT,
                [VIGIL_OPCODE_GET_STRING_SIZE] = &&op_GET_STRING_SIZE,
                [VIGIL_OPCODE_GREATER] = &&op_GREATER,
                [VIGIL_OPCODE_JUMP] = &&op_JUMP,
                [VIGIL_OPCODE_JUMP_IF_FALSE] = &&op_JUMP_IF_FALSE,
                [VIGIL_OPCODE_LESS] = &&op_LESS,
                [VIGIL_OPCODE_LOOP] = &&op_LOOP,
                [VIGIL_OPCODE_MAP_GET_SAFE] = &&op_MAP_GET_SAFE,
                [VIGIL_OPCODE_MAP_HAS] = &&op_MAP_HAS,
                [VIGIL_OPCODE_MAP_KEYS] = &&op_MAP_KEYS,
                [VIGIL_OPCODE_MAP_REMOVE_SAFE] = &&op_MAP_REMOVE_SAFE,
                [VIGIL_OPCODE_MAP_SET_SAFE] = &&op_MAP_SET_SAFE,
                [VIGIL_OPCODE_MAP_VALUES] = &&op_MAP_VALUES,
                [VIGIL_OPCODE_ADD_I64] = &&op_ADD_I64,
                [VIGIL_OPCODE_SUBTRACT_I64] = &&op_SUBTRACT_I64,
                [VIGIL_OPCODE_LESS_I64] = &&op_LESS_I64,
                [VIGIL_OPCODE_LESS_EQUAL_I64] = &&op_LESS_EQUAL_I64,
                [VIGIL_OPCODE_GREATER_I64] = &&op_GREATER_I64,
                [VIGIL_OPCODE_GREATER_EQUAL_I64] = &&op_GREATER_EQUAL_I64,
                [VIGIL_OPCODE_MULTIPLY_I64] = &&op_MULTIPLY_I64,
                [VIGIL_OPCODE_DIVIDE_I64] = &&op_DIVIDE_I64,
                [VIGIL_OPCODE_MODULO_I64] = &&op_MODULO_I64,
                [VIGIL_OPCODE_EQUAL_I64] = &&op_EQUAL_I64,
                [VIGIL_OPCODE_NOT_EQUAL_I64] = &&op_NOT_EQUAL_I64,
                [VIGIL_OPCODE_LOCALS_ADD_I64] = &&op_LOCALS_ADD_I64,
                [VIGIL_OPCODE_LOCALS_SUBTRACT_I64] = &&op_LOCALS_SUBTRACT_I64,
                [VIGIL_OPCODE_LOCALS_MULTIPLY_I64] = &&op_LOCALS_MULTIPLY_I64,
                [VIGIL_OPCODE_LOCALS_MODULO_I64] = &&op_LOCALS_MODULO_I64,
                [VIGIL_OPCODE_LOCALS_LESS_I64] = &&op_LOCALS_LESS_I64,
                [VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64] = &&op_LOCALS_LESS_EQUAL_I64,
                [VIGIL_OPCODE_LOCALS_GREATER_I64] = &&op_LOCALS_GREATER_I64,
                [VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64] = &&op_LOCALS_GREATER_EQUAL_I64,
                [VIGIL_OPCODE_LOCALS_EQUAL_I64] = &&op_LOCALS_EQUAL_I64,
                [VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64] = &&op_LOCALS_NOT_EQUAL_I64,
                [VIGIL_OPCODE_ADD_I32] = &&op_ADD_I32,
                [VIGIL_OPCODE_SUBTRACT_I32] = &&op_SUBTRACT_I32,
                [VIGIL_OPCODE_MULTIPLY_I32] = &&op_MULTIPLY_I32,
                [VIGIL_OPCODE_DIVIDE_I32] = &&op_DIVIDE_I32,
                [VIGIL_OPCODE_MODULO_I32] = &&op_MODULO_I32,
                [VIGIL_OPCODE_LESS_I32] = &&op_LESS_I32,
                [VIGIL_OPCODE_LESS_EQUAL_I32] = &&op_LESS_EQUAL_I32,
                [VIGIL_OPCODE_GREATER_I32] = &&op_GREATER_I32,
                [VIGIL_OPCODE_GREATER_EQUAL_I32] = &&op_GREATER_EQUAL_I32,
                [VIGIL_OPCODE_EQUAL_I32] = &&op_EQUAL_I32,
                [VIGIL_OPCODE_NOT_EQUAL_I32] = &&op_NOT_EQUAL_I32,
                [VIGIL_OPCODE_LOCALS_ADD_I32_STORE] = &&op_LOCALS_ADD_I32_STORE,
                [VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE] = &&op_LOCALS_SUBTRACT_I32_STORE,
                [VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE] = &&op_LOCALS_MULTIPLY_I32_STORE,
                [VIGIL_OPCODE_LOCALS_MODULO_I32_STORE] = &&op_LOCALS_MODULO_I32_STORE,
                [VIGIL_OPCODE_LOCALS_LESS_I32_STORE] = &&op_LOCALS_LESS_I32_STORE,
                [VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE] = &&op_LOCALS_LESS_EQUAL_I32_STORE,
                [VIGIL_OPCODE_LOCALS_GREATER_I32_STORE] = &&op_LOCALS_GREATER_I32_STORE,
                [VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE] = &&op_LOCALS_GREATER_EQUAL_I32_STORE,
                [VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE] = &&op_LOCALS_EQUAL_I32_STORE,
                [VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE] = &&op_LOCALS_NOT_EQUAL_I32_STORE,
                [VIGIL_OPCODE_INCREMENT_LOCAL_I32] = &&op_INCREMENT_LOCAL_I32,
                [VIGIL_OPCODE_TAIL_CALL] = &&op_TAIL_CALL,
                [VIGIL_OPCODE_FORLOOP_I32] = &&op_FORLOOP_I32,
                [VIGIL_OPCODE_CALL_NATIVE] = &&op_CALL_NATIVE,
                [VIGIL_OPCODE_DEFER_CALL_NATIVE] = &&op_DEFER_CALL_NATIVE,
                // clang-format off
                [VIGIL_OPCODE_CALL_EXTERN]=&&op_CALL_EXTERN, [VIGIL_OPCODE_MATH_SIN_F64]=&&op_MATH_SIN_F64, [VIGIL_OPCODE_MATH_COS_F64]=&&op_MATH_COS_F64, [VIGIL_OPCODE_MATH_SQRT_F64]=&&op_MATH_SQRT_F64, [VIGIL_OPCODE_MATH_LOG_F64]=&&op_MATH_LOG_F64, [VIGIL_OPCODE_MATH_POW_F64]=&&op_MATH_POW_F64, [VIGIL_OPCODE_CONSTANT_I32]=&&op_CONSTANT_I32,
                // clang-format on
                [VIGIL_OPCODE_MODULO] = &&op_MODULO,
                [VIGIL_OPCODE_MULTIPLY] = &&op_MULTIPLY,
                [VIGIL_OPCODE_NEGATE] = &&op_NEGATE,
                [VIGIL_OPCODE_NEW_ARRAY] = &&op_NEW_ARRAY,
                [VIGIL_OPCODE_NEW_CLOSURE] = &&op_NEW_CLOSURE,
                [VIGIL_OPCODE_NEW_ERROR] = &&op_NEW_ERROR,
                [VIGIL_OPCODE_NEW_INSTANCE] = &&op_NEW_INSTANCE,
                [VIGIL_OPCODE_NEW_MAP] = &&op_NEW_MAP,
                [VIGIL_OPCODE_NIL] = &&op_NIL,
                [VIGIL_OPCODE_NOT] = &&op_NOT,
                [VIGIL_OPCODE_POP] = &&op_POP,
                [VIGIL_OPCODE_RETURN] = &&op_RETURN,
                [VIGIL_OPCODE_SET_CAPTURE] = &&op_SET_CAPTURE,
                [VIGIL_OPCODE_SET_FIELD] = &&op_SET_FIELD,
                [VIGIL_OPCODE_SET_GLOBAL] = &&op_SET_GLOBAL,
                [VIGIL_OPCODE_SET_INDEX] = &&op_SET_INDEX,
                [VIGIL_OPCODE_SET_LOCAL] = &&op_SET_LOCAL,
                [VIGIL_OPCODE_SHIFT_LEFT] = &&op_SHIFT_LEFT,
                [VIGIL_OPCODE_SHIFT_RIGHT] = &&op_SHIFT_RIGHT,
                [VIGIL_OPCODE_STRING_BYTES] = &&op_STRING_BYTES,
                [VIGIL_OPCODE_STRING_CHAR_AT] = &&op_STRING_CHAR_AT,
                [VIGIL_OPCODE_STRING_CONTAINS] = &&op_STRING_CONTAINS,
                [VIGIL_OPCODE_STRING_ENDS_WITH] = &&op_STRING_ENDS_WITH,
                [VIGIL_OPCODE_STRING_INDEX_OF] = &&op_STRING_INDEX_OF,
                [VIGIL_OPCODE_STRING_REPLACE] = &&op_STRING_REPLACE,
                [VIGIL_OPCODE_STRING_SPLIT] = &&op_STRING_SPLIT,
                [VIGIL_OPCODE_STRING_STARTS_WITH] = &&op_STRING_STARTS_WITH,
                [VIGIL_OPCODE_STRING_SUBSTR] = &&op_STRING_SUBSTR,
                [VIGIL_OPCODE_STRING_TO_LOWER] = &&op_STRING_TO_LOWER,
                [VIGIL_OPCODE_STRING_TO_UPPER] = &&op_STRING_TO_UPPER,
                [VIGIL_OPCODE_STRING_TRIM] = &&op_STRING_TRIM,
                [VIGIL_OPCODE_STRING_TRIM_LEFT] = &&op_STRING_TRIM_LEFT,
                [VIGIL_OPCODE_STRING_TRIM_RIGHT] = &&op_STRING_TRIM_RIGHT,
                [VIGIL_OPCODE_STRING_REPEAT] = &&op_STRING_REPEAT,
                [VIGIL_OPCODE_STRING_REVERSE] = &&op_STRING_REVERSE,
                [VIGIL_OPCODE_STRING_IS_EMPTY] = &&op_STRING_IS_EMPTY,
                [VIGIL_OPCODE_STRING_COUNT] = &&op_STRING_COUNT,
                [VIGIL_OPCODE_STRING_LAST_INDEX_OF] = &&op_STRING_LAST_INDEX_OF,
                [VIGIL_OPCODE_STRING_TRIM_PREFIX] = &&op_STRING_TRIM_PREFIX,
                [VIGIL_OPCODE_STRING_TRIM_SUFFIX] = &&op_STRING_TRIM_SUFFIX,
                [VIGIL_OPCODE_CHAR_FROM_INT] = &&op_CHAR_FROM_INT,
                [VIGIL_OPCODE_STRING_TO_C] = &&op_STRING_TO_C,
                [VIGIL_OPCODE_STRING_JOIN] = &&op_STRING_JOIN,
                [VIGIL_OPCODE_STRING_CUT] = &&op_STRING_CUT,
                [VIGIL_OPCODE_STRING_FIELDS] = &&op_STRING_FIELDS,
                [VIGIL_OPCODE_STRING_EQUAL_FOLD] = &&op_STRING_EQUAL_FOLD,
                [VIGIL_OPCODE_STRING_CHAR_COUNT] = &&op_STRING_CHAR_COUNT,
                [VIGIL_OPCODE_FORMAT_SPEC] = &&op_FORMAT_SPEC,
                [VIGIL_OPCODE_SUBTRACT] = &&op_SUBTRACT,
                [VIGIL_OPCODE_TO_F64] = &&op_TO_F64,
                [VIGIL_OPCODE_TO_I32] = &&op_TO_I32,
                [VIGIL_OPCODE_TO_I64] = &&op_TO_I64,
                [VIGIL_OPCODE_TO_STRING] = &&op_TO_STRING,
                [VIGIL_OPCODE_TO_U32] = &&op_TO_U32,
                [VIGIL_OPCODE_TO_U64] = &&op_TO_U64,
                [VIGIL_OPCODE_TO_U8] = &&op_TO_U8,
                [VIGIL_OPCODE_TRUE] = &&op_TRUE,
            };
#define VM_DISPATCH()                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (vm->debug_hook != NULL)                                                                                    \
        {                                                                                                              \
            if (vm->debug_hook(vm, vm->debug_hook_userdata) != 0)                                                      \
            {                                                                                                          \
                status = VIGIL_STATUS_OK;                                                                              \
                goto cleanup;                                                                                          \
            }                                                                                                          \
        }                                                                                                              \
        if (dispatch_table[code[frame->ip]] == NULL)                                                                   \
        {                                                                                                              \
            status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_UNSUPPORTED, "unsupported opcode", error);                   \
            goto cleanup;                                                                                              \
        }                                                                                                              \
        goto *dispatch_table[code[frame->ip]];                                                                         \
    } while (0)
#define VM_CASE(op) op_##op:
#define VM_BREAK()                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (frame->ip >= code_size)                                                                                    \
            goto vm_loop_end;                                                                                          \
        VM_DISPATCH();                                                                                                 \
    } while (0)
#define VM_BREAK_RELOAD()                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        frame = &vm->frames[vm->frame_count - 1U];                                                                     \
        code = VIGIL_VM_CHUNK_CODE(frame->chunk);                                                                      \
        code_size = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);                                                            \
        if (frame->ip >= code_size)                                                                                    \
            goto vm_loop_end;                                                                                          \
        VM_DISPATCH();                                                                                                 \
    } while (0)
            VM_DISPATCH();
#else
#define VM_CASE(op) case VIGIL_OPCODE_##op:
#define VM_BREAK() break
#define VM_BREAK_RELOAD() break

        if (vm->debug_hook != NULL)
        {
            if (vm->debug_hook(vm, vm->debug_hook_userdata) != 0)
            {
                status = VIGIL_STATUS_OK;
                goto cleanup;
            }
        }
        switch ((vigil_opcode_t)code[frame->ip])
        {
#endif
            VM_CASE(CONSTANT)
            VIGIL_VM_READ_U32(code, frame->ip, constant_index);
            constant = VIGIL_VM_CHUNK_CONSTANT(frame->chunk, (size_t)constant_index);
            if (constant == NULL)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "constant index out of range", error);
                goto cleanup;
            }
            /* Fast path: non-object constants (int, float, bool) —
               skip VALUE_COPY retain and PUSH capacity check. */
            if (!vigil_nanbox_has_object(*constant))
            {
                if (vm->stack_count >= vm->stack_capacity)
                {
                    status = vigil_vm_grow_stack(vm, vm->stack_count + 1U, error);
                    if (status != VIGIL_STATUS_OK)
                        goto cleanup;
                }
                vm->stack[vm->stack_count] = *constant;
                vm->stack_count += 1U;
            }
            else
            {
                VIGIL_VM_PUSH(vm, constant);
            }
            VM_BREAK();
            // clang-format off
            VM_CASE(CONSTANT_I32) { uint32_t raw; VIGIL_VM_READ_U32(code, frame->ip, raw); value = vigil_nanbox_encode_i32((int32_t)raw); VIGIL_VM_PUSH(vm, &value); } VM_BREAK();
            // clang-format on
            VM_CASE(POP)
            VIGIL_VM_POP(vm, value);
            VIGIL_VM_VALUE_RELEASE(&value);
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(DUP)
            peeked = vigil_vm_peek(vm, 0U);
            if (peeked == NULL)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "dup requires a value on the stack", error);
                goto cleanup;
            }
            VIGIL_VM_VALUE_COPY(&value, peeked);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(DUP_TWO)
            left_peek = vigil_vm_peek(vm, 1U);
            peeked = vigil_vm_peek(vm, 0U);
            if (left_peek == NULL || peeked == NULL)
            {
                status =
                    vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "dup_two requires two values on the stack", error);
                goto cleanup;
            }

            VIGIL_VM_VALUE_COPY(&left, left_peek);
            status = vigil_vm_push(vm, &left, error);
            VIGIL_VM_VALUE_RELEASE(&left);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }

            VIGIL_VM_VALUE_COPY(&value, peeked);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(GET_LOCAL)
            VIGIL_VM_READ_U32(code, frame->ip, operand);
            local_index = frame->base_slot + (size_t)operand;
            /* Fast path: non-object values (int, bool, float, nil)
               don't need retain/release — just copy the struct. */
            if (!vigil_nanbox_has_object(vm->stack[local_index]))
            {
                if (vm->stack_count >= vm->stack_capacity)
                {
                    status = vigil_vm_grow_stack(vm, vm->stack_count + 1U, error);
                    if (status != VIGIL_STATUS_OK)
                        goto cleanup;
                }
                vm->stack[vm->stack_count] = vm->stack[local_index];
                vm->stack_count += 1U;
            }
            else
            {
                VIGIL_VM_VALUE_COPY(&value, &vm->stack[local_index]);
                VIGIL_VM_PUSH(vm, &value);
                VIGIL_VM_VALUE_RELEASE(&value);
            }
            VM_BREAK();
            VM_CASE(SET_LOCAL)
            VIGIL_VM_READ_U32(code, frame->ip, operand);
            local_index = frame->base_slot + (size_t)operand;
            /* Fast path for non-object values: skip retain/release. */
            if (vm->stack_count > 0U && !vigil_nanbox_has_object(vm->stack[vm->stack_count - 1U]))
            {
                vm->stack[local_index] = vm->stack[vm->stack_count - 1U];
                /* SET_LOCAL + POP fusion: if next opcode is POP, consume
                   it here by popping the stack top directly. */
                if (frame->ip < code_size && code[frame->ip] == VIGIL_OPCODE_POP)
                {
                    vm->stack_count -= 1U;
                    frame->ip += 1U;
                }
            }
            else if (vm->stack_count > 0U)
            {
                VIGIL_VM_VALUE_RELEASE(&vm->stack[local_index]);
                VIGIL_VM_VALUE_COPY(&vm->stack[local_index], &vm->stack[vm->stack_count - 1U]);
                if (frame->ip < code_size && code[frame->ip] == VIGIL_OPCODE_POP)
                {
                    vm->stack_count -= 1U;
                    VIGIL_VM_VALUE_RELEASE(&vm->stack[vm->stack_count]);
                    frame->ip += 1U;
                }
            }
            else
            {
                status =
                    vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "assignment requires a value on the stack", error);
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(GET_GLOBAL)
            status = vigil_vm_read_u32(vm, &operand, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }

            frame = vigil_vm_current_frame(vm);
            if (frame == NULL || frame->function == NULL)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "global read requires a function-backed frame",
                                             error);
                goto cleanup;
            }

            VIGIL_VM_VALUE_INIT_NIL(&value);
            if (!vigil_function_object_get_global(frame->function, (size_t)operand, &value))
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "global index out of range", error);
                goto cleanup;
            }

            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(SET_GLOBAL)
            status = vigil_vm_read_u32(vm, &operand, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }

            peeked = vigil_vm_peek(vm, 0U);
            if (peeked == NULL)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL,
                                             "global assignment requires a value on the stack", error);
                goto cleanup;
            }

            frame = vigil_vm_current_frame(vm);
            if (frame == NULL || frame->function == NULL)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL,
                                             "global assignment requires a function-backed frame", error);
                goto cleanup;
            }

            status = vigil_function_object_set_global(frame->function, (size_t)operand, peeked, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(GET_FUNCTION)
            status = vigil_vm_read_u32(vm, &operand, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }

            frame = vigil_vm_current_frame(vm);
            if (frame == NULL || frame->function == NULL)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL,
                                             "function reference requires a function-backed frame", error);
                goto cleanup;
            }

            {
                const vigil_object_t *callee;
                vigil_object_t *retained;

                callee = vigil_function_object_sibling(frame->function, (size_t)operand);
                if (callee == NULL || vigil_object_type(callee) != VIGIL_OBJECT_FUNCTION)
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "function index out of range", error);
                    goto cleanup;
                }
                retained = (vigil_object_t *)callee;
                vigil_object_retain(retained);
                vigil_value_init_object(&value, &retained);
            }

            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(NEW_CLOSURE)
            {
                vigil_object_t *closure;
                const vigil_object_t *callee;
                size_t capture_count;
                size_t function_index;
                size_t base_slot;

                status = vigil_vm_read_u32(vm, &constant_index, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                status = vigil_vm_read_raw_u32(vm, &operand, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                function_index = (size_t)constant_index;
                capture_count = (size_t)operand;

                frame = vigil_vm_current_frame(vm);
                if (frame == NULL || frame->function == NULL)
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL,
                                                 "closure creation requires a function-backed frame", error);
                    goto cleanup;
                }
                if (capture_count > vm->stack_count)
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL,
                                                 "closure captures are missing from the stack", error);
                    goto cleanup;
                }

                callee = vigil_function_object_sibling(frame->function, function_index);
                if (callee == NULL || vigil_object_type(callee) != VIGIL_OBJECT_FUNCTION)
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "closure function index is invalid", error);
                    goto cleanup;
                }

                base_slot = vm->stack_count - capture_count;
                closure = NULL;
                status = vigil_closure_object_new(vm->runtime, (vigil_object_t *)callee,
                                                  capture_count == 0U ? NULL : vm->stack + base_slot, capture_count,
                                                  &closure, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }

                vigil_vm_unwind_stack_to(vm, base_slot);
                vigil_value_init_object(&value, &closure);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(GET_CAPTURE)
            status = vigil_vm_read_u32(vm, &operand, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }

            frame = vigil_vm_current_frame(vm);
            if (frame == NULL || frame->callable == NULL || vigil_object_type(frame->callable) != VIGIL_OBJECT_CLOSURE)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "capture read requires a closure-backed frame",
                                             error);
                goto cleanup;
            }
            if (!vigil_closure_object_get_capture(frame->callable, (size_t)operand, &value))
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "capture index out of range", error);
                goto cleanup;
            }
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(SET_CAPTURE)
            status = vigil_vm_read_u32(vm, &operand, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }

            peeked = vigil_vm_peek(vm, 0U);
            if (peeked == NULL)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL,
                                             "capture assignment requires a value on the stack", error);
                goto cleanup;
            }
            frame = vigil_vm_current_frame(vm);
            if (frame == NULL || frame->callable == NULL || vigil_object_type(frame->callable) != VIGIL_OBJECT_CLOSURE)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL,
                                             "capture assignment requires a closure-backed frame", error);
                goto cleanup;
            }
            status =
                vigil_closure_object_set_capture((vigil_object_t *)frame->callable, (size_t)operand, peeked, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(CALL)
            {
                const vigil_object_t *callee;
                size_t base_slot;

                VIGIL_VM_READ_U32(code, frame->ip, constant_index);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, operand);

                callee = vigil_vm_function_sibling(frame->function, (size_t)constant_index);
                base_slot = vm->stack_count - (size_t)operand;

                /* Fast path: frame capacity available (pre-allocated 64).
                   Skip memset — only set the fields we need.  The defer
                   and pending_return fields are already zero from either
                   initial allocation or the RETURN fast path. */
                if (vm->frame_count < vm->frame_capacity)
                {
                    vigil_vm_frame_t *nf = &vm->frames[vm->frame_count];
                    nf->callable = callee;
                    nf->function = callee;
                    nf->chunk = vigil_vm_function_chunk(callee);
                    nf->ip = 0U;
                    nf->base_slot = base_slot;
                    vm->frame_count += 1U;
                }
                else
                {
                    status = vigil_vm_push_frame(vm, callee, callee, vigil_vm_function_chunk(callee), base_slot, error);
                    if (status != VIGIL_STATUS_OK)
                    {
                        goto cleanup;
                    }
                }
                VM_BREAK_RELOAD();
            }
            VM_CASE(TAIL_CALL)
            {
                const vigil_object_t *callee;
                size_t arg_count;
                size_t arg_src;
                size_t dst;
                size_t i;

                VIGIL_VM_READ_U32(code, frame->ip, constant_index);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, operand);
                arg_count = (size_t)operand;

                callee = vigil_vm_function_sibling(frame->function, (size_t)constant_index);

                /* Source: arguments are at top of stack. */
                arg_src = vm->stack_count - arg_count;
                dst = frame->base_slot;

                /* Release old locals that will be overwritten. */
                for (i = dst; i < dst + arg_count && i < arg_src; i++)
                {
                    VIGIL_VM_VALUE_RELEASE(&vm->stack[i]);
                }
                /* Release any remaining old locals beyond arg_count. */
                for (i = dst + arg_count; i < arg_src; i++)
                {
                    VIGIL_VM_VALUE_RELEASE(&vm->stack[i]);
                }

                /* Move arguments down (may overlap if arg_count > old locals). */
                if (dst != arg_src)
                {
                    memmove(&vm->stack[dst], &vm->stack[arg_src], arg_count * sizeof(vigil_value_t));
                }

                vm->stack_count = dst + arg_count;

                /* Reuse the current frame. */
                frame->callable = callee;
                frame->function = callee;
                frame->chunk = vigil_vm_function_chunk(callee);
                frame->ip = 0U;
                /* base_slot stays the same */

                code = VIGIL_VM_CHUNK_CODE(frame->chunk);
                code_size = VIGIL_VM_CHUNK_CODE_SIZE(frame->chunk);
                VM_BREAK_RELOAD();
            }
            VM_CASE(CALL_VALUE)
            status = vigil_vm_read_u32(vm, &operand, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            status = vigil_vm_invoke_value_call(vm, (size_t)operand, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK_RELOAD();
            VM_CASE(CALL_NATIVE)
            {
                uint32_t native_arg_count;
                const vigil_value_t *native_val;
                vigil_object_t *native_obj;
                vigil_native_fn_t native_fn;

                VIGIL_VM_READ_U32(code, frame->ip, constant_index);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, native_arg_count);

                native_val = VIGIL_VM_CHUNK_CONSTANT(frame->chunk, (size_t)constant_index);
                native_obj = (vigil_object_t *)vigil_nanbox_decode_ptr(*native_val);
                native_fn = vigil_native_function_get(native_obj);
                if (native_fn == NULL)
                {
                    status =
                        vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "call target is not a native function", error);
                    goto cleanup;
                }
                status = native_fn(vm, (size_t)native_arg_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(DEFER_CALL_NATIVE)
            {
                uint32_t native_defer_arg_count;

                VIGIL_VM_READ_U32(code, frame->ip, constant_index);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, native_defer_arg_count);

                frame = vigil_vm_current_frame(vm);
                status = vigil_vm_schedule_defer(vm, frame, VIGIL_VM_DEFER_CALL_NATIVE, constant_index, 0U,
                                                 native_defer_arg_count, (size_t)native_defer_arg_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(CALL_EXTERN)
            {
                uint32_t extern_arg_count;
                const vigil_value_t *desc_val;
                const vigil_object_t *desc_obj;
                const char *desc_data;
                size_t desc_len;

                VIGIL_VM_READ_U32(code, frame->ip, constant_index);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, extern_arg_count);

                desc_val = VIGIL_VM_CHUNK_CONSTANT(frame->chunk, (size_t)constant_index);
                desc_obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(*desc_val);
                desc_data = vigil_string_object_c_str(desc_obj);
                desc_len = vigil_string_object_length(desc_obj);

                status = vigil_vm_call_extern(vm, desc_data, desc_len, (size_t)extern_arg_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            // clang-format off
            /* Math intrinsics */ VM_CASE(MATH_SIN_F64) VM_CASE(MATH_COS_F64) VM_CASE(MATH_SQRT_F64) VM_CASE(MATH_LOG_F64) VM_CASE(MATH_POW_F64) vigil_vm_math_dispatch(vm, (vigil_opcode_t)code[frame->ip]); frame->ip += 1U; VIGIL_VM_MATH_NEXT(dispatch_table, code, frame->ip);
            // clang-format on
            VM_CASE(CALL_INTERFACE)
            {
                size_t interface_index;
                size_t method_index;
                size_t arg_count;

                status = vigil_vm_read_u32(vm, &constant_index, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                status = vigil_vm_read_raw_u32(vm, &operand, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                interface_index = (size_t)constant_index;
                method_index = (size_t)operand;

                status = vigil_vm_read_raw_u32(vm, &operand, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                arg_count = (size_t)operand;
                frame = vigil_vm_current_frame(vm);
                status = vigil_vm_invoke_interface_call(vm, frame, interface_index, method_index, arg_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK_RELOAD();
            }
            VM_CASE(NEW_INSTANCE)
            {
                size_t class_index;
                size_t field_count;

                status = vigil_vm_read_u32(vm, &operand, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                class_index = (size_t)operand;

                status = vigil_vm_read_raw_u32(vm, &operand, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }

                field_count = (size_t)operand;
                status = vigil_vm_invoke_new_instance(vm, class_index, field_count, 0, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(NEW_ARRAY)
            {
                size_t type_index;
                size_t item_count;

                status = vigil_vm_read_u32(vm, &operand, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                type_index = (size_t)operand;

                status = vigil_vm_read_raw_u32(vm, &operand, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }

                item_count = (size_t)operand;
                status = vigil_vm_invoke_new_array(vm, type_index, item_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(NEW_MAP)
            {
                size_t type_index;
                size_t pair_count;

                status = vigil_vm_read_u32(vm, &operand, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                type_index = (size_t)operand;

                status = vigil_vm_read_raw_u32(vm, &operand, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }

                pair_count = (size_t)operand;
                status = vigil_vm_invoke_new_map(vm, type_index, pair_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(DEFER_CALL)
            {
                uint32_t arg_count;

                status = vigil_vm_read_u32(vm, &constant_index, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                status = vigil_vm_read_raw_u32(vm, &arg_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                frame = vigil_vm_current_frame(vm);
                status = vigil_vm_schedule_defer(vm, frame, VIGIL_VM_DEFER_CALL, constant_index, 0U, arg_count,
                                                 (size_t)arg_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(DEFER_CALL_VALUE)
            {
                uint32_t arg_count;

                status = vigil_vm_read_u32(vm, &arg_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                frame = vigil_vm_current_frame(vm);
                status = vigil_vm_schedule_defer(vm, frame, VIGIL_VM_DEFER_CALL_VALUE, 0U, 0U, arg_count,
                                                 (size_t)arg_count + 1U, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(DEFER_CALL_INTERFACE)
            {
                uint32_t interface_index;
                uint32_t method_index;
                uint32_t arg_count;

                status = vigil_vm_read_u32(vm, &interface_index, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                status = vigil_vm_read_raw_u32(vm, &method_index, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                status = vigil_vm_read_raw_u32(vm, &arg_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                frame = vigil_vm_current_frame(vm);
                status = vigil_vm_schedule_defer(vm, frame, VIGIL_VM_DEFER_CALL_INTERFACE, interface_index,
                                                 method_index, arg_count, (size_t)arg_count + 1U, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(DEFER_NEW_INSTANCE)
            {
                uint32_t class_index;
                uint32_t field_count;

                status = vigil_vm_read_u32(vm, &class_index, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                status = vigil_vm_read_raw_u32(vm, &field_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                frame = vigil_vm_current_frame(vm);
                status = vigil_vm_schedule_defer(vm, frame, VIGIL_VM_DEFER_NEW_INSTANCE, class_index, 0U, field_count,
                                                 (size_t)field_count, error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(GET_FIELD)
            status = vigil_vm_read_u32(vm, &operand, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }

            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_INSTANCE)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "field access requires a class instance", error);
                goto cleanup;
            }

            if (!vigil_instance_object_get_field(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), (size_t)operand,
                                                 &value))
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "field index out of range", error);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&left);

            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(SET_FIELD)
            status = vigil_vm_read_u32(vm, &operand, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }

            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_INSTANCE)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "field assignment requires a class instance", error);
                goto cleanup;
            }

            status = vigil_instance_object_set_field(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), (size_t)operand,
                                                     &right, error);
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(GET_INDEX)
            frame->ip += 1U;
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);

            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "index access requires an array or map",
                                             error);
                goto cleanup;
            }

            if (vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) == VIGIL_OBJECT_ARRAY)
            {
                if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&(right)) < 0)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "array index must be a non-negative i32", error);
                    goto cleanup;
                }

                if (!vigil_array_object_get(((vigil_object_t *)vigil_nanbox_decode_ptr(left)),
                                            (size_t)vigil_value_as_int(&(right)), &value))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status =
                        vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array index is out of range", error);
                    goto cleanup;
                }
            }
            else if (vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) == VIGIL_OBJECT_MAP)
            {
                if (!vigil_vm_value_is_supported_map_key(&right))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "map index must be i32, bool, or string", error);
                    goto cleanup;
                }

                if (!vigil_map_object_get(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), &right, &value))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map key is not present", error);
                    goto cleanup;
                }
            }
            else
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "index access requires an array or map",
                                             error);
                goto cleanup;
            }

            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(GET_COLLECTION_SIZE)
            frame->ip += 1U;
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "collection size requires an array or map", error);
                goto cleanup;
            }

            if (vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) == VIGIL_OBJECT_ARRAY)
            {
                vigil_value_init_int(
                    &value, (int64_t)vigil_array_object_length(((vigil_object_t *)vigil_nanbox_decode_ptr(left))));
            }
            else if (vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) == VIGIL_OBJECT_MAP)
            {
                vigil_value_init_int(
                    &value, (int64_t)vigil_map_object_count(((vigil_object_t *)vigil_nanbox_decode_ptr(left))));
            }
            else
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "collection size requires an array or map", error);
                goto cleanup;
            }

            VIGIL_VM_VALUE_RELEASE(&left);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(ARRAY_PUSH)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_ARRAY)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "array push() requires an array receiver", error);
                goto cleanup;
            }
            status = vigil_array_object_append(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), &value, error);
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(ARRAY_POP)
            frame->ip += 1U;
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_ARRAY)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "array pop() requires an array receiver", error);
                goto cleanup;
            }
            if (vigil_array_object_pop(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), &value))
            {
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_INIT_NIL(&right);
                status = vigil_vm_make_ok_error_value(vm, &right, error);
            }
            else
            {
                status = vigil_vm_make_bounds_error_value(vm, "array is empty", &value, error);
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                goto cleanup;
            }
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status == VIGIL_STATUS_OK)
            {
                status = vigil_vm_push(vm, &right, error);
            }
            VIGIL_VM_VALUE_RELEASE(&right);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(ARRAY_GET_SAFE)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_ARRAY)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "array get() requires an array receiver", error);
                goto cleanup;
            }
            if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&(right)) < 0)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "array get() index must be a non-negative i32", error);
                goto cleanup;
            }
            {
                vigil_value_t item;
                int found;

                VIGIL_VM_VALUE_INIT_NIL(&item);
                found = vigil_array_object_get(((vigil_object_t *)vigil_nanbox_decode_ptr(left)),
                                               (size_t)vigil_value_as_int(&(right)), &item);
                if (found)
                {
                    VIGIL_VM_VALUE_RELEASE(&value);
                    value = item;
                    status = vigil_vm_make_ok_error_value(vm, &right, error);
                }
                else
                {
                    status = vigil_vm_make_bounds_error_value(vm, "array index is out of range", &right, error);
                }
                if (!found)
                {
                    vigil_value_release(&item);
                }
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                goto cleanup;
            }
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status == VIGIL_STATUS_OK)
            {
                status = vigil_vm_push(vm, &right, error);
            }
            VIGIL_VM_VALUE_RELEASE(&right);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(ARRAY_SET_SAFE)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_ARRAY)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "array set() requires an array receiver", error);
                goto cleanup;
            }
            if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&(right)) < 0)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "array set() index must be a non-negative i32", error);
                goto cleanup;
            }
            status = vigil_array_object_set(((vigil_object_t *)vigil_nanbox_decode_ptr(left)),
                                            (size_t)vigil_value_as_int(&(right)), &value, error);
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status == VIGIL_STATUS_OK)
            {
                status = vigil_vm_make_ok_error_value(vm, &value, error);
            }
            else if (status == VIGIL_STATUS_INVALID_ARGUMENT)
            {
                status = vigil_vm_make_bounds_error_value(vm, "array index is out of range", &value, error);
            }
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(ARRAY_SLICE)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_ARRAY)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "array slice() requires an array receiver", error);
                goto cleanup;
            }
            if (!vigil_nanbox_is_int(right) || !vigil_nanbox_is_int(value) || vigil_value_as_int(&(right)) < 0 ||
                vigil_value_as_int(&(value)) < 0)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "array slice() start and end must be non-negative i32 values", error);
                goto cleanup;
            }
            object = NULL;
            status = vigil_array_object_slice(((vigil_object_t *)vigil_nanbox_decode_ptr(left)),
                                              (size_t)vigil_value_as_int(&(right)),
                                              (size_t)vigil_value_as_int(&(value)), &object, error);
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                status =
                    vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "array slice range is out of bounds", error);
                goto cleanup;
            }
            vigil_value_init_object(&value, &object);
            vigil_object_release(&object);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(ARRAY_CONTAINS)
            frame->ip += 1U;
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_ARRAY)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "array contains() requires an array receiver", error);
                goto cleanup;
            }
            do
            {
                (value) = vigil_nanbox_from_bool(0);
            } while (0);
            {
                size_t item_count = vigil_array_object_length(((vigil_object_t *)vigil_nanbox_decode_ptr(left)));
                size_t item_index;
                vigil_value_t item;

                for (item_index = 0U; item_index < item_count; item_index += 1U)
                {
                    VIGIL_VM_VALUE_INIT_NIL(&item);
                    if (!vigil_array_object_get(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), item_index, &item))
                    {
                        continue;
                    }
                    if (vigil_vm_values_equal(&item, &right))
                    {
                        do
                        {
                            (value) = vigil_nanbox_from_bool(1);
                        } while (0);
                        vigil_value_release(&item);
                        break;
                    }
                    vigil_value_release(&item);
                }
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(MAP_GET_SAFE)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_MAP)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status =
                    vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map get() requires a map receiver", error);
                goto cleanup;
            }
            {
                vigil_value_t stored;
                int found;

                VIGIL_VM_VALUE_INIT_NIL(&stored);
                found = vigil_map_object_get(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), &right, &stored);
                VIGIL_VM_VALUE_RELEASE(&right);
                do
                {
                    (right) = vigil_nanbox_from_bool(found != 0);
                } while (0);
                if (found)
                {
                    VIGIL_VM_VALUE_RELEASE(&value);
                    value = stored;
                }
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status == VIGIL_STATUS_OK)
            {
                status = vigil_vm_push(vm, &right, error);
            }
            VIGIL_VM_VALUE_RELEASE(&right);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(MAP_SET_SAFE)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_MAP)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status =
                    vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map set() requires a map receiver", error);
                goto cleanup;
            }
            status = vigil_map_object_set(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), &right, &value, error);
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            status = vigil_vm_make_ok_error_value(vm, &value, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(MAP_REMOVE_SAFE)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_MAP)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map remove() requires a map receiver",
                                             error);
                goto cleanup;
            }
            {
                vigil_value_t removed_value;
                int removed;

                VIGIL_VM_VALUE_INIT_NIL(&removed_value);
                removed = vigil_map_object_remove(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), &right,
                                                  &removed_value, error);
                VIGIL_VM_VALUE_RELEASE(&right);
                do
                {
                    (right) = vigil_nanbox_from_bool(removed != 0);
                } while (0);
                if (removed)
                {
                    VIGIL_VM_VALUE_RELEASE(&value);
                    value = removed_value;
                }
                else
                {
                    vigil_value_release(&removed_value);
                }
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status == VIGIL_STATUS_OK)
            {
                status = vigil_vm_push(vm, &right, error);
            }
            VIGIL_VM_VALUE_RELEASE(&right);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(MAP_HAS)
            frame->ip += 1U;
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_MAP)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status =
                    vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map has() requires a map receiver", error);
                goto cleanup;
            }
            {
                vigil_value_t stored;
                int found;

                VIGIL_VM_VALUE_INIT_NIL(&stored);
                found = vigil_map_object_get(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), &right, &stored);
                vigil_value_release(&stored);
                VIGIL_VM_VALUE_RELEASE(&right);
                do
                {
                    (value) = vigil_nanbox_from_bool(found != 0);
                } while (0);
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(MAP_KEYS)
            VM_CASE(MAP_VALUES)
            frame->ip += 1U;
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_MAP)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                status =
                    vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map method requires a map receiver", error);
                goto cleanup;
            }
            {
                size_t item_count = vigil_map_object_count(((vigil_object_t *)vigil_nanbox_decode_ptr(left)));
                vigil_value_t *items = NULL;
                size_t item_capacity = 0U;
                size_t item_index;

                status = vigil_vm_grow_value_array(vm->runtime, &items, &item_capacity, item_count, error);
                for (item_index = 0U; status == VIGIL_STATUS_OK && item_index < item_count; item_index += 1U)
                {
                    VIGIL_VM_VALUE_INIT_NIL(&items[item_index]);
                    if ((vigil_opcode_t)code[frame->ip - 1U] == VIGIL_OPCODE_MAP_KEYS)
                    {
                        if (!vigil_map_object_key_at(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), item_index,
                                                     &items[item_index]))
                        {
                            status = VIGIL_STATUS_INTERNAL;
                        }
                    }
                    else
                    {
                        if (!vigil_map_object_value_at(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), item_index,
                                                       &items[item_index]))
                        {
                            status = VIGIL_STATUS_INTERNAL;
                        }
                    }
                }
                if (status == VIGIL_STATUS_OK)
                {
                    object = NULL;
                    status = vigil_array_object_new(vm->runtime, items, item_count, &object, error);
                }
                for (item_index = 0U; item_index < item_count; item_index += 1U)
                {
                    vigil_value_release(&items[item_index]);
                }
                vigil_runtime_free(vm->runtime, (void **)&items);
                VIGIL_VM_VALUE_RELEASE(&left);
                if (status != VIGIL_STATUS_OK)
                {
                    if (status == VIGIL_STATUS_INTERNAL)
                    {
                        status =
                            vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "failed to enumerate map entries", error);
                    }
                    goto cleanup;
                }
                vigil_value_init_object(&value, &object);
                vigil_object_release(&object);
            }
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(GET_STRING_SIZE)
            frame->ip += 1U;
            left = vigil_vm_pop_or_nil(vm);
            {
                const char *text;
                size_t length;

                if (!vigil_vm_get_string_parts(&left, &text, &length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string len() requires a string receiver", error);
                    goto cleanup;
                }
                vigil_value_init_int(&value, (int64_t)length);
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(STRING_CONTAINS)
            VM_CASE(STRING_STARTS_WITH)
            VM_CASE(STRING_ENDS_WITH)
            {
                vigil_opcode_t string_opcode = (vigil_opcode_t)code[frame->ip];
                const char *text;
                const char *needle;
                size_t text_length;
                size_t needle_length;
                int found;

                frame->ip += 1U;
                right = vigil_vm_pop_or_nil(vm);
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
                    !vigil_vm_get_string_parts(&right, &needle, &needle_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string method arguments must be strings", error);
                    goto cleanup;
                }

                found = 0;
                if (string_opcode == VIGIL_OPCODE_STRING_CONTAINS)
                {
                    found = vigil_vm_find_substring(text, text_length, needle, needle_length, NULL);
                }
                else if (string_opcode == VIGIL_OPCODE_STRING_STARTS_WITH)
                {
                    found = needle_length <= text_length && memcmp(text, needle, needle_length) == 0;
                }
                else
                {
                    found = needle_length <= text_length &&
                            memcmp(text + (text_length - needle_length), needle, needle_length) == 0;
                }
                do
                {
                    (value) = vigil_nanbox_from_bool(found);
                } while (0);
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(STRING_TRIM)
            VM_CASE(STRING_TO_UPPER)
            VM_CASE(STRING_TO_LOWER)
            {
                vigil_opcode_t string_opcode = (vigil_opcode_t)code[frame->ip];
                const char *text;
                size_t length;

                frame->ip += 1U;
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string method requires a string receiver", error);
                    goto cleanup;
                }

                if (string_opcode == VIGIL_OPCODE_STRING_TRIM)
                {
                    size_t start = 0U;
                    size_t end = length;

                    while (start < length && isspace((unsigned char)text[start]))
                    {
                        start += 1U;
                    }
                    while (end > start && isspace((unsigned char)text[end - 1U]))
                    {
                        end -= 1U;
                    }
                    status = vigil_vm_new_string_value(vm, text + start, end - start, &value, error);
                }
                else
                {
                    void *memory = NULL;
                    char *buffer;
                    size_t index;

                    status = vigil_runtime_alloc(vm->runtime, length + 1U, &memory, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        buffer = (char *)memory;
                        for (index = 0U; index < length; index += 1U)
                        {
                            buffer[index] = (char)(string_opcode == VIGIL_OPCODE_STRING_TO_UPPER
                                                       ? toupper((unsigned char)text[index])
                                                       : tolower((unsigned char)text[index]));
                        }
                        buffer[length] = '\0';
                        status = vigil_vm_new_string_value(vm, buffer, length, &value, error);
                        vigil_runtime_free(vm->runtime, &memory);
                    }
                }
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    goto cleanup;
                }
                VIGIL_VM_VALUE_RELEASE(&left);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }
            VM_CASE(STRING_REPLACE)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            {
                const char *text;
                const char *old_text;
                const char *new_text;
                size_t text_length;
                size_t old_length;
                size_t new_length;
                size_t index;
                size_t match_index;
                vigil_string_t built;

                if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
                    !vigil_vm_get_string_parts(&right, &old_text, &old_length) ||
                    !vigil_vm_get_string_parts(&value, &new_text, &new_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    VIGIL_VM_VALUE_RELEASE(&value);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string replace() arguments must be strings", error);
                    goto cleanup;
                }

                if (old_length == 0U)
                {
                    VIGIL_VM_VALUE_RELEASE(&right);
                    VIGIL_VM_VALUE_COPY(&right, &left);
                }
                else
                {
                    vigil_string_init(&built, vm->runtime);
                    index = 0U;
                    status = VIGIL_STATUS_OK;
                    while (index < text_length && status == VIGIL_STATUS_OK)
                    {
                        if (vigil_vm_find_substring(text + index, text_length - index, old_text, old_length,
                                                    &match_index))
                        {
                            status = vigil_string_append(&built, text + index, match_index, error);
                            if (status == VIGIL_STATUS_OK)
                            {
                                status = vigil_string_append(&built, new_text, new_length, error);
                            }
                            index += match_index + old_length;
                        }
                        else
                        {
                            status = vigil_string_append(&built, text + index, text_length - index, error);
                            break;
                        }
                    }
                    if (status == VIGIL_STATUS_OK)
                    {
                        VIGIL_VM_VALUE_RELEASE(&right);
                        status = vigil_vm_new_string_value(vm, vigil_string_c_str(&built), vigil_string_length(&built),
                                                           &right, error);
                    }
                    vigil_string_free(&built);
                    if (status != VIGIL_STATUS_OK)
                    {
                        VIGIL_VM_VALUE_RELEASE(&left);
                        VIGIL_VM_VALUE_RELEASE(&right);
                        VIGIL_VM_VALUE_RELEASE(&value);
                        goto cleanup;
                    }
                }
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&value);
            status = vigil_vm_push(vm, &right, error);
            VIGIL_VM_VALUE_RELEASE(&right);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(STRING_SPLIT)
            frame->ip += 1U;
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            {
                const char *text;
                const char *separator;
                size_t text_length;
                size_t separator_length;
                vigil_value_t *items = NULL;
                size_t item_count = 0U;
                size_t item_capacity = 0U;
                size_t index = 0U;
                vigil_object_t *array_object = NULL;
                size_t match_index;

                if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
                    !vigil_vm_get_string_parts(&right, &separator, &separator_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string split() arguments must be strings", error);
                    goto cleanup;
                }

                status = VIGIL_STATUS_OK;
                if (separator_length == 0U)
                {
                    while (index < text_length && status == VIGIL_STATUS_OK)
                    {
                        status = vigil_vm_grow_value_array(vm->runtime, &items, &item_capacity, item_count + 1U, error);
                        if (status == VIGIL_STATUS_OK)
                        {
                            VIGIL_VM_VALUE_INIT_NIL(&items[item_count]);
                            status = vigil_vm_new_string_value(vm, text + index, 1U, &items[item_count], error);
                            if (status == VIGIL_STATUS_OK)
                            {
                                item_count += 1U;
                            }
                        }
                        index += 1U;
                    }
                }
                else
                {
                    while (status == VIGIL_STATUS_OK)
                    {
                        status = vigil_vm_grow_value_array(vm->runtime, &items, &item_capacity, item_count + 1U, error);
                        if (status != VIGIL_STATUS_OK)
                        {
                            break;
                        }
                        VIGIL_VM_VALUE_INIT_NIL(&items[item_count]);
                        if (vigil_vm_find_substring(text + index, text_length - index, separator, separator_length,
                                                    &match_index))
                        {
                            status =
                                vigil_vm_new_string_value(vm, text + index, match_index, &items[item_count], error);
                            if (status == VIGIL_STATUS_OK)
                            {
                                item_count += 1U;
                            }
                            index += match_index + separator_length;
                        }
                        else
                        {
                            status = vigil_vm_new_string_value(vm, text + index, text_length - index,
                                                               &items[item_count], error);
                            if (status == VIGIL_STATUS_OK)
                            {
                                item_count += 1U;
                            }
                            break;
                        }
                    }
                }

                if (status == VIGIL_STATUS_OK)
                {
                    status = vigil_array_object_new(vm->runtime, items, item_count, &array_object, error);
                }
                if (status != VIGIL_STATUS_OK)
                {
                    size_t item_index;

                    for (item_index = 0U; item_index < item_count; item_index += 1U)
                    {
                        vigil_value_release(&items[item_index]);
                    }
                    vigil_runtime_free(vm->runtime, (void **)&items);
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    goto cleanup;
                }
                for (match_index = 0U; match_index < item_count; match_index += 1U)
                {
                    vigil_value_release(&items[match_index]);
                }
                vigil_runtime_free(vm->runtime, (void **)&items);
                vigil_value_init_object(&value, &array_object);
                vigil_object_release(&array_object);
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(STRING_INDEX_OF)
            frame->ip += 1U;
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            {
                const char *text;
                const char *needle;
                size_t text_length;
                size_t needle_length;
                size_t index;
                int found;

                if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
                    !vigil_vm_get_string_parts(&right, &needle, &needle_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string method arguments must be strings", error);
                    goto cleanup;
                }

                found = vigil_vm_find_substring(text, text_length, needle, needle_length, &index);
                vigil_value_init_int(&value, found ? (int64_t)index : -1);
                VIGIL_VM_VALUE_RELEASE(&right);
                do
                {
                    (right) = vigil_nanbox_from_bool(found);
                } while (0);
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status == VIGIL_STATUS_OK)
            {
                status = vigil_vm_push(vm, &right, error);
            }
            VIGIL_VM_VALUE_RELEASE(&right);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(STRING_SUBSTR)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            {
                const char *text;
                size_t text_length;
                int64_t start;
                int64_t slice_length;

                if (!vigil_vm_get_string_parts(&left, &text, &text_length) || !vigil_nanbox_is_int(right) ||
                    !vigil_nanbox_is_int(value))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    VIGIL_VM_VALUE_RELEASE(&value);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string substr() requires i32 start and length", error);
                    goto cleanup;
                }
                start = vigil_value_as_int(&(right));
                slice_length = vigil_value_as_int(&(value));
                if (start < 0 || slice_length < 0 || (uint64_t)start > text_length ||
                    (uint64_t)slice_length > text_length - (size_t)start)
                {
                    status = vigil_vm_new_string_value(vm, "", 0U, &right, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        status = vigil_vm_make_error_value(vm, 7, "string slice is out of range",
                                                           sizeof("string slice is out of range") - 1U, &value, error);
                    }
                }
                else
                {
                    status = vigil_vm_new_string_value(vm, text + (size_t)start, (size_t)slice_length, &right, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        status = vigil_vm_make_ok_error_value(vm, &value, error);
                    }
                }
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    VIGIL_VM_VALUE_RELEASE(&value);
                    goto cleanup;
                }
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            status = vigil_vm_push(vm, &right, error);
            VIGIL_VM_VALUE_RELEASE(&right);
            if (status == VIGIL_STATUS_OK)
            {
                status = vigil_vm_push(vm, &value, error);
            }
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(STRING_BYTES)
            frame->ip += 1U;
            left = vigil_vm_pop_or_nil(vm);
            {
                const char *text;
                size_t text_length;
                vigil_value_t *items = NULL;
                size_t item_count = 0U;
                size_t item_capacity = 0U;
                size_t index;
                vigil_object_t *array_object = NULL;

                if (!vigil_vm_get_string_parts(&left, &text, &text_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string bytes() requires a string receiver", error);
                    goto cleanup;
                }
                status = vigil_vm_grow_value_array(vm->runtime, &items, &item_capacity, text_length, error);
                for (index = 0U; status == VIGIL_STATUS_OK && index < text_length; index += 1U)
                {
                    vigil_value_init_uint(&items[index], (uint64_t)(unsigned char)text[index]);
                }
                item_count = status == VIGIL_STATUS_OK ? text_length : 0U;
                if (status == VIGIL_STATUS_OK)
                {
                    status = vigil_array_object_new(vm->runtime, items, item_count, &array_object, error);
                }
                for (index = 0U; index < item_count; index += 1U)
                {
                    vigil_value_release(&items[index]);
                }
                vigil_runtime_free(vm->runtime, (void **)&items);
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    goto cleanup;
                }
                vigil_value_init_object(&value, &array_object);
                vigil_object_release(&array_object);
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(STRING_CHAR_AT)
            frame->ip += 1U;
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            VIGIL_VM_VALUE_INIT_NIL(&value);
            {
                const char *text;
                size_t text_length;
                int64_t index;

                if (!vigil_vm_get_string_parts(&left, &text, &text_length) || !vigil_nanbox_is_int(right))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string char_at() requires an i32 index", error);
                    goto cleanup;
                }
                index = vigil_value_as_int(&(right));
                if (index < 0 || (uint64_t)index >= text_length)
                {
                    status = vigil_vm_new_string_value(vm, "", 0U, &right, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        status = vigil_vm_make_error_value(vm, 7, "string index is out of range",
                                                           sizeof("string index is out of range") - 1U, &value, error);
                    }
                }
                else
                {
                    status = vigil_vm_new_string_value(vm, text + (size_t)index, 1U, &right, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        status = vigil_vm_make_ok_error_value(vm, &value, error);
                    }
                }
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    goto cleanup;
                }
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            status = vigil_vm_push(vm, &right, error);
            VIGIL_VM_VALUE_RELEASE(&right);
            if (status == VIGIL_STATUS_OK)
            {
                status = vigil_vm_push(vm, &value, error);
            }
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();

            /* ── New string methods ──────────────────────────────── */

            VM_CASE(STRING_TRIM_LEFT)
            VM_CASE(STRING_TRIM_RIGHT)
            {
                vigil_opcode_t string_opcode = (vigil_opcode_t)code[frame->ip];
                const char *text;
                size_t length;

                frame->ip += 1U;
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string method requires a string receiver", error);
                    goto cleanup;
                }

                if (string_opcode == VIGIL_OPCODE_STRING_TRIM_LEFT)
                {
                    size_t start = 0U;
                    while (start < length && isspace((unsigned char)text[start]))
                    {
                        start += 1U;
                    }
                    status = vigil_vm_new_string_value(vm, text + start, length - start, &value, error);
                }
                else
                {
                    size_t end = length;
                    while (end > 0U && isspace((unsigned char)text[end - 1U]))
                    {
                        end -= 1U;
                    }
                    status = vigil_vm_new_string_value(vm, text, end, &value, error);
                }
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    goto cleanup;
                }
                VIGIL_VM_VALUE_RELEASE(&left);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }

            VM_CASE(STRING_REVERSE)
            {
                const char *text;
                size_t length;

                frame->ip += 1U;
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string method requires a string receiver", error);
                    goto cleanup;
                }

                {
                    void *memory = NULL;
                    char *buffer;
                    size_t i;

                    status = vigil_runtime_alloc(vm->runtime, length + 1U, &memory, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        buffer = (char *)memory;
                        for (i = 0U; i < length; i += 1U)
                        {
                            buffer[i] = text[length - 1U - i];
                        }
                        buffer[length] = '\0';
                        status = vigil_vm_new_string_value(vm, buffer, length, &value, error);
                        vigil_runtime_free(vm->runtime, &memory);
                    }
                }
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    goto cleanup;
                }
                VIGIL_VM_VALUE_RELEASE(&left);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }

            VM_CASE(STRING_IS_EMPTY)
            {
                const char *text;
                size_t length;

                frame->ip += 1U;
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string method requires a string receiver", error);
                    goto cleanup;
                }

                VIGIL_VM_VALUE_RELEASE(&left);
                vigil_value_init_bool(&value, length == 0U);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }

            VM_CASE(STRING_CHAR_COUNT)
            {
                const char *text;
                size_t text_length;
                size_t ci;
                int32_t ccount;

                frame->ip += 1U;
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &text_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "char_count() requires a string receiver", error);
                    goto cleanup;
                }

                ccount = 0;
                for (ci = 0U; ci < text_length;)
                {
                    unsigned char uc = (unsigned char)text[ci];
                    if (uc < 0x80U)
                    {
                        ci += 1U;
                    }
                    else if ((uc & 0xE0U) == 0xC0U)
                    {
                        ci += 2U;
                    }
                    else if ((uc & 0xF0U) == 0xE0U)
                    {
                        ci += 3U;
                    }
                    else
                    {
                        ci += 4U;
                    }
                    ccount += 1;
                }

                VIGIL_VM_VALUE_RELEASE(&left);
                vigil_value_init_int(&value, (int64_t)ccount);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }

            VM_CASE(STRING_REPEAT)
            {
                const char *text;
                size_t length;
                int64_t count;

                frame->ip += 1U;
                right = vigil_vm_pop_or_nil(vm);
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &length) || !vigil_nanbox_is_int(right))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string repeat() requires an i32 count", error);
                    goto cleanup;
                }

                count = vigil_value_as_int(&right);
                VIGIL_VM_VALUE_RELEASE(&right);

                if (count <= 0)
                {
                    status = vigil_vm_new_string_value(vm, "", 0U, &value, error);
                }
                else
                {
                    size_t total = length * (size_t)count;
                    void *memory = NULL;
                    char *buffer;
                    int64_t i;

                    status = vigil_runtime_alloc(vm->runtime, total + 1U, &memory, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        buffer = (char *)memory;
                        for (i = 0; i < count; i += 1)
                        {
                            memcpy(buffer + (size_t)i * length, text, length);
                        }
                        buffer[total] = '\0';
                        status = vigil_vm_new_string_value(vm, buffer, total, &value, error);
                        vigil_runtime_free(vm->runtime, &memory);
                    }
                }
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    goto cleanup;
                }
                VIGIL_VM_VALUE_RELEASE(&left);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }

            VM_CASE(STRING_COUNT)
            {
                const char *text;
                size_t text_length;
                const char *needle;
                size_t needle_length;

                frame->ip += 1U;
                right = vigil_vm_pop_or_nil(vm);
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
                    !vigil_vm_get_string_parts(&right, &needle, &needle_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string count() requires a string argument", error);
                    goto cleanup;
                }

                {
                    int64_t n = 0;
                    if (needle_length > 0U && needle_length <= text_length)
                    {
                        const char *p = text;
                        const char *end = text + text_length;
                        while (p <= end - needle_length)
                        {
                            if (memcmp(p, needle, needle_length) == 0)
                            {
                                n += 1;
                                p += needle_length;
                            }
                            else
                            {
                                p += 1;
                            }
                        }
                    }
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    vigil_value_init_int(&value, n);
                    status = vigil_vm_push(vm, &value, error);
                    VIGIL_VM_VALUE_RELEASE(&value);
                    if (status != VIGIL_STATUS_OK)
                    {
                        goto cleanup;
                    }
                }
                VM_BREAK();
            }

            VM_CASE(STRING_LAST_INDEX_OF)
            {
                const char *text;
                size_t text_length;
                const char *needle;
                size_t needle_length;

                frame->ip += 1U;
                right = vigil_vm_pop_or_nil(vm);
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
                    !vigil_vm_get_string_parts(&right, &needle, &needle_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string last_index_of() requires a string argument", error);
                    goto cleanup;
                }

                {
                    int64_t found_index = -1;
                    if (needle_length > 0U && needle_length <= text_length)
                    {
                        size_t i = text_length - needle_length;
                        for (;;)
                        {
                            if (memcmp(text + i, needle, needle_length) == 0)
                            {
                                found_index = (int64_t)i;
                                break;
                            }
                            if (i == 0U)
                                break;
                            i -= 1U;
                        }
                    }
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    vigil_value_init_int(&value, found_index >= 0 ? found_index : 0);
                    status = vigil_vm_push(vm, &value, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        vigil_value_t found_val;
                        vigil_value_init_bool(&found_val, found_index >= 0);
                        status = vigil_vm_push(vm, &found_val, error);
                        VIGIL_VM_VALUE_RELEASE(&found_val);
                    }
                    VIGIL_VM_VALUE_RELEASE(&value);
                    if (status != VIGIL_STATUS_OK)
                    {
                        goto cleanup;
                    }
                }
                VM_BREAK();
            }

            VM_CASE(STRING_TRIM_PREFIX)
            VM_CASE(STRING_TRIM_SUFFIX)
            {
                vigil_opcode_t string_opcode = (vigil_opcode_t)code[frame->ip];
                const char *text;
                size_t text_length;
                const char *prefix;
                size_t prefix_length;

                frame->ip += 1U;
                right = vigil_vm_pop_or_nil(vm);
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &text_length) ||
                    !vigil_vm_get_string_parts(&right, &prefix, &prefix_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "string method requires a string argument", error);
                    goto cleanup;
                }

                if (string_opcode == VIGIL_OPCODE_STRING_TRIM_PREFIX)
                {
                    if (prefix_length <= text_length && memcmp(text, prefix, prefix_length) == 0)
                    {
                        status = vigil_vm_new_string_value(vm, text + prefix_length, text_length - prefix_length,
                                                           &value, error);
                    }
                    else
                    {
                        status = vigil_vm_new_string_value(vm, text, text_length, &value, error);
                    }
                }
                else
                {
                    if (prefix_length <= text_length &&
                        memcmp(text + text_length - prefix_length, prefix, prefix_length) == 0)
                    {
                        status = vigil_vm_new_string_value(vm, text, text_length - prefix_length, &value, error);
                    }
                    else
                    {
                        status = vigil_vm_new_string_value(vm, text, text_length, &value, error);
                    }
                }
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    goto cleanup;
                }
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                VM_BREAK();
            }

            VM_CASE(CHAR_FROM_INT)
            {
                frame->ip += 1U;
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_nanbox_is_int(left))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "char() requires an integer argument", error);
                    goto cleanup;
                }

                int32_t code_point = vigil_nanbox_decode_i32(left);
                VIGIL_VM_VALUE_RELEASE(&left);

                if (code_point < 0 || code_point > 255)
                {
                    status =
                        vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "char() argument must be 0-255", error);
                    goto cleanup;
                }

                char ch = (char)code_point;
                status = vigil_vm_new_string_value(vm, &ch, 1, &value, error);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                VM_BREAK();
            }

            VM_CASE(STRING_TO_C)
            {
                frame->ip += 1U;
                left = vigil_vm_pop_or_nil(vm);

                const char *text;
                size_t text_length;
                if (!vigil_vm_get_string_parts(&left, &text, &text_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "to_c() requires a string", error);
                    goto cleanup;
                }

                /* Build C-style escaped string */
                size_t out_cap = text_length * 4 + 3; /* worst case: all \xNN + quotes + null */
                char *out_buf = NULL;
                status = vigil_runtime_alloc(vm->runtime, out_cap, (void **)&out_buf, error);
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    goto cleanup;
                }

                size_t j = 0;
                out_buf[j++] = '"';
                for (size_t i = 0; i < text_length; i++)
                {
                    unsigned char c = (unsigned char)text[i];
                    if (c == '"')
                    {
                        out_buf[j++] = '\\';
                        out_buf[j++] = '"';
                    }
                    else if (c == '\\')
                    {
                        out_buf[j++] = '\\';
                        out_buf[j++] = '\\';
                    }
                    else if (c == '\n')
                    {
                        out_buf[j++] = '\\';
                        out_buf[j++] = 'n';
                    }
                    else if (c == '\r')
                    {
                        out_buf[j++] = '\\';
                        out_buf[j++] = 'r';
                    }
                    else if (c == '\t')
                    {
                        out_buf[j++] = '\\';
                        out_buf[j++] = 't';
                    }
                    else if (c >= 32 && c < 127)
                    {
                        out_buf[j++] = (char)c;
                    }
                    else
                    {
                        out_buf[j++] = '\\';
                        out_buf[j++] = 'x';
                        out_buf[j++] = "0123456789abcdef"[c >> 4];
                        out_buf[j++] = "0123456789abcdef"[c & 0xf];
                    }
                }
                out_buf[j++] = '"';

                VIGIL_VM_VALUE_RELEASE(&left);
                status = vigil_vm_new_string_value(vm, out_buf, j, &value, error);
                vigil_runtime_free(vm->runtime, (void **)&out_buf);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                VM_BREAK();
            }

            VM_CASE(STRING_FIELDS)
            {
                const char *text;
                size_t text_length;
                vigil_value_t *items = NULL;
                size_t item_count = 0U;
                size_t item_capacity = 0U;
                vigil_object_t *array_object = NULL;

                frame->ip += 1U;
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &text_length))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    status =
                        vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "fields() requires a string", error);
                    goto cleanup;
                }

                status = VIGIL_STATUS_OK;
                size_t i = 0;
                while (i < text_length && status == VIGIL_STATUS_OK)
                {
                    while (i < text_length && isspace((unsigned char)text[i]))
                        i++;
                    if (i >= text_length)
                        break;
                    size_t start = i;
                    while (i < text_length && !isspace((unsigned char)text[i]))
                        i++;
                    status = vigil_vm_grow_value_array(vm->runtime, &items, &item_capacity, item_count + 1U, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        VIGIL_VM_VALUE_INIT_NIL(&items[item_count]);
                        status = vigil_vm_new_string_value(vm, text + start, i - start, &items[item_count], error);
                        if (status == VIGIL_STATUS_OK)
                            item_count++;
                    }
                }

                if (status == VIGIL_STATUS_OK)
                {
                    status = vigil_array_object_new(vm->runtime, items, item_count, &array_object, error);
                }
                if (status != VIGIL_STATUS_OK)
                {
                    for (size_t idx = 0; idx < item_count; idx++)
                        vigil_value_release(&items[idx]);
                    vigil_runtime_free(vm->runtime, (void **)&items);
                    VIGIL_VM_VALUE_RELEASE(&left);
                    goto cleanup;
                }
                for (size_t idx = 0; idx < item_count; idx++)
                    vigil_value_release(&items[idx]);
                vigil_runtime_free(vm->runtime, (void **)&items);
                VIGIL_VM_VALUE_RELEASE(&left);
                vigil_value_init_object(&value, &array_object);
                vigil_object_release(&array_object);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                VM_BREAK();
            }

            VM_CASE(STRING_EQUAL_FOLD)
            {
                const char *text1, *text2;
                size_t len1, len2;

                frame->ip += 1U;
                right = vigil_vm_pop_or_nil(vm);
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text1, &len1) ||
                    !vigil_vm_get_string_parts(&right, &text2, &len2))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "equal_fold() requires string arguments", error);
                    goto cleanup;
                }

                int equal = 0;
                if (len1 == len2)
                {
                    equal = 1;
                    for (size_t i = 0; i < len1; i++)
                    {
                        if (tolower((unsigned char)text1[i]) != tolower((unsigned char)text2[i]))
                        {
                            equal = 0;
                            break;
                        }
                    }
                }
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                do
                {
                    (value) = vigil_nanbox_from_bool(equal);
                } while (0);
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                VM_BREAK();
            }

            VM_CASE(STRING_CUT)
            {
                const char *text, *sep;
                size_t text_len, sep_len;
                size_t match_idx;

                frame->ip += 1U;
                right = vigil_vm_pop_or_nil(vm);
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &text, &text_len) ||
                    !vigil_vm_get_string_parts(&right, &sep, &sep_len))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "cut() requires string arguments",
                                                 error);
                    goto cleanup;
                }

                vigil_value_t before, after, found_val;
                VIGIL_VM_VALUE_INIT_NIL(&before);
                VIGIL_VM_VALUE_INIT_NIL(&after);

                int found = vigil_vm_find_substring(text, text_len, sep, sep_len, &match_idx);
                if (found)
                {
                    status = vigil_vm_new_string_value(vm, text, match_idx, &before, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        status = vigil_vm_new_string_value(vm, text + match_idx + sep_len,
                                                           text_len - match_idx - sep_len, &after, error);
                    }
                }
                else
                {
                    status = vigil_vm_new_string_value(vm, text, text_len, &before, error);
                    if (status == VIGIL_STATUS_OK)
                    {
                        status = vigil_vm_new_string_value(vm, "", 0, &after, error);
                    }
                }
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&before);
                    VIGIL_VM_VALUE_RELEASE(&after);
                    goto cleanup;
                }
                do
                {
                    (found_val) = vigil_nanbox_from_bool(found);
                } while (0);
                status = vigil_vm_push(vm, &before, error);
                VIGIL_VM_VALUE_RELEASE(&before);
                if (status == VIGIL_STATUS_OK)
                    status = vigil_vm_push(vm, &after, error);
                VIGIL_VM_VALUE_RELEASE(&after);
                if (status == VIGIL_STATUS_OK)
                    status = vigil_vm_push(vm, &found_val, error);
                VIGIL_VM_VALUE_RELEASE(&found_val);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                VM_BREAK();
            }

            VM_CASE(STRING_JOIN)
            {
                const char *sep;
                size_t sep_len;
                vigil_object_t *arr;

                frame->ip += 1U;
                right = vigil_vm_pop_or_nil(vm);
                left = vigil_vm_pop_or_nil(vm);

                if (!vigil_vm_get_string_parts(&left, &sep, &sep_len))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "join() requires a string separator", error);
                    goto cleanup;
                }
                if (!vigil_nanbox_is_object(right) ||
                    (arr = (vigil_object_t *)vigil_nanbox_decode_ptr(right)) == NULL ||
                    vigil_object_type(arr) != VIGIL_OBJECT_ARRAY)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "join() requires an array<string> argument", error);
                    goto cleanup;
                }

                size_t arr_len = vigil_array_object_length(arr);
                vigil_string_t built;
                vigil_string_init(&built, vm->runtime);

                for (size_t i = 0; i < arr_len && status == VIGIL_STATUS_OK; i++)
                {
                    vigil_value_t elem;
                    const char *elem_text;
                    size_t elem_len;
                    if (!vigil_array_object_get(arr, i, &elem) ||
                        !vigil_vm_get_string_parts(&elem, &elem_text, &elem_len))
                    {
                        vigil_value_release(&elem);
                        status = VIGIL_STATUS_INVALID_ARGUMENT;
                        break;
                    }
                    if (i > 0)
                        status = vigil_string_append(&built, sep, sep_len, error);
                    if (status == VIGIL_STATUS_OK)
                        status = vigil_string_append(&built, elem_text, elem_len, error);
                    vigil_value_release(&elem);
                }

                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                if (status != VIGIL_STATUS_OK)
                {
                    vigil_string_free(&built);
                    goto cleanup;
                }
                status = vigil_vm_new_string_value(vm, vigil_string_c_str(&built), vigil_string_length(&built), &value,
                                                   error);
                vigil_string_free(&built);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                    goto cleanup;
                VM_BREAK();
            }

            VM_CASE(GET_MAP_KEY_AT)
            frame->ip += 1U;
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            VIGIL_VM_VALUE_INIT_NIL(&value);

            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_MAP)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration requires a map object",
                                             error);
                goto cleanup;
            }
            if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&(right)) < 0)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "map iteration index must be a non-negative i32", error);
                goto cleanup;
            }
            if (!vigil_map_object_key_at(((vigil_object_t *)vigil_nanbox_decode_ptr(left)),
                                         (size_t)vigil_value_as_int(&(right)), &value))
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration index is out of range",
                                             error);
                goto cleanup;
            }

            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(GET_MAP_VALUE_AT)
            frame->ip += 1U;
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            VIGIL_VM_VALUE_INIT_NIL(&value);

            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_MAP)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration requires a map object",
                                             error);
                goto cleanup;
            }
            if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&(right)) < 0)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "map iteration index must be a non-negative i32", error);
                goto cleanup;
            }
            if (!vigil_map_object_value_at(((vigil_object_t *)vigil_nanbox_decode_ptr(left)),
                                           (size_t)vigil_value_as_int(&(right)), &value))
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "map iteration index is out of range",
                                             error);
                goto cleanup;
            }

            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            status = vigil_vm_push(vm, &value, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(SET_INDEX)
            frame->ip += 1U;
            value = vigil_vm_pop_or_nil(vm);
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);

            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "indexed assignment requires an array or map", error);
                goto cleanup;
            }

            if (vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) == VIGIL_OBJECT_ARRAY)
            {
                if (!vigil_nanbox_is_int(right) || vigil_value_as_int(&(right)) < 0)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    VIGIL_VM_VALUE_RELEASE(&value);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "array index must be a non-negative i32", error);
                    goto cleanup;
                }

                status = vigil_array_object_set(((vigil_object_t *)vigil_nanbox_decode_ptr(left)),
                                                (size_t)vigil_value_as_int(&(right)), &value, error);
            }
            else if (vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) == VIGIL_OBJECT_MAP)
            {
                if (!vigil_vm_value_is_supported_map_key(&right))
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    VIGIL_VM_VALUE_RELEASE(&right);
                    VIGIL_VM_VALUE_RELEASE(&value);
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "map index must be i32, bool, or string", error);
                    goto cleanup;
                }

                status = vigil_map_object_set(((vigil_object_t *)vigil_nanbox_decode_ptr(left)), &right, &value, error);
            }
            else
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "indexed assignment requires an array or map", error);
                goto cleanup;
            }

            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(JUMP)
            VIGIL_VM_READ_U32(code, frame->ip, operand);
            frame->ip += (size_t)operand;
            VM_BREAK();
            VM_CASE(JUMP_IF_FALSE)
            VIGIL_VM_READ_U32(code, frame->ip, operand);
            if (vm->stack_count > 0U && vigil_nanbox_is_bool(vm->stack[vm->stack_count - 1U]))
            {
                if (!vigil_nanbox_decode_bool(vm->stack[vm->stack_count - 1U]))
                {
                    /* Condition false — jump.  The POP after us is
                       inside the true-path, so we skip past it. */
                    frame->ip += (size_t)operand;
                }
                else
                {
                    /* Condition true — fall through.  Fuse with the
                       following POP if present. */
                    if (frame->ip < code_size && code[frame->ip] == VIGIL_OPCODE_POP)
                    {
                        vm->stack_count -= 1U;
                        frame->ip += 1U;
                    }
                }
            }
            else
            {
                status =
                    vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "condition must evaluate to bool", error);
                goto cleanup;
            }
            VM_BREAK();
            VM_CASE(LOOP)
            VIGIL_VM_READ_U32(code, frame->ip, operand);
            frame->ip -= (size_t)operand;
            VM_BREAK();
            VM_CASE(NIL)
            VIGIL_VM_VALUE_INIT_NIL(&value);
            status = vigil_vm_push(vm, &value, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(TRUE)
            do
            {
                (value) = vigil_nanbox_from_bool(1);
            } while (0);
            status = vigil_vm_push(vm, &value, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(FALSE)
            do
            {
                (value) = vigil_nanbox_from_bool(0);
            } while (0);
            status = vigil_vm_push(vm, &value, error);
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(ADD)
            VM_CASE(SUBTRACT)
            VM_CASE(MULTIPLY)
            VM_CASE(DIVIDE)
            VM_CASE(MODULO)
            VM_CASE(BITWISE_AND)
            VM_CASE(BITWISE_OR)
            VM_CASE(BITWISE_XOR)
            VM_CASE(SHIFT_LEFT)
            VM_CASE(SHIFT_RIGHT)
            VM_CASE(GREATER)
            VM_CASE(LESS)
            VM_CASE(EQUAL)
            VIGIL_VM_POP(vm, right);
            VIGIL_VM_POP(vm, left);

            if ((vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_EQUAL)
            {
                vigil_value_init_bool(&value, vigil_vm_values_equal(&left, &right));
            }
            else
            {
                if ((vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_ADD && vigil_nanbox_is_object(left) &&
                    vigil_nanbox_is_object(right) && ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) != NULL &&
                    ((vigil_object_t *)vigil_nanbox_decode_ptr(right)) != NULL &&
                    vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) == VIGIL_OBJECT_STRING &&
                    vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(right))) == VIGIL_OBJECT_STRING)
                {
                    status = vigil_vm_concat_strings(vm, &left, &right, &value, error);
                    if (status != VIGIL_STATUS_OK)
                    {
                        VIGIL_VM_VALUE_RELEASE(&left);
                        VIGIL_VM_VALUE_RELEASE(&right);
                        goto cleanup;
                    }
                }
                else if (((vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_GREATER ||
                          (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_LESS) &&
                         vigil_nanbox_is_object(left) && vigil_nanbox_is_object(right) &&
                         ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) != NULL &&
                         ((vigil_object_t *)vigil_nanbox_decode_ptr(right)) != NULL &&
                         vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) == VIGIL_OBJECT_STRING &&
                         vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(right))) == VIGIL_OBJECT_STRING)
                {
                    vigil_object_t *ls = (vigil_object_t *)vigil_nanbox_decode_ptr(left);
                    vigil_object_t *rs = (vigil_object_t *)vigil_nanbox_decode_ptr(right);
                    const char *lp = vigil_string_object_c_str(ls);
                    const char *rp = vigil_string_object_c_str(rs);
                    int cmp = strcmp(lp, rp);
                    if ((vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_GREATER)
                    {
                        vigil_value_init_bool(&value, cmp > 0);
                    }
                    else
                    {
                        vigil_value_init_bool(&value, cmp < 0);
                    }
                }
                else if (vigil_nanbox_is_double(left) && vigil_nanbox_is_double(right))
                {
                    switch ((vigil_opcode_t)code[frame->ip])
                    {
                    case VIGIL_OPCODE_ADD:
                        vigil_value_init_float(&value,
                                               vigil_nanbox_decode_double(left) + vigil_nanbox_decode_double(right));
                        break;
                    case VIGIL_OPCODE_SUBTRACT:
                        vigil_value_init_float(&value,
                                               vigil_nanbox_decode_double(left) - vigil_nanbox_decode_double(right));
                        break;
                    case VIGIL_OPCODE_MULTIPLY:
                        vigil_value_init_float(&value,
                                               vigil_nanbox_decode_double(left) * vigil_nanbox_decode_double(right));
                        break;
                    case VIGIL_OPCODE_DIVIDE:
                        vigil_value_init_float(&value,
                                               vigil_nanbox_decode_double(left) / vigil_nanbox_decode_double(right));
                        break;
                    case VIGIL_OPCODE_GREATER:
                        vigil_value_init_bool(&value,
                                              vigil_nanbox_decode_double(left) > vigil_nanbox_decode_double(right));
                        break;
                    case VIGIL_OPCODE_LESS:
                        vigil_value_init_bool(&value,
                                              vigil_nanbox_decode_double(left) < vigil_nanbox_decode_double(right));
                        break;
                    default:
                        VIGIL_VM_VALUE_RELEASE(&left);
                        VIGIL_VM_VALUE_RELEASE(&right);
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                     "float operands are not supported for this opcode", error);
                        goto cleanup;
                    }
                }
                else
                {
                    if (!vigil_vm_value_is_integer(&left) || !vigil_vm_value_is_integer(&right) ||
                        vigil_value_kind(&(left)) != vigil_value_kind(&(right)))
                    {
                        VIGIL_VM_VALUE_RELEASE(&left);
                        VIGIL_VM_VALUE_RELEASE(&right);
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer operands are required",
                                                     error);
                        goto cleanup;
                    }

                    if (vigil_nanbox_is_uint(left))
                    {
                        switch ((vigil_opcode_t)code[frame->ip])
                        {
                        case VIGIL_OPCODE_ADD:
                            status = vigil_vm_checked_uadd(vigil_value_as_uint(&(left)), vigil_value_as_uint(&(right)),
                                                           &uinteger_result);
                            break;
                        case VIGIL_OPCODE_SUBTRACT:
                            status = vigil_vm_checked_usubtract(vigil_value_as_uint(&(left)),
                                                                vigil_value_as_uint(&(right)), &uinteger_result);
                            break;
                        case VIGIL_OPCODE_MULTIPLY:
                            status = vigil_vm_checked_umultiply(vigil_value_as_uint(&(left)),
                                                                vigil_value_as_uint(&(right)), &uinteger_result);
                            break;
                        case VIGIL_OPCODE_DIVIDE:
                            status = vigil_vm_checked_udivide(vigil_value_as_uint(&(left)),
                                                              vigil_value_as_uint(&(right)), &uinteger_result);
                            break;
                        case VIGIL_OPCODE_MODULO:
                            status = vigil_vm_checked_umodulo(vigil_value_as_uint(&(left)),
                                                              vigil_value_as_uint(&(right)), &uinteger_result);
                            break;
                        case VIGIL_OPCODE_BITWISE_AND:
                            status = VIGIL_STATUS_OK;
                            uinteger_result = vigil_value_as_uint(&(left)) & vigil_value_as_uint(&(right));
                            break;
                        case VIGIL_OPCODE_BITWISE_OR:
                            status = VIGIL_STATUS_OK;
                            uinteger_result = vigil_value_as_uint(&(left)) | vigil_value_as_uint(&(right));
                            break;
                        case VIGIL_OPCODE_BITWISE_XOR:
                            status = VIGIL_STATUS_OK;
                            uinteger_result = vigil_value_as_uint(&(left)) ^ vigil_value_as_uint(&(right));
                            break;
                        case VIGIL_OPCODE_SHIFT_LEFT:
                            status = vigil_vm_checked_ushift_left(vigil_value_as_uint(&(left)),
                                                                  vigil_value_as_uint(&(right)), &uinteger_result);
                            break;
                        case VIGIL_OPCODE_SHIFT_RIGHT:
                            status = vigil_vm_checked_ushift_right(vigil_value_as_uint(&(left)),
                                                                   vigil_value_as_uint(&(right)), &uinteger_result);
                            break;
                        case VIGIL_OPCODE_GREATER:
                            status = VIGIL_STATUS_OK;
                            vigil_value_init_bool(&value, vigil_value_as_uint(&(left)) > vigil_value_as_uint(&(right)));
                            break;
                        case VIGIL_OPCODE_LESS:
                            status = VIGIL_STATUS_OK;
                            vigil_value_init_bool(&value, vigil_value_as_uint(&(left)) < vigil_value_as_uint(&(right)));
                            break;
                        default:
                            VIGIL_VM_VALUE_INIT_NIL(&value);
                            break;
                        }
                    }
                    else
                    {
                        switch ((vigil_opcode_t)code[frame->ip])
                        {
                        case VIGIL_OPCODE_ADD:
                            status = vigil_vm_checked_add(vigil_value_as_int(&(left)), vigil_value_as_int(&(right)),
                                                          &integer_result);
                            break;
                        case VIGIL_OPCODE_SUBTRACT:
                            status = vigil_vm_checked_subtract(vigil_value_as_int(&(left)),
                                                               vigil_value_as_int(&(right)), &integer_result);
                            break;
                        case VIGIL_OPCODE_MULTIPLY:
                            status = vigil_vm_checked_multiply(vigil_value_as_int(&(left)),
                                                               vigil_value_as_int(&(right)), &integer_result);
                            break;
                        case VIGIL_OPCODE_DIVIDE:
                            status = vigil_vm_checked_divide(vigil_value_as_int(&(left)), vigil_value_as_int(&(right)),
                                                             &integer_result);
                            break;
                        case VIGIL_OPCODE_MODULO:
                            status = vigil_vm_checked_modulo(vigil_value_as_int(&(left)), vigil_value_as_int(&(right)),
                                                             &integer_result);
                            break;
                        case VIGIL_OPCODE_BITWISE_AND:
                            status = VIGIL_STATUS_OK;
                            integer_result = vigil_value_as_int(&(left)) & vigil_value_as_int(&(right));
                            break;
                        case VIGIL_OPCODE_BITWISE_OR:
                            status = VIGIL_STATUS_OK;
                            integer_result = vigil_value_as_int(&(left)) | vigil_value_as_int(&(right));
                            break;
                        case VIGIL_OPCODE_BITWISE_XOR:
                            status = VIGIL_STATUS_OK;
                            integer_result = vigil_value_as_int(&(left)) ^ vigil_value_as_int(&(right));
                            break;
                        case VIGIL_OPCODE_SHIFT_LEFT:
                            status = vigil_vm_checked_shift_left(vigil_value_as_int(&(left)),
                                                                 vigil_value_as_int(&(right)), &integer_result);
                            break;
                        case VIGIL_OPCODE_SHIFT_RIGHT:
                            status = vigil_vm_checked_shift_right(vigil_value_as_int(&(left)),
                                                                  vigil_value_as_int(&(right)), &integer_result);
                            break;
                        case VIGIL_OPCODE_GREATER:
                            status = VIGIL_STATUS_OK;
                            vigil_value_init_bool(&value, vigil_value_as_int(&(left)) > vigil_value_as_int(&(right)));
                            break;
                        case VIGIL_OPCODE_LESS:
                            status = VIGIL_STATUS_OK;
                            vigil_value_init_bool(&value, vigil_value_as_int(&(left)) < vigil_value_as_int(&(right)));
                            break;
                        default:
                            VIGIL_VM_VALUE_INIT_NIL(&value);
                            break;
                        }
                    }
                    if (status != VIGIL_STATUS_OK)
                    {
                        VIGIL_VM_VALUE_RELEASE(&left);
                        VIGIL_VM_VALUE_RELEASE(&right);
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                     "integer arithmetic overflow or invalid operation", error);
                        goto cleanup;
                    }
                    if ((vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_ADD ||
                        (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_SUBTRACT ||
                        (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_MULTIPLY ||
                        (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_DIVIDE ||
                        (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_MODULO ||
                        (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_BITWISE_AND ||
                        (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_BITWISE_OR ||
                        (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_BITWISE_XOR ||
                        (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_SHIFT_LEFT ||
                        (vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_SHIFT_RIGHT)
                    {
                        if (vigil_nanbox_is_uint(left))
                        {
                            vigil_value_init_uint(&value, uinteger_result);
                        }
                        else
                        {
                            vigil_value_init_int(&value, integer_result);
                        }
                    }
                }
            }

            VIGIL_VM_VALUE_RELEASE(&left);
            VIGIL_VM_VALUE_RELEASE(&right);
            VIGIL_VM_PUSH(vm, &value);
            VIGIL_VM_VALUE_RELEASE(&value);
            frame->ip += 1U;
            VM_BREAK();

            /* ── Specialized i64 arithmetic ────────────────────────
               No type dispatch, no overflow check wrappers — just
               inline integer ops.  The compiler only emits these
               when both operands are statically i32/i64.

               TO_I32 fusion: after computing the result, peek at the
               next opcode.  If it is TO_I32 (very common — the
               compiler emits it after every i32 arithmetic op), do
               the range check inline and skip the TO_I32 dispatch.
               This saves one full opcode dispatch per arithmetic op
               in i32-heavy code. */
            VM_CASE(ADD_I64)
            VM_CASE(SUBTRACT_I64)
            {
                int64_t a, b, r;
                vm->stack_count -= 1U;
                b = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
                vm->stack_count -= 1U;
                a = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
                if ((vigil_opcode_t)code[frame->ip] == VIGIL_OPCODE_ADD_I64)
                {
                    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
                        goto cleanup;
                    }
                    r = a + b;
                }
                else
                {
                    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
                        goto cleanup;
                    }
                    r = a - b;
                }
                /* kind set by nanbox_encode_int below */
                vm->stack[vm->stack_count] = vigil_nanbox_encode_int(r);
                vm->stack_count += 1U;
                frame->ip += 1U;
                /* TO_I32 fusion */
                if (frame->ip < code_size && code[frame->ip] == VIGIL_OPCODE_TO_I32)
                {
                    if (r < (int64_t)INT32_MIN || r > (int64_t)INT32_MAX)
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                     "i32 conversion overflow or invalid value", error);
                        goto cleanup;
                    }
                    frame->ip += 1U;
                }
                VM_BREAK();
            }
            VM_CASE(LESS_I64)
            VM_CASE(LESS_EQUAL_I64)
            VM_CASE(GREATER_I64)
            VM_CASE(GREATER_EQUAL_I64)
            VM_CASE(EQUAL_I64)
            VM_CASE(NOT_EQUAL_I64)
            {
                int64_t a, b;
                bool result;
                vm->stack_count -= 1U;
                b = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
                vm->stack_count -= 1U;
                a = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
                switch ((vigil_opcode_t)code[frame->ip])
                {
                case VIGIL_OPCODE_LESS_I64:
                    result = a < b;
                    break;
                case VIGIL_OPCODE_LESS_EQUAL_I64:
                    result = a <= b;
                    break;
                case VIGIL_OPCODE_GREATER_I64:
                    result = a > b;
                    break;
                case VIGIL_OPCODE_GREATER_EQUAL_I64:
                    result = a >= b;
                    break;
                case VIGIL_OPCODE_EQUAL_I64:
                    result = a == b;
                    break;
                case VIGIL_OPCODE_NOT_EQUAL_I64:
                    result = a != b;
                    break;
                default:
                    result = false;
                    break;
                }
                /* kind set by nanbox_from_bool below */
                vm->stack[vm->stack_count] = vigil_nanbox_from_bool(result);
                vm->stack_count += 1U;
                frame->ip += 1U;
                VM_BREAK();
            }
            VM_CASE(MULTIPLY_I64)
            VM_CASE(DIVIDE_I64)
            VM_CASE(MODULO_I64)
            {
                int64_t a, b, r;
                vm->stack_count -= 1U;
                b = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
                vm->stack_count -= 1U;
                a = vigil_nanbox_decode_int(vm->stack[vm->stack_count]);
                switch ((vigil_opcode_t)code[frame->ip])
                {
                case VIGIL_OPCODE_MULTIPLY_I64:
                    /* Overflow check for multiplication. */
                    if (a != 0 && b != 0 &&
                        ((a > 0 && b > 0 && a > INT64_MAX / b) || (a > 0 && b < 0 && b < INT64_MIN / a) ||
                         (a < 0 && b > 0 && a < INT64_MIN / b) || (a < 0 && b < 0 && a < INT64_MAX / b)))
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
                        goto cleanup;
                    }
                    r = a * b;
                    break;
                case VIGIL_OPCODE_DIVIDE_I64:
                    if (b == 0)
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "division by zero", error);
                        goto cleanup;
                    }
                    if (a == INT64_MIN && b == -1)
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
                        goto cleanup;
                    }
                    r = a / b;
                    break;
                default: /* MODULO_I64 */
                    if (b == 0)
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "division by zero", error);
                        goto cleanup;
                    }
                    r = a % b;
                    break;
                }
                /* kind set by nanbox_encode_int below */
                vm->stack[vm->stack_count] = vigil_nanbox_encode_int(r);
                vm->stack_count += 1U;
                frame->ip += 1U;
                /* TO_I32 fusion */
                if (frame->ip < code_size && code[frame->ip] == VIGIL_OPCODE_TO_I32)
                {
                    if (r < (int64_t)INT32_MIN || r > (int64_t)INT32_MAX)
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                     "i32 conversion overflow or invalid value", error);
                        goto cleanup;
                    }
                    frame->ip += 1U;
                }
                VM_BREAK();
            }

            /* ── Superinstructions: LOCALS_<op>_I64 ───────────────
               Fused GET_LOCAL + GET_LOCAL + <i64 op>.  Two u32
               operands encode the local slot indices.  Result is
               pushed directly — no intermediate stack traffic.
               Saves 2 dispatches per occurrence. */
            VM_CASE(LOCALS_ADD_I64)
            VM_CASE(LOCALS_SUBTRACT_I64)
            {
                uint32_t idx_a, idx_b;
                int64_t a, b, r;
                VIGIL_VM_READ_U32(code, frame->ip, idx_a);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_b);
                a = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_a]);
                b = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_b]);
                if ((vigil_opcode_t)code[frame->ip - 9U] == VIGIL_OPCODE_LOCALS_ADD_I64)
                {
                    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
                        goto cleanup;
                    }
                    r = a + b;
                }
                else
                {
                    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
                        goto cleanup;
                    }
                    r = a - b;
                }
                /* kind set by nanbox_encode_int below */
                vm->stack[vm->stack_count] = vigil_nanbox_encode_int(r);
                vm->stack_count += 1U;
                /* TO_I32 fusion */
                if (frame->ip < code_size && code[frame->ip] == VIGIL_OPCODE_TO_I32)
                {
                    if (r < (int64_t)INT32_MIN || r > (int64_t)INT32_MAX)
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                     "i32 conversion overflow or invalid value", error);
                        goto cleanup;
                    }
                    frame->ip += 1U;
                }
                VM_BREAK();
            }
            VM_CASE(LOCALS_MULTIPLY_I64)
            VM_CASE(LOCALS_MODULO_I64)
            {
                uint32_t idx_a, idx_b;
                int64_t a, b, r;
                VIGIL_VM_READ_U32(code, frame->ip, idx_a);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_b);
                a = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_a]);
                b = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_b]);
                if ((vigil_opcode_t)code[frame->ip - 9U] == VIGIL_OPCODE_LOCALS_MULTIPLY_I64)
                {
                    if (a != 0 && b != 0 &&
                        ((a > 0 && b > 0 && a > INT64_MAX / b) || (a > 0 && b < 0 && b < INT64_MIN / a) ||
                         (a < 0 && b > 0 && a < INT64_MIN / b) || (a < 0 && b < 0 && a < INT64_MAX / b)))
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "integer overflow", error);
                        goto cleanup;
                    }
                    r = a * b;
                }
                else
                {
                    if (b == 0)
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "division by zero", error);
                        goto cleanup;
                    }
                    r = a % b;
                }
                /* kind set by nanbox_encode_int below */
                vm->stack[vm->stack_count] = vigil_nanbox_encode_int(r);
                vm->stack_count += 1U;
                /* TO_I32 fusion */
                if (frame->ip < code_size && code[frame->ip] == VIGIL_OPCODE_TO_I32)
                {
                    if (r < (int64_t)INT32_MIN || r > (int64_t)INT32_MAX)
                    {
                        status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                     "i32 conversion overflow or invalid value", error);
                        goto cleanup;
                    }
                    frame->ip += 1U;
                }
                VM_BREAK();
            }
            VM_CASE(LOCALS_LESS_I64)
            VM_CASE(LOCALS_LESS_EQUAL_I64)
            VM_CASE(LOCALS_GREATER_I64)
            VM_CASE(LOCALS_GREATER_EQUAL_I64)
            VM_CASE(LOCALS_EQUAL_I64)
            VM_CASE(LOCALS_NOT_EQUAL_I64)
            {
                uint32_t idx_a, idx_b;
                int64_t a, b;
                bool result;
                uint8_t op;
                VIGIL_VM_READ_U32(code, frame->ip, idx_a);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_b);
                a = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_a]);
                b = vigil_nanbox_decode_int(vm->stack[frame->base_slot + idx_b]);
                op = code[frame->ip - 9U];
                switch ((vigil_opcode_t)op)
                {
                case VIGIL_OPCODE_LOCALS_LESS_I64:
                    result = a < b;
                    break;
                case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64:
                    result = a <= b;
                    break;
                case VIGIL_OPCODE_LOCALS_GREATER_I64:
                    result = a > b;
                    break;
                case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64:
                    result = a >= b;
                    break;
                case VIGIL_OPCODE_LOCALS_EQUAL_I64:
                    result = a == b;
                    break;
                case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64:
                    result = a != b;
                    break;
                default:
                    result = false;
                    break;
                }
                /* kind set by nanbox_from_bool below */
                vm->stack[vm->stack_count] = vigil_nanbox_from_bool(result);
                vm->stack_count += 1U;
                VM_BREAK();
            }
            /* ── i32-specific binary opcodes ──────────────────────────
               These skip the i64 overflow check entirely.  For i32
               arithmetic, overflow is checked with __builtin_*_overflow
               on 32-bit operands (single instruction on ARM/x86). */
            VM_CASE(ADD_I32)
            {
                int32_t a, b, r;
                vm->stack_count -= 1U;
                b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                vm->stack_count -= 1U;
                a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                if (VIGIL_I32_ADD_OVERFLOW(a, b, &r))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
                vm->stack_count += 1U;
                frame->ip += 1U;
                VM_BREAK();
            }
            VM_CASE(SUBTRACT_I32)
            {
                int32_t a, b, r;
                vm->stack_count -= 1U;
                b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                vm->stack_count -= 1U;
                a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                if (VIGIL_I32_SUB_OVERFLOW(a, b, &r))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
                vm->stack_count += 1U;
                frame->ip += 1U;
                VM_BREAK();
            }
            VM_CASE(MULTIPLY_I32)
            {
                int32_t a, b, r;
                vm->stack_count -= 1U;
                b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                vm->stack_count -= 1U;
                a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                if (VIGIL_I32_MUL_OVERFLOW(a, b, &r))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
                vm->stack_count += 1U;
                frame->ip += 1U;
                VM_BREAK();
            }
            VM_CASE(DIVIDE_I32)
            {
                int32_t a, b, r;
                vm->stack_count -= 1U;
                b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                vm->stack_count -= 1U;
                a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                if (b == 0 || (a == INT32_MIN && b == -1))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                r = a / b;
                vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
                vm->stack_count += 1U;
                frame->ip += 1U;
                VM_BREAK();
            }
            VM_CASE(MODULO_I32)
            {
                int32_t a, b, r;
                vm->stack_count -= 1U;
                b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                vm->stack_count -= 1U;
                a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                if (b == 0 || (a == INT32_MIN && b == -1))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                r = a % b;
                vm->stack[vm->stack_count] = vigil_nanbox_encode_i32(r);
                vm->stack_count += 1U;
                frame->ip += 1U;
                VM_BREAK();
            }
            VM_CASE(LESS_I32)
            VM_CASE(LESS_EQUAL_I32)
            VM_CASE(GREATER_I32)
            VM_CASE(GREATER_EQUAL_I32)
            VM_CASE(EQUAL_I32)
            VM_CASE(NOT_EQUAL_I32)
            {
                int32_t a, b;
                bool result;
                uint8_t op;
                vm->stack_count -= 1U;
                b = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                vm->stack_count -= 1U;
                a = vigil_nanbox_decode_i32(vm->stack[vm->stack_count]);
                op = code[frame->ip];
                switch ((vigil_opcode_t)op)
                {
                case VIGIL_OPCODE_LESS_I32:
                    result = a < b;
                    break;
                case VIGIL_OPCODE_LESS_EQUAL_I32:
                    result = a <= b;
                    break;
                case VIGIL_OPCODE_GREATER_I32:
                    result = a > b;
                    break;
                case VIGIL_OPCODE_GREATER_EQUAL_I32:
                    result = a >= b;
                    break;
                case VIGIL_OPCODE_EQUAL_I32:
                    result = a == b;
                    break;
                case VIGIL_OPCODE_NOT_EQUAL_I32:
                    result = a != b;
                    break;
                default:
                    result = false;
                    break;
                }
                vm->stack[vm->stack_count] = vigil_nanbox_from_bool(result);
                vm->stack_count += 1U;
                frame->ip += 1U;
                VM_BREAK();
            }

            /* ── Three-address i32 store superinstructions ────────────
               Format: [opcode][u32 dst][u32 a][u32 b]  (13 bytes)
               Reads locals a and b, operates, stores result to local dst.
               Zero stack traffic — pure register-style execution. */
            VM_CASE(LOCALS_ADD_I32_STORE)
            {
                uint32_t dst, idx_a, idx_b;
                int32_t a, b, r;
                VIGIL_VM_READ_U32(code, frame->ip, dst);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_a);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_b);
                a = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_a]);
                b = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_b]);
                if (VIGIL_I32_ADD_OVERFLOW(a, b, &r))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                vm->stack[frame->base_slot + dst] = vigil_nanbox_encode_i32(r);
                VM_BREAK();
            }
            VM_CASE(LOCALS_SUBTRACT_I32_STORE)
            {
                uint32_t dst, idx_a, idx_b;
                int32_t a, b, r;
                VIGIL_VM_READ_U32(code, frame->ip, dst);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_a);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_b);
                a = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_a]);
                b = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_b]);
                if (VIGIL_I32_SUB_OVERFLOW(a, b, &r))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                vm->stack[frame->base_slot + dst] = vigil_nanbox_encode_i32(r);
                VM_BREAK();
            }
            VM_CASE(LOCALS_MULTIPLY_I32_STORE)
            {
                uint32_t dst, idx_a, idx_b;
                int32_t a, b, r;
                VIGIL_VM_READ_U32(code, frame->ip, dst);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_a);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_b);
                a = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_a]);
                b = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_b]);
                if (VIGIL_I32_MUL_OVERFLOW(a, b, &r))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                vm->stack[frame->base_slot + dst] = vigil_nanbox_encode_i32(r);
                VM_BREAK();
            }
            VM_CASE(LOCALS_MODULO_I32_STORE)
            {
                uint32_t dst, idx_a, idx_b;
                int32_t a, b, r;
                VIGIL_VM_READ_U32(code, frame->ip, dst);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_a);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_b);
                a = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_a]);
                b = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_b]);
                if (b == 0 || (a == INT32_MIN && b == -1))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                r = a % b;
                vm->stack[frame->base_slot + dst] = vigil_nanbox_encode_i32(r);
                VM_BREAK();
            }
            VM_CASE(LOCALS_LESS_I32_STORE)
            VM_CASE(LOCALS_LESS_EQUAL_I32_STORE)
            VM_CASE(LOCALS_GREATER_I32_STORE)
            VM_CASE(LOCALS_GREATER_EQUAL_I32_STORE)
            VM_CASE(LOCALS_EQUAL_I32_STORE)
            VM_CASE(LOCALS_NOT_EQUAL_I32_STORE)
            {
                uint32_t dst, idx_a, idx_b;
                int32_t a, b;
                bool result;
                uint8_t op;
                VIGIL_VM_READ_U32(code, frame->ip, dst);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_a);
                VIGIL_VM_READ_RAW_U32(code, frame->ip, idx_b);
                a = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_a]);
                b = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx_b]);
                op = code[frame->ip - 13U];
                switch ((vigil_opcode_t)op)
                {
                case VIGIL_OPCODE_LOCALS_LESS_I32_STORE:
                    result = a < b;
                    break;
                case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE:
                    result = a <= b;
                    break;
                case VIGIL_OPCODE_LOCALS_GREATER_I32_STORE:
                    result = a > b;
                    break;
                case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE:
                    result = a >= b;
                    break;
                case VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE:
                    result = a == b;
                    break;
                case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE:
                    result = a != b;
                    break;
                default:
                    result = false;
                    break;
                }
                vm->stack[frame->base_slot + dst] = vigil_nanbox_from_bool(result);
                VM_BREAK();
            }

            /* ── INCREMENT_LOCAL_I32 ──────────────────────────────────
               Format: [opcode][u32 local_idx][i8 delta]  (6 bytes)
               Increments local[idx] by a signed 8-bit immediate.
               Covers i = i + 1, i = i - 1, and small constant steps. */
            VM_CASE(INCREMENT_LOCAL_I32)
            {
                uint32_t idx;
                int32_t val, delta, r;
                VIGIL_VM_READ_U32(code, frame->ip, idx);
                delta = (int8_t)code[frame->ip];
                frame->ip += 1U;
                val = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx]);
                if (VIGIL_I32_ADD_OVERFLOW(val, delta, &r))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                vm->stack[frame->base_slot + idx] = vigil_nanbox_encode_i32(r);
                VM_BREAK();
            }

            /* ── FORLOOP_I32 ──────────────────────────────────────────
               Format: [op][u32 local][i8 delta][u32 const_idx][u8 cmp][u32 back_off]
               Single-dispatch counting loop: increment, compare, branch. */
            VM_CASE(FORLOOP_I32)
            {
                uint32_t idx, ci, back;
                int32_t val, delta, r, limit;
                uint8_t cmp;
                int cont;
                const vigil_value_t *cv;

                VIGIL_VM_READ_U32(code, frame->ip, idx);
                delta = (int8_t)code[frame->ip];
                frame->ip += 1U;
                VIGIL_VM_READ_RAW_U32(code, frame->ip, ci);
                cmp = code[frame->ip];
                frame->ip += 1U;
                VIGIL_VM_READ_RAW_U32(code, frame->ip, back);

                val = vigil_nanbox_decode_i32(vm->stack[frame->base_slot + idx]);
                if (VIGIL_I32_ADD_OVERFLOW(val, delta, &r))
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 overflow", error);
                    goto cleanup;
                }
                vm->stack[frame->base_slot + idx] = vigil_nanbox_encode_i32(r);

                cv = VIGIL_VM_CHUNK_CONSTANT(frame->chunk, (size_t)ci);
                limit = vigil_nanbox_decode_i32(*cv);

                switch (cmp)
                {
                case 0:
                    cont = r < limit;
                    break;
                case 1:
                    cont = r <= limit;
                    break;
                case 2:
                    cont = r > limit;
                    break;
                case 3:
                    cont = r >= limit;
                    break;
                case 4:
                    cont = r != limit;
                    break;
                default:
                    cont = 0;
                    break;
                }
                if (cont)
                {
                    frame->ip -= (size_t)back;
                }
                else
                {
                    /* Push false so the POP after the loop has a value. */
                    vm->stack[vm->stack_count] = VIGIL_NANBOX_FALSE;
                    vm->stack_count += 1U;
                }
                VM_BREAK();
            }

            VM_CASE(NEGATE)
            value = vigil_vm_pop_or_nil(vm);
            if (vigil_nanbox_is_double(value))
            {
                vigil_value_t negated;

                vigil_value_init_float(&negated, -vigil_nanbox_decode_double(value));
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_push(vm, &negated, error);
                if (status != VIGIL_STATUS_OK)
                {
                    vigil_value_release(&negated);
                    goto cleanup;
                }
                vigil_value_release(&negated);
                frame->ip += 1U;
                VM_BREAK();
            }
            if (!vigil_nanbox_is_int(value))
            {
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "negation requires an integer or float operand", error);
                goto cleanup;
            }
            status = vigil_vm_checked_negate(vigil_value_as_int(&(value)), &integer_result);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "integer arithmetic overflow or invalid operation", error);
                goto cleanup;
            }
            do
            {
                (left) = vigil_nanbox_encode_int(integer_result);
            } while (0);
            VIGIL_VM_VALUE_RELEASE(&value);
            status = vigil_vm_push(vm, &left, error);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(NOT)
            value = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_bool(value))
            {
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "logical not requires a bool operand",
                                             error);
                goto cleanup;
            }
            vigil_value_init_bool(&left, !vigil_nanbox_decode_bool(value));
            VIGIL_VM_VALUE_RELEASE(&value);
            status = vigil_vm_push(vm, &left, error);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(BITWISE_NOT)
            value = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_int(value))
            {
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "bitwise not requires an integer operand", error);
                goto cleanup;
            }
            vigil_value_init_int(&left, ~vigil_value_as_int(&(value)));
            VIGIL_VM_VALUE_RELEASE(&value);
            status = vigil_vm_push(vm, &left, error);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(TO_I32)
            /* Fast path: if top of stack is already INT, just range-check */
            if (vm->stack_count > 0U && vigil_nanbox_is_int_inline(vm->stack[vm->stack_count - 1U]))
            {
                int64_t v = vigil_nanbox_decode_int(vm->stack[vm->stack_count - 1U]);
                if (v < (int64_t)INT32_MIN || v > (int64_t)INT32_MAX)
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                                 "i32 conversion overflow or invalid value", error);
                    goto cleanup;
                }
                frame->ip += 1U;
                VM_BREAK();
            }
            value = vigil_vm_pop_or_nil(vm);
            status = vigil_vm_convert_to_signed_integer_type(vm, &value, (int64_t)INT32_MIN, (int64_t)INT32_MAX,
                                                             "i32 conversion requires an int or float operand",
                                                             "i32 conversion overflow or invalid value", error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(TO_I64)
            value = vigil_vm_pop_or_nil(vm);
            status = vigil_vm_convert_to_signed_integer_type(vm, &value, INT64_MIN, INT64_MAX,
                                                             "i64 conversion requires an int or float operand",
                                                             "i64 conversion overflow or invalid value", error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(TO_U8)
            value = vigil_vm_pop_or_nil(vm);
            status = vigil_vm_convert_to_unsigned_integer_type(vm, &value, (uint64_t)UINT8_MAX,
                                                               "u8 conversion requires an int or float operand",
                                                               "u8 conversion overflow or invalid value", error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(TO_U32)
            value = vigil_vm_pop_or_nil(vm);
            status = vigil_vm_convert_to_unsigned_integer_type(vm, &value, (uint64_t)UINT32_MAX,
                                                               "u32 conversion requires an int or float operand",
                                                               "u32 conversion overflow or invalid value", error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(TO_U64)
            value = vigil_vm_pop_or_nil(vm);
            status = vigil_vm_convert_to_unsigned_integer_type(vm, &value, UINT64_MAX,
                                                               "u64 conversion requires an int or float operand",
                                                               "u64 conversion overflow or invalid value", error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
                goto cleanup;
            }
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(TO_F64)
            value = vigil_vm_pop_or_nil(vm);
            if (vigil_nanbox_is_double(value))
            {
                status = vigil_vm_push(vm, &value, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                frame->ip += 1U;
                VM_BREAK();
            }
            if (!vigil_vm_value_is_integer(&value))
            {
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "f64 conversion requires an int or float operand", error);
                goto cleanup;
            }
            if (vigil_nanbox_is_uint(value))
            {
                vigil_value_init_float(&left, (double)vigil_value_as_uint(&(value)));
            }
            else
            {
                vigil_value_init_float(&left, (double)vigil_value_as_int(&(value)));
            }
            VIGIL_VM_VALUE_RELEASE(&value);
            status = vigil_vm_push(vm, &left, error);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(TO_STRING)
            value = vigil_vm_pop_or_nil(vm);
            VIGIL_VM_VALUE_INIT_NIL(&left);
            status = vigil_vm_stringify_value(vm, &value, &left, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "string conversion requires a primitive or string operand", error);
                goto cleanup;
            }
            status = vigil_vm_push(vm, &left, error);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(FORMAT_F64)
            if ((status = vigil_vm_read_u32(vm, &operand, error)) != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            value = vigil_vm_pop_or_nil(vm);
            VIGIL_VM_VALUE_INIT_NIL(&left);
            status = vigil_vm_format_f64_value(vm, &value, operand, &left, error);
            VIGIL_VM_VALUE_RELEASE(&value);
            if (status != VIGIL_STATUS_OK)
            {
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "f64 formatting requires an f64 operand", error);
                goto cleanup;
            }
            status = vigil_vm_push(vm, &left, error);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            VM_BREAK();
            VM_CASE(FORMAT_SPEC)
            {
                uint32_t w1;
                uint32_t w2;
                if ((status = vigil_vm_read_u32(vm, &w1, error)) != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                if ((status = vigil_vm_read_raw_u32(vm, &w2, error)) != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                value = vigil_vm_pop_or_nil(vm);
                VIGIL_VM_VALUE_INIT_NIL(&left);
                status = vigil_vm_format_spec_value(vm, &value, w1, w2, &left, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "format specifier error", error);
                    goto cleanup;
                }
                status = vigil_vm_push(vm, &left, error);
                if (status != VIGIL_STATUS_OK)
                {
                    VIGIL_VM_VALUE_RELEASE(&left);
                    goto cleanup;
                }
                VIGIL_VM_VALUE_RELEASE(&left);
                VM_BREAK();
            }
            VM_CASE(NEW_ERROR)
            right = vigil_vm_pop_or_nil(vm);
            left = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_STRING)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "error construction requires string message and i32 kind", error);
                goto cleanup;
            }
            if (!vigil_nanbox_is_int(right))
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "error construction requires string message and i32 kind", error);
                goto cleanup;
            }
            {
                vigil_object_t *error_object = NULL;

                status = vigil_error_object_new(
                    vm->runtime, vigil_string_object_c_str(((vigil_object_t *)vigil_nanbox_decode_ptr(left))),
                    vigil_string_object_length(((vigil_object_t *)vigil_nanbox_decode_ptr(left))),
                    vigil_value_as_int(&(right)), &error_object, error);
                VIGIL_VM_VALUE_RELEASE(&left);
                VIGIL_VM_VALUE_RELEASE(&right);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                vigil_value_init_object(&value, &error_object);
            }
            status = vigil_vm_push(vm, &value, error);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&value);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&value);
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(GET_ERROR_KIND)
            value = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(value) || ((vigil_object_t *)vigil_nanbox_decode_ptr(value)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(value))) != VIGIL_OBJECT_ERROR)
            {
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "error kind access requires an err value", error);
                goto cleanup;
            }
            vigil_value_init_int(&left, vigil_error_object_kind(((vigil_object_t *)vigil_nanbox_decode_ptr(value))));
            VIGIL_VM_VALUE_RELEASE(&value);
            status = vigil_vm_push(vm, &left, error);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(GET_ERROR_MESSAGE)
            value = vigil_vm_pop_or_nil(vm);
            if (!vigil_nanbox_is_object(value) || ((vigil_object_t *)vigil_nanbox_decode_ptr(value)) == NULL ||
                vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(value))) != VIGIL_OBJECT_ERROR)
            {
                VIGIL_VM_VALUE_RELEASE(&value);
                status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                             "error message access requires an err value", error);
                goto cleanup;
            }
            {
                vigil_object_t *string_object = NULL;

                status = vigil_string_object_new(
                    vm->runtime, vigil_error_object_message(((vigil_object_t *)vigil_nanbox_decode_ptr(value))),
                    vigil_error_object_message_length(((vigil_object_t *)vigil_nanbox_decode_ptr(value))),
                    &string_object, error);
                VIGIL_VM_VALUE_RELEASE(&value);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                vigil_value_init_object(&left, &string_object);
            }
            status = vigil_vm_push(vm, &left, error);
            if (status != VIGIL_STATUS_OK)
            {
                VIGIL_VM_VALUE_RELEASE(&left);
                goto cleanup;
            }
            VIGIL_VM_VALUE_RELEASE(&left);
            frame->ip += 1U;
            VM_BREAK();
            VM_CASE(RETURN)
            frame->ip += 1U;
            if (frame->ip + 4U <= code_size)
            {
                VIGIL_VM_READ_RAW_U32(code, frame->ip, operand);
            }
            else
            {
                operand = 1U;
            }
            /* Fast-path RETURN: single value, no defers, caller waiting.
               Avoids heap-allocating a returned_values array.
               WARNING: This duplicates logic from vigil_vm_complete_return().
               If return semantics change, update BOTH paths.  See the
               developer checklist at the top of this file. */
            if (operand == 1U && frame->defer_count == 0U && !frame->draining_defers && vm->frame_count > 1U &&
                !vm->frames[vm->frame_count - 2U].draining_defers)
            {
                vigil_value_t ret_val;
                size_t base_slot;

                /* Grab return value directly (skip POP overhead). */
                vm->stack_count -= 1U;
                ret_val = vm->stack[vm->stack_count];
                base_slot = frame->base_slot;
                vm->frame_count -= 1U;
                /* The fast path only fires when defer_count == 0 and
                   draining_defers == false, so those fields are already
                   clean.  The CALL fast path overwrites callable,
                   function, chunk, ip, base_slot.  Nothing to clear. */
                /* Unwind stack: release any remaining locals. */
                while (vm->stack_count > base_slot)
                {
                    vm->stack_count -= 1U;
                    VIGIL_VM_VALUE_RELEASE(&vm->stack[vm->stack_count]);
                }
                /* Place return value at base_slot (stack has capacity). */
                vm->stack[vm->stack_count] = ret_val;
                vm->stack_count += 1U;
                VM_BREAK_RELOAD();
            }
            if (operand == 0U)
            {
                status = vigil_vm_complete_return(vm, NULL, 0U, out_value, error);
            }
            else
            {
                vigil_value_t *returned_values;
                size_t returned_capacity;
                size_t return_index;

                returned_values = NULL;
                returned_capacity = 0U;
                status = vigil_vm_grow_value_array(vm->runtime, &returned_values, &returned_capacity, (size_t)operand,
                                                   error);
                if (status != VIGIL_STATUS_OK)
                {
                    goto cleanup;
                }
                for (return_index = (size_t)operand; return_index > 0U; return_index -= 1U)
                {
                    returned_values[return_index - 1U] = vigil_vm_pop_or_nil(vm);
                }
                status = vigil_vm_complete_return(vm, returned_values, (size_t)operand, out_value, error);
            }
            if (status != VIGIL_STATUS_OK)
            {
                goto cleanup;
            }
            if (vm->frame_count == 0U)
            {
                return VIGIL_STATUS_OK;
            }
            VM_BREAK_RELOAD();
#if !VIGIL_VM_COMPUTED_GOTO
        default:
            status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_UNSUPPORTED, "unsupported opcode", error);
            goto cleanup;
#endif
#if VIGIL_VM_COMPUTED_GOTO
        vm_loop_end:
            (void)0;
            _Pragma("GCC diagnostic pop")
        }
#else
        }
#endif
    }

#undef VM_CASE
#undef VM_BREAK
#undef VM_BREAK_RELOAD

    status = vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INTERNAL, "chunk execution reached end without return", error);

cleanup:
    vigil_vm_release_stack(vm);
    vigil_vm_clear_frames(vm);
    return status;
}
