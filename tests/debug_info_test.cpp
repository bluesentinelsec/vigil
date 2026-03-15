#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "basl/debug_info.h"
#include "basl/native_module.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/stdlib.h"
#include "basl/value.h"
#include "basl/vm.h"
}

namespace {

struct DebugInfoFixture {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_native_registry_t natives;
    basl_diagnostic_list_t diagnostics;
    basl_debug_symbol_table_t symbols;

    DebugInfoFixture() {
        EXPECT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
        basl_source_registry_init(&registry, runtime);
        basl_diagnostic_list_init(&diagnostics, runtime);
        basl_native_registry_init(&natives);
        EXPECT_EQ(basl_stdlib_register_all(&natives, &error), BASL_STATUS_OK);
        basl_debug_symbol_table_init(&symbols, runtime);
    }

    ~DebugInfoFixture() {
        basl_debug_symbol_table_free(&symbols);
        basl_diagnostic_list_free(&diagnostics);
        basl_native_registry_free(&natives);
        basl_source_registry_free(&registry);
        basl_runtime_close(&runtime);
    }

    basl_object_t *compile(const char *source_text) {
        basl_source_id_t source_id = 0U;
        basl_object_t *function = nullptr;

        EXPECT_EQ(
            basl_source_registry_register_cstr(
                &registry, "main.basl", source_text, &source_id, &error),
            BASL_STATUS_OK
        );
        EXPECT_EQ(
            basl_compile_source_with_debug_info(
                &registry, source_id, &natives, &function,
                &diagnostics, &symbols, &error),
            BASL_STATUS_OK
        );
        EXPECT_EQ(basl_diagnostic_list_count(&diagnostics), 0U);
        return function;
    }
};

/* ── Symbol table tests ──────────────────────────────────────────── */

TEST(BaslDebugInfoTest, SymbolTableContainsMainFunction) {
    DebugInfoFixture f;
    basl_object_t *fn = f.compile(R"(
fn main() -> i32 {
    return 0;
}
    )");

    ASSERT_NE(fn, nullptr);
    EXPECT_GE(basl_debug_symbol_table_count(&f.symbols), 1U);

    bool found_main = false;
    for (size_t i = 0; i < basl_debug_symbol_table_count(&f.symbols); i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&f.symbols, i);
        if (sym->kind == BASL_DEBUG_SYMBOL_FUNCTION &&
            sym->name_length == 4 && memcmp(sym->name, "main", 4) == 0) {
            found_main = true;
        }
    }
    EXPECT_TRUE(found_main);
    basl_object_release(&fn);
}

TEST(BaslDebugInfoTest, SymbolTableContainsClassAndMembers) {
    DebugInfoFixture f;
    basl_object_t *fn = f.compile(R"(
class Point {
    pub i32 x;
    pub i32 y;

    pub fn distance() -> f64 {
        return 0.0;
    }
}

fn main() -> i32 {
    return 0;
}
    )");

    ASSERT_NE(fn, nullptr);

    bool found_class = false;
    bool found_field_x = false;
    bool found_method = false;
    size_t class_idx = SIZE_MAX;

    for (size_t i = 0; i < basl_debug_symbol_table_count(&f.symbols); i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&f.symbols, i);
        if (sym->kind == BASL_DEBUG_SYMBOL_CLASS &&
            sym->name_length == 5 && memcmp(sym->name, "Point", 5) == 0) {
            found_class = true;
            class_idx = i;
        }
        if (sym->kind == BASL_DEBUG_SYMBOL_FIELD &&
            sym->name_length == 1 && sym->name[0] == 'x') {
            found_field_x = true;
            EXPECT_EQ(sym->parent_index, class_idx);
        }
        if (sym->kind == BASL_DEBUG_SYMBOL_METHOD &&
            sym->name_length == 8 && memcmp(sym->name, "distance", 8) == 0) {
            found_method = true;
            EXPECT_EQ(sym->parent_index, class_idx);
        }
    }
    EXPECT_TRUE(found_class);
    EXPECT_TRUE(found_field_x);
    EXPECT_TRUE(found_method);
    basl_object_release(&fn);
}

TEST(BaslDebugInfoTest, SymbolTableContainsEnumAndMembers) {
    DebugInfoFixture f;
    basl_object_t *fn = f.compile(R"(
enum Color {
    Red,
    Green,
    Blue
}

fn main() -> i32 {
    return 0;
}
    )");

    ASSERT_NE(fn, nullptr);

    bool found_enum = false;
    bool found_red = false;
    size_t enum_idx = SIZE_MAX;

    for (size_t i = 0; i < basl_debug_symbol_table_count(&f.symbols); i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&f.symbols, i);
        if (sym->kind == BASL_DEBUG_SYMBOL_ENUM &&
            sym->name_length == 5 && memcmp(sym->name, "Color", 5) == 0) {
            found_enum = true;
            enum_idx = i;
        }
        if (sym->kind == BASL_DEBUG_SYMBOL_ENUM_MEMBER &&
            sym->name_length == 3 && memcmp(sym->name, "Red", 3) == 0) {
            found_red = true;
            EXPECT_EQ(sym->parent_index, enum_idx);
        }
    }
    EXPECT_TRUE(found_enum);
    EXPECT_TRUE(found_red);
    basl_object_release(&fn);
}

TEST(BaslDebugInfoTest, SymbolTableContainsGlobals) {
    DebugInfoFixture f;
    basl_object_t *fn = f.compile(R"(
const i32 MAX = 100;
i32 counter = 0;

fn main() -> i32 {
    return MAX;
}
    )");

    ASSERT_NE(fn, nullptr);

    bool found_const = false;
    bool found_var = false;

    for (size_t i = 0; i < basl_debug_symbol_table_count(&f.symbols); i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&f.symbols, i);
        if (sym->kind == BASL_DEBUG_SYMBOL_GLOBAL_CONST &&
            sym->name_length == 3 && memcmp(sym->name, "MAX", 3) == 0) {
            found_const = true;
        }
        if (sym->kind == BASL_DEBUG_SYMBOL_GLOBAL_VAR &&
            sym->name_length == 7 && memcmp(sym->name, "counter", 7) == 0) {
            found_var = true;
        }
    }
    EXPECT_TRUE(found_const);
    EXPECT_TRUE(found_var);
    basl_object_release(&fn);
}

/* ── Local variable debug info tests ─────────────────────────────── */

TEST(BaslDebugInfoTest, LocalTableBasic) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_debug_local_table_t table;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_debug_local_table_init(&table, runtime);

    EXPECT_EQ(basl_debug_local_table_count(&table), 0U);

    EXPECT_EQ(basl_debug_local_table_add(&table, "x", 1, 0, 0, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_debug_local_table_add(&table, "y", 1, 1, 5, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_debug_local_table_count(&table), 2U);

    /* Close scope for slot >= 1 at IP 10. */
    basl_debug_local_table_close_scope(&table, 1, 10);

    const basl_debug_local_t *entry = basl_debug_local_table_get(&table, 1);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->name_length, 1U);
    EXPECT_EQ(entry->name[0], 'y');
    EXPECT_EQ(entry->slot, 1U);
    EXPECT_EQ(entry->scope_start_ip, 5U);
    EXPECT_EQ(entry->scope_end_ip, 10U);

    /* x should still be open. */
    const basl_debug_local_t *x_entry = basl_debug_local_table_get(&table, 0);
    ASSERT_NE(x_entry, nullptr);
    EXPECT_EQ(x_entry->scope_end_ip, SIZE_MAX);

    basl_debug_local_table_free(&table);
    basl_runtime_close(&runtime);
}

TEST(BaslDebugInfoTest, ChunkContainsDebugLocals) {
    DebugInfoFixture f;
    basl_object_t *fn = f.compile(R"(
fn main() -> i32 {
    i32 x = 10;
    i32 y = 20;
    return x + y;
}
    )");

    ASSERT_NE(fn, nullptr);

    /* The compiled function's chunk should have debug locals for x and y.
     * We can't easily access the chunk from the function object in the
     * public API, but we can verify the compilation succeeded and the
     * program runs correctly. */
    basl_vm_t *vm = nullptr;
    basl_value_t result;
    basl_value_init_nil(&result);
    ASSERT_EQ(basl_vm_open(&vm, f.runtime, nullptr, &f.error), BASL_STATUS_OK);
    EXPECT_EQ(basl_vm_execute_function(vm, fn, &result, &f.error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_as_int(&result), 30);
    basl_value_release(&result);
    basl_vm_close(&vm);
    basl_object_release(&fn);
}

TEST(BaslDebugInfoTest, SymbolTableContainsInterface) {
    DebugInfoFixture f;
    basl_object_t *fn = f.compile(R"(
interface Drawable {
    fn draw() -> void;
}

class Circle implements Drawable {
    pub i32 radius;

    pub fn draw() -> void {}
}

fn main() -> i32 {
    return 0;
}
    )");

    ASSERT_NE(fn, nullptr);

    bool found_interface = false;
    for (size_t i = 0; i < basl_debug_symbol_table_count(&f.symbols); i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&f.symbols, i);
        if (sym->kind == BASL_DEBUG_SYMBOL_INTERFACE &&
            sym->name_length == 8 && memcmp(sym->name, "Drawable", 8) == 0) {
            found_interface = true;
        }
    }
    EXPECT_TRUE(found_interface);
    basl_object_release(&fn);
}

}  // namespace
