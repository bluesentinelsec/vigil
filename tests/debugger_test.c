#include "basl_test.h"
#include <string.h>

#include "basl/debugger.h"
#include "basl/compiler.h"
#include "basl/native_module.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/stdlib.h"
#include "basl/value.h"
#include "basl/vm.h"

typedef struct DebuggerFixture {
    basl_runtime_t *runtime;
    basl_vm_t *vm;
    basl_error_t error;
    basl_source_registry_t registry;
    basl_native_registry_t natives;
    basl_diagnostic_list_t diagnostics;
    basl_debugger_t *debugger;
    basl_source_id_t source_id;
} DebuggerFixture;

static void dbgf_init(DebuggerFixture *f) {
    memset(f, 0, sizeof(*f));
    basl_runtime_open(&f->runtime, NULL, &f->error);
    basl_vm_open(&f->vm, f->runtime, NULL, &f->error);
    basl_source_registry_init(&f->registry, f->runtime);
    basl_diagnostic_list_init(&f->diagnostics, f->runtime);
    basl_native_registry_init(&f->natives);
    basl_stdlib_register_all(&f->natives, &f->error);
}

static void dbgf_free(DebuggerFixture *f) {
    basl_debugger_destroy(&f->debugger);
    basl_diagnostic_list_free(&f->diagnostics);
    basl_native_registry_free(&f->natives);
    basl_source_registry_free(&f->registry);
    basl_vm_close(&f->vm);
    basl_runtime_close(&f->runtime);
}

static void dbgf_create_debugger(DebuggerFixture *f) {
    basl_debugger_create(&f->debugger, f->vm, &f->registry, &f->error);
}

static basl_object_t *dbgf_compile(DebuggerFixture *f, const char *source_text) {
    basl_object_t *function = NULL;
    basl_source_registry_register_cstr(
        &f->registry, "main.basl", source_text, &f->source_id, &f->error);
    basl_compile_source_with_natives(
        &f->registry, f->source_id, &f->natives, &function,
        &f->diagnostics, &f->error);
    return function;
}

TEST(BaslDebuggerTest, CreateAndDestroy) {
    DebuggerFixture f;
    dbgf_init(&f);
    dbgf_create_debugger(&f);
    EXPECT_NE(f.debugger, NULL);
    dbgf_free(&f);
}

TEST(BaslDebuggerTest, AttachDetachDoesNotBreakExecution) {
    DebuggerFixture f;
    dbgf_init(&f);
    basl_object_t *fn = dbgf_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    return 42;\n"
        "}\n"
        "    ");
    ASSERT_NE(fn, NULL);
    dbgf_create_debugger(&f);
    basl_debugger_attach(f.debugger);

    basl_value_t result;
    basl_value_init_nil(&result);
    EXPECT_EQ(basl_vm_execute_function(f.vm, fn, &result, &f.error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_as_int(&result), 42);

    basl_debugger_detach(f.debugger);
    basl_value_release(&result);
    basl_object_release(&fn);
    dbgf_free(&f);
}


typedef struct CallbackState {
    uint32_t hit_lines[64];
    size_t hit_count;
    basl_debug_stop_reason_t reasons[64];
    size_t reason_count;
} CallbackState;

static basl_debug_action_t test_callback(
    basl_debugger_t *dbg,
    basl_debug_stop_reason_t reason,
    void *userdata
) {
    CallbackState *state = (CallbackState *)userdata;
    uint32_t line = 0;
    basl_debugger_current_location(dbg, NULL, &line, NULL);
    state->hit_lines[state->hit_count++] = line;
    state->reasons[state->reason_count++] = reason;
    return BASL_DEBUG_CONTINUE;
}

TEST(BaslDebuggerTest, BreakpointHitsCorrectLine) {
    DebuggerFixture f;
    dbgf_init(&f);
    basl_object_t *fn = dbgf_compile(&f, "\n"
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
    basl_debugger_set_callback(f.debugger, test_callback, &cb_state);

    size_t bp_id = 0;
    EXPECT_EQ(
        basl_debugger_set_breakpoint(f.debugger, f.source_id, 4, &bp_id, &f.error),
        BASL_STATUS_OK
    );

    basl_debugger_attach(f.debugger);

    basl_value_t result;
    basl_value_init_nil(&result);
    EXPECT_EQ(basl_vm_execute_function(f.vm, fn, &result, &f.error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_as_int(&result), 30);

    /* Should have hit the breakpoint on line 4. */
    EXPECT_TRUE(cb_state.hit_count > 0);
    int found_line_4 = false;
    { size_t ii_; for (ii_ = 0; ii_ < cb_state.hit_count; ii_++) { uint32_t line = cb_state.hit_lines[ii_];
        if (line == 4) found_line_4 = true;
    } }
    EXPECT_TRUE(found_line_4);
    EXPECT_EQ(cb_state.reasons[0], BASL_DEBUG_STOP_BREAKPOINT);

    basl_debugger_detach(f.debugger);
    basl_value_release(&result);
    basl_object_release(&fn);
    dbgf_free(&f);
}

TEST(BaslDebuggerTest, ClearBreakpointStopsHitting) {
    DebuggerFixture f;
    dbgf_init(&f);
    basl_object_t *fn = dbgf_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    i32 x = 10;\n"
        "    return x;\n"
        "}\n"
        "    ");
    ASSERT_NE(fn, NULL);
    dbgf_create_debugger(&f);

    CallbackState cb_state;
    memset(&cb_state, 0, sizeof(cb_state));
    basl_debugger_set_callback(f.debugger, test_callback, &cb_state);

    size_t bp_id = 0;
    basl_debugger_set_breakpoint(f.debugger, f.source_id, 3, &bp_id, &f.error);
    basl_debugger_clear_breakpoint(f.debugger, bp_id);

    basl_debugger_attach(f.debugger);

    basl_value_t result;
    basl_value_init_nil(&result);
    EXPECT_EQ(basl_vm_execute_function(f.vm, fn, &result, &f.error), BASL_STATUS_OK);

    /* No breakpoints should have been hit. */
    EXPECT_EQ(cb_state.hit_count, 0U);

    basl_debugger_detach(f.debugger);
    basl_value_release(&result);
    basl_object_release(&fn);
    dbgf_free(&f);
}

struct FrameCheckState {
    size_t frame_count;
};

static basl_debug_action_t frame_check_callback(
    basl_debugger_t *debugger,
    basl_debug_stop_reason_t reason,
    void *userdata
) {
    struct FrameCheckState *s = (struct FrameCheckState *)userdata;
    (void)reason;
    s->frame_count = basl_debugger_frame_count(debugger);
    return BASL_DEBUG_CONTINUE;
}

TEST(BaslDebuggerTest, FrameCountDuringCallback) {
    DebuggerFixture f;
    dbgf_init(&f);
    basl_object_t *fn = dbgf_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    return 42;\n"
        "}\n"
        "    ");
    ASSERT_NE(fn, NULL);
    dbgf_create_debugger(&f);

    struct FrameCheckState fc_state;
    memset(&fc_state, 0, sizeof(fc_state));

    basl_debugger_set_callback(f.debugger, frame_check_callback, &fc_state);
    size_t bp_id = 0;
    basl_debugger_set_breakpoint(f.debugger, f.source_id, 3, &bp_id, &f.error);
    basl_debugger_attach(f.debugger);

    basl_value_t result;
    basl_value_init_nil(&result);
    basl_vm_execute_function(f.vm, fn, &result, &f.error);

    EXPECT_GE(fc_state.frame_count, 1U);

    basl_debugger_detach(f.debugger);
    basl_value_release(&result);
    basl_object_release(&fn);
    dbgf_free(&f);
}

TEST(BaslDebuggerTest, NoDebuggerAttachedRunsNormally) {
    DebuggerFixture f;
    dbgf_init(&f);
    basl_object_t *fn = dbgf_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    return 99;\n"
        "}\n"
        "    ");
    ASSERT_NE(fn, NULL);

    /* Don't create or attach a debugger — just run. */
    basl_value_t result;
    basl_value_init_nil(&result);
    EXPECT_EQ(basl_vm_execute_function(f.vm, fn, &result, &f.error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_as_int(&result), 99);

    basl_value_release(&result);
    basl_object_release(&fn);
    dbgf_free(&f);
}

void register_debugger_tests(void) {
    REGISTER_TEST(BaslDebuggerTest, CreateAndDestroy);
    REGISTER_TEST(BaslDebuggerTest, AttachDetachDoesNotBreakExecution);
    REGISTER_TEST(BaslDebuggerTest, BreakpointHitsCorrectLine);
    REGISTER_TEST(BaslDebuggerTest, ClearBreakpointStopsHitting);
    REGISTER_TEST(BaslDebuggerTest, FrameCountDuringCallback);
    REGISTER_TEST(BaslDebuggerTest, NoDebuggerAttachedRunsNormally);
}
