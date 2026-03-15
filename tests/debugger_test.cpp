#include <gtest/gtest.h>
#include <cstring>
#include <vector>

extern "C" {
#include "basl/debugger.h"
#include "basl/native_module.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/stdlib.h"
#include "basl/value.h"
#include "basl/vm.h"
}

namespace {

struct DebuggerFixture {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_native_registry_t natives;
    basl_diagnostic_list_t diagnostics;
    basl_debugger_t *debugger = nullptr;
    basl_source_id_t source_id = 0U;

    DebuggerFixture() {
        EXPECT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
        EXPECT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
        basl_source_registry_init(&registry, runtime);
        basl_diagnostic_list_init(&diagnostics, runtime);
        basl_native_registry_init(&natives);
        EXPECT_EQ(basl_stdlib_register_all(&natives, &error), BASL_STATUS_OK);
    }

    ~DebuggerFixture() {
        if (debugger != nullptr) basl_debugger_destroy(&debugger);
        basl_diagnostic_list_free(&diagnostics);
        basl_native_registry_free(&natives);
        basl_source_registry_free(&registry);
        basl_vm_close(&vm);
        basl_runtime_close(&runtime);
    }

    basl_object_t *compile(const char *source_text) {
        basl_object_t *function = nullptr;
        EXPECT_EQ(
            basl_source_registry_register_cstr(
                &registry, "main.basl", source_text, &source_id, &error),
            BASL_STATUS_OK
        );
        EXPECT_EQ(
            basl_compile_source_with_natives(
                &registry, source_id, &natives, &function,
                &diagnostics, &error),
            BASL_STATUS_OK
        );
        return function;
    }

    void create_debugger() {
        EXPECT_EQ(
            basl_debugger_create(&debugger, vm, &registry, &error),
            BASL_STATUS_OK
        );
        ASSERT_NE(debugger, nullptr);
    }
};

/* Track callback invocations. */
struct CallbackState {
    std::vector<uint32_t> hit_lines;
    std::vector<basl_debug_stop_reason_t> reasons;
    int continue_after = 0;  /* 0 = always continue */
};

static basl_debug_action_t test_callback(
    basl_debugger_t *debugger,
    basl_debug_stop_reason_t reason,
    void *userdata
) {
    auto *state = static_cast<CallbackState *>(userdata);
    uint32_t line = 0;
    basl_debugger_current_location(debugger, nullptr, &line, nullptr);
    state->hit_lines.push_back(line);
    state->reasons.push_back(reason);
    return BASL_DEBUG_CONTINUE;
}

/* ── Tests ───────────────────────────────────────────────────────── */

TEST(BaslDebuggerTest, CreateAndDestroy) {
    DebuggerFixture f;
    f.create_debugger();
    EXPECT_NE(f.debugger, nullptr);
}

TEST(BaslDebuggerTest, AttachDetachDoesNotBreakExecution) {
    DebuggerFixture f;
    basl_object_t *fn = f.compile(R"(
fn main() -> i32 {
    return 42;
}
    )");
    ASSERT_NE(fn, nullptr);
    f.create_debugger();
    basl_debugger_attach(f.debugger);

    basl_value_t result;
    basl_value_init_nil(&result);
    EXPECT_EQ(basl_vm_execute_function(f.vm, fn, &result, &f.error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_as_int(&result), 42);

    basl_debugger_detach(f.debugger);
    basl_value_release(&result);
    basl_object_release(&fn);
}

TEST(BaslDebuggerTest, BreakpointHitsCorrectLine) {
    DebuggerFixture f;
    basl_object_t *fn = f.compile(R"(
fn main() -> i32 {
    i32 x = 10;
    i32 y = 20;
    return x + y;
}
    )");
    ASSERT_NE(fn, nullptr);
    f.create_debugger();

    CallbackState cb_state;
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
    EXPECT_FALSE(cb_state.hit_lines.empty());
    bool found_line_4 = false;
    for (uint32_t line : cb_state.hit_lines) {
        if (line == 4) found_line_4 = true;
    }
    EXPECT_TRUE(found_line_4);
    EXPECT_EQ(cb_state.reasons[0], BASL_DEBUG_STOP_BREAKPOINT);

    basl_debugger_detach(f.debugger);
    basl_value_release(&result);
    basl_object_release(&fn);
}

TEST(BaslDebuggerTest, ClearBreakpointStopsHitting) {
    DebuggerFixture f;
    basl_object_t *fn = f.compile(R"(
fn main() -> i32 {
    i32 x = 10;
    return x;
}
    )");
    ASSERT_NE(fn, nullptr);
    f.create_debugger();

    CallbackState cb_state;
    basl_debugger_set_callback(f.debugger, test_callback, &cb_state);

    size_t bp_id = 0;
    basl_debugger_set_breakpoint(f.debugger, f.source_id, 3, &bp_id, &f.error);
    basl_debugger_clear_breakpoint(f.debugger, bp_id);

    basl_debugger_attach(f.debugger);

    basl_value_t result;
    basl_value_init_nil(&result);
    EXPECT_EQ(basl_vm_execute_function(f.vm, fn, &result, &f.error), BASL_STATUS_OK);

    /* No breakpoints should have been hit. */
    EXPECT_TRUE(cb_state.hit_lines.empty());

    basl_debugger_detach(f.debugger);
    basl_value_release(&result);
    basl_object_release(&fn);
}

TEST(BaslDebuggerTest, FrameCountDuringCallback) {
    DebuggerFixture f;
    basl_object_t *fn = f.compile(R"(
fn main() -> i32 {
    return 42;
}
    )");
    ASSERT_NE(fn, nullptr);
    f.create_debugger();

    struct FrameCheckState {
        size_t frame_count = 0;
    } fc_state;

    auto frame_check_cb = [](basl_debugger_t *debugger,
                             basl_debug_stop_reason_t,
                             void *userdata) -> basl_debug_action_t {
        auto *s = static_cast<FrameCheckState *>(userdata);
        s->frame_count = basl_debugger_frame_count(debugger);
        return BASL_DEBUG_CONTINUE;
    };

    basl_debugger_set_callback(f.debugger, frame_check_cb, &fc_state);
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
}

TEST(BaslDebuggerTest, NoDebuggerAttachedRunsNormally) {
    DebuggerFixture f;
    basl_object_t *fn = f.compile(R"(
fn main() -> i32 {
    return 99;
}
    )");
    ASSERT_NE(fn, nullptr);

    /* Don't create or attach a debugger — just run. */
    basl_value_t result;
    basl_value_init_nil(&result);
    EXPECT_EQ(basl_vm_execute_function(f.vm, fn, &result, &f.error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_as_int(&result), 99);

    basl_value_release(&result);
    basl_object_release(&fn);
}

}  // namespace
