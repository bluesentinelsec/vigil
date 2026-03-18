#include "vigil_test.h"
#include <string.h>

#include "vigil/debugger.h"
#include "vigil/compiler.h"
#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/source.h"
#include "vigil/stdlib.h"
#include "vigil/value.h"
#include "vigil/vm.h"

typedef struct DebuggerFixture {
    vigil_runtime_t *runtime;
    vigil_vm_t *vm;
    vigil_error_t error;
    vigil_source_registry_t registry;
    vigil_native_registry_t natives;
    vigil_diagnostic_list_t diagnostics;
    vigil_debugger_t *debugger;
    vigil_source_id_t source_id;
} DebuggerFixture;

static void dbgf_init(DebuggerFixture *f) {
    memset(f, 0, sizeof(*f));
    vigil_runtime_open(&f->runtime, NULL, &f->error);
    vigil_vm_open(&f->vm, f->runtime, NULL, &f->error);
    vigil_source_registry_init(&f->registry, f->runtime);
    vigil_diagnostic_list_init(&f->diagnostics, f->runtime);
    vigil_native_registry_init(&f->natives);
    vigil_stdlib_register_all(&f->natives, &f->error);
}

static void dbgf_free(DebuggerFixture *f) {
    vigil_debugger_destroy(&f->debugger);
    vigil_diagnostic_list_free(&f->diagnostics);
    vigil_native_registry_free(&f->natives);
    vigil_source_registry_free(&f->registry);
    vigil_vm_close(&f->vm);
    vigil_runtime_close(&f->runtime);
}

static void dbgf_create_debugger(DebuggerFixture *f) {
    vigil_debugger_create(&f->debugger, f->vm, &f->registry, &f->error);
}

static vigil_object_t *dbgf_compile(DebuggerFixture *f, const char *source_text) {
    vigil_object_t *function = NULL;
    vigil_source_registry_register_cstr(
        &f->registry, "main.vigil", source_text, &f->source_id, &f->error);
    vigil_compile_source_with_natives(
        &f->registry, f->source_id, &f->natives, &function,
        &f->diagnostics, &f->error);
    return function;
}

TEST(VigilDebuggerTest, CreateAndDestroy) {
    DebuggerFixture f;
    dbgf_init(&f);
    dbgf_create_debugger(&f);
    EXPECT_NE(f.debugger, NULL);
    dbgf_free(&f);
}

TEST(VigilDebuggerTest, AttachDetachDoesNotBreakExecution) {
    DebuggerFixture f;
    dbgf_init(&f);
    vigil_object_t *fn = dbgf_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    return 42;\n"
        "}\n"
        "    ");
    ASSERT_NE(fn, NULL);
    dbgf_create_debugger(&f);
    vigil_debugger_attach(f.debugger);

    vigil_value_t result;
    vigil_value_init_nil(&result);
    EXPECT_EQ(vigil_vm_execute_function(f.vm, fn, &result, &f.error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 42);

    vigil_debugger_detach(f.debugger);
    vigil_value_release(&result);
    vigil_object_release(&fn);
    dbgf_free(&f);
}


typedef struct CallbackState {
    uint32_t hit_lines[64];
    size_t hit_count;
    vigil_debug_stop_reason_t reasons[64];
    size_t reason_count;
} CallbackState;

static vigil_debug_action_t test_callback(
    vigil_debugger_t *dbg,
    vigil_debug_stop_reason_t reason,
    void *userdata
) {
    CallbackState *state = (CallbackState *)userdata;
    uint32_t line = 0;
    vigil_debugger_current_location(dbg, NULL, &line, NULL);
    state->hit_lines[state->hit_count++] = line;
    state->reasons[state->reason_count++] = reason;
    return VIGIL_DEBUG_CONTINUE;
}

TEST(VigilDebuggerTest, BreakpointHitsCorrectLine) {
    DebuggerFixture f;
    dbgf_init(&f);
    vigil_object_t *fn = dbgf_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    i32 x = 10;\n"
        "    i32 y = 20;\n"
        "    return x + y;\n"
        "}\n"
        "    ");
    ASSERT_NE(fn, NULL);
    dbgf_create_debugger(&f);

    CallbackState cb_state;
    memset(&cb_state, 0, sizeof(cb_state));
    vigil_debugger_set_callback(f.debugger, test_callback, &cb_state);

    size_t bp_id = 0;
    EXPECT_EQ(
        vigil_debugger_set_breakpoint(f.debugger, f.source_id, 4, &bp_id, &f.error),
        VIGIL_STATUS_OK
    );

    vigil_debugger_attach(f.debugger);

    vigil_value_t result;
    vigil_value_init_nil(&result);
    EXPECT_EQ(vigil_vm_execute_function(f.vm, fn, &result, &f.error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 30);

    /* Should have hit the breakpoint on line 4. */
    EXPECT_TRUE(cb_state.hit_count > 0);
    int found_line_4 = false;
    { size_t ii_; for (ii_ = 0; ii_ < cb_state.hit_count; ii_++) { uint32_t line = cb_state.hit_lines[ii_];
        if (line == 4) found_line_4 = true;
    } }
    EXPECT_TRUE(found_line_4);
    EXPECT_EQ(cb_state.reasons[0], VIGIL_DEBUG_STOP_BREAKPOINT);

    vigil_debugger_detach(f.debugger);
    vigil_value_release(&result);
    vigil_object_release(&fn);
    dbgf_free(&f);
}

TEST(VigilDebuggerTest, ClearBreakpointStopsHitting) {
    DebuggerFixture f;
    dbgf_init(&f);
    vigil_object_t *fn = dbgf_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    i32 x = 10;\n"
        "    return x;\n"
        "}\n"
        "    ");
    ASSERT_NE(fn, NULL);
    dbgf_create_debugger(&f);

    CallbackState cb_state;
    memset(&cb_state, 0, sizeof(cb_state));
    vigil_debugger_set_callback(f.debugger, test_callback, &cb_state);

    size_t bp_id = 0;
    vigil_debugger_set_breakpoint(f.debugger, f.source_id, 3, &bp_id, &f.error);
    vigil_debugger_clear_breakpoint(f.debugger, bp_id);

    vigil_debugger_attach(f.debugger);

    vigil_value_t result;
    vigil_value_init_nil(&result);
    EXPECT_EQ(vigil_vm_execute_function(f.vm, fn, &result, &f.error), VIGIL_STATUS_OK);

    /* No breakpoints should have been hit. */
    EXPECT_EQ(cb_state.hit_count, 0U);

    vigil_debugger_detach(f.debugger);
    vigil_value_release(&result);
    vigil_object_release(&fn);
    dbgf_free(&f);
}

struct FrameCheckState {
    size_t frame_count;
};

static vigil_debug_action_t frame_check_callback(
    vigil_debugger_t *debugger,
    vigil_debug_stop_reason_t reason,
    void *userdata
) {
    struct FrameCheckState *s = (struct FrameCheckState *)userdata;
    (void)reason;
    s->frame_count = vigil_debugger_frame_count(debugger);
    return VIGIL_DEBUG_CONTINUE;
}

TEST(VigilDebuggerTest, FrameCountDuringCallback) {
    DebuggerFixture f;
    dbgf_init(&f);
    vigil_object_t *fn = dbgf_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    return 42;\n"
        "}\n"
        "    ");
    ASSERT_NE(fn, NULL);
    dbgf_create_debugger(&f);

    struct FrameCheckState fc_state;
    memset(&fc_state, 0, sizeof(fc_state));

    vigil_debugger_set_callback(f.debugger, frame_check_callback, &fc_state);
    size_t bp_id = 0;
    vigil_debugger_set_breakpoint(f.debugger, f.source_id, 3, &bp_id, &f.error);
    vigil_debugger_attach(f.debugger);

    vigil_value_t result;
    vigil_value_init_nil(&result);
    vigil_vm_execute_function(f.vm, fn, &result, &f.error);

    EXPECT_GE(fc_state.frame_count, 1U);

    vigil_debugger_detach(f.debugger);
    vigil_value_release(&result);
    vigil_object_release(&fn);
    dbgf_free(&f);
}

TEST(VigilDebuggerTest, NoDebuggerAttachedRunsNormally) {
    DebuggerFixture f;
    dbgf_init(&f);
    vigil_object_t *fn = dbgf_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    return 99;\n"
        "}\n"
        "    ");
    ASSERT_NE(fn, NULL);

    /* Don't create or attach a debugger — just run. */
    vigil_value_t result;
    vigil_value_init_nil(&result);
    EXPECT_EQ(vigil_vm_execute_function(f.vm, fn, &result, &f.error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_value_as_int(&result), 99);

    vigil_value_release(&result);
    vigil_object_release(&fn);
    dbgf_free(&f);
}

void register_debugger_tests(void) {
    REGISTER_TEST(VigilDebuggerTest, CreateAndDestroy);
    REGISTER_TEST(VigilDebuggerTest, AttachDetachDoesNotBreakExecution);
    REGISTER_TEST(VigilDebuggerTest, BreakpointHitsCorrectLine);
    REGISTER_TEST(VigilDebuggerTest, ClearBreakpointStopsHitting);
    REGISTER_TEST(VigilDebuggerTest, FrameCountDuringCallback);
    REGISTER_TEST(VigilDebuggerTest, NoDebuggerAttachedRunsNormally);
}
