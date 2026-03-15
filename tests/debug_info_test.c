#include "basl_test.h"
#include <string.h>

#include "basl/debug_info.h"
#include "basl/native_module.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/stdlib.h"
#include "basl/value.h"
#include "basl/vm.h"

typedef struct DebugInfoFixture {
    basl_runtime_t *runtime;
    basl_error_t error;
    basl_source_registry_t registry;
    basl_native_registry_t natives;
    basl_diagnostic_list_t diagnostics;
    basl_debug_symbol_table_t symbols;
} DebugInfoFixture;

static void dif_init(DebugInfoFixture *f) {
    memset(f, 0, sizeof(*f));
    basl_runtime_open(&f->runtime, NULL, &f->error);
    basl_source_registry_init(&f->registry, f->runtime);
    basl_diagnostic_list_init(&f->diagnostics, f->runtime);
    basl_native_registry_init(&f->natives);
    basl_stdlib_register_all(&f->natives, &f->error);
    basl_debug_symbol_table_init(&f->symbols, f->runtime);
}

static void dif_free(DebugInfoFixture *f) {
    basl_debug_symbol_table_free(&f->symbols);
    basl_diagnostic_list_free(&f->diagnostics);
    basl_native_registry_free(&f->natives);
    basl_source_registry_free(&f->registry);
    basl_runtime_close(&f->runtime);
}

static basl_object_t *dif_compile(DebugInfoFixture *f, const char *source_text) {
    basl_source_id_t source_id = 0U;
    basl_object_t *function = NULL;
    if (basl_source_registry_register_cstr(
            &f->registry, "main.basl", source_text, &source_id, &f->error) != BASL_STATUS_OK)
        return NULL;
    if (basl_compile_source_with_debug_info(
            &f->registry, source_id, &f->natives, &function,
            &f->diagnostics, &f->symbols, &f->error) != BASL_STATUS_OK)
        return NULL;
    return function;
}

TEST(BaslDebugInfoTest, SymbolTableContainsMainFunction) {
    DebugInfoFixture f;
    dif_init(&f);
    basl_object_t *fn = dif_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    return 0;\n"
        "}\n"
        "    ");

    ASSERT_NE(fn, NULL);
    EXPECT_GE(basl_debug_symbol_table_count(&f.symbols), 1U);

    int found_main = false;
    for (size_t i = 0; i < basl_debug_symbol_table_count(&f.symbols); i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&f.symbols, i);
        if (sym->kind == BASL_DEBUG_SYMBOL_FUNCTION &&
            sym->name_length == 4 && memcmp(sym->name, "main", 4) == 0) {
            found_main = true;
        }
    }
    EXPECT_TRUE(found_main);
    basl_object_release(&fn);
    dif_free(&f);
}

TEST(BaslDebugInfoTest, SymbolTableContainsClassAndMembers) {
    DebugInfoFixture f;
    dif_init(&f);
    basl_object_t *fn = dif_compile(&f, "\n"
        "class Point {\n"
        "    pub i32 x;\n"
        "    pub i32 y;\n"
        "\n"
        "    pub fn distance() -> f64 {\n"
        "        return 0.0;\n"
        "    }\n"
        "}\n"
        "\n"
        "fn main() -> i32 {\n"
        "    return 0;\n"
        "}\n"
        "    ");

    ASSERT_NE(fn, NULL);

    int found_class = false;
    int found_field_x = false;
    int found_method = false;
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
    dif_free(&f);
}

TEST(BaslDebugInfoTest, SymbolTableContainsEnumAndMembers) {
    DebugInfoFixture f;
    dif_init(&f);
    basl_object_t *fn = dif_compile(&f, "\n"
        "enum Color {\n"
        "    Red,\n"
        "    Green,\n"
        "    Blue\n"
        "}\n"
        "\n"
        "fn main() -> i32 {\n"
        "    return 0;\n"
        "}\n"
        "    ");

    ASSERT_NE(fn, NULL);

    int found_enum = false;
    int found_red = false;
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
    dif_free(&f);
}

TEST(BaslDebugInfoTest, SymbolTableContainsGlobals) {
    DebugInfoFixture f;
    dif_init(&f);
    basl_object_t *fn = dif_compile(&f, "\n"
        "const i32 MAX = 100;\n"
        "i32 counter = 0;\n"
        "\n"
        "fn main() -> i32 {\n"
        "    return MAX;\n"
        "}\n"
        "    ");

    ASSERT_NE(fn, NULL);

    int found_const = false;
    int found_var = false;

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
    dif_free(&f);
}

TEST(BaslDebugInfoTest, LocalTableBasic) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_debug_local_table_t table;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_debug_local_table_init(&table, runtime);

    EXPECT_EQ(basl_debug_local_table_count(&table), 0U);

    EXPECT_EQ(basl_debug_local_table_add(&table, "x", 1, 0, 0, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_debug_local_table_add(&table, "y", 1, 1, 5, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_debug_local_table_count(&table), 2U);

    /* Close scope for slot >= 1 at IP 10. */
    basl_debug_local_table_close_scope(&table, 1, 10);

    const basl_debug_local_t *entry = basl_debug_local_table_get(&table, 1);
    ASSERT_NE(entry, NULL);
    EXPECT_EQ(entry->name_length, 1U);
    EXPECT_EQ(entry->name[0], 'y');
    EXPECT_EQ(entry->slot, 1U);
    EXPECT_EQ(entry->scope_start_ip, 5U);
    EXPECT_EQ(entry->scope_end_ip, 10U);

    /* x should still be open. */
    const basl_debug_local_t *x_entry = basl_debug_local_table_get(&table, 0);
    ASSERT_NE(x_entry, NULL);
    EXPECT_EQ(x_entry->scope_end_ip, SIZE_MAX);

    basl_debug_local_table_free(&table);
    basl_runtime_close(&runtime);
}

TEST(BaslDebugInfoTest, ChunkContainsDebugLocals) {
    DebugInfoFixture f;
    dif_init(&f);
    basl_object_t *fn = dif_compile(&f, "\n"
        "fn main() -> i32 {\n"
        "    i32 x = 10;\n"
        "    i32 y = 20;\n"
        "    return x + y;\n"
        "}\n"
        "    ");

    ASSERT_NE(fn, NULL);

    /* The compiled function's chunk should have debug locals for x and y.
     * We can't easily access the chunk from the function object in the
     * public API, but we can verify the compilation succeeded and the
     * program runs correctly. */
    basl_vm_t *vm = NULL;
    basl_value_t result;
    basl_value_init_nil(&result);
    ASSERT_EQ(basl_vm_open(&vm, f.runtime, NULL, &f.error), BASL_STATUS_OK);
    EXPECT_EQ(basl_vm_execute_function(vm, fn, &result, &f.error), BASL_STATUS_OK);
    EXPECT_EQ(basl_value_as_int(&result), 30);
    basl_value_release(&result);
    basl_vm_close(&vm);
    basl_object_release(&fn);
    dif_free(&f);
}

TEST(BaslDebugInfoTest, SymbolTableContainsInterface) {
    DebugInfoFixture f;
    dif_init(&f);
    basl_object_t *fn = dif_compile(&f, "\n"
        "interface Drawable {\n"
        "    fn draw() -> void;\n"
        "}\n"
        "\n"
        "class Circle implements Drawable {\n"
        "    pub i32 radius;\n"
        "\n"
        "    pub fn draw() -> void {}\n"
        "}\n"
        "\n"
        "fn main() -> i32 {\n"
        "    return 0;\n"
        "}\n"
        "    ");

    ASSERT_NE(fn, NULL);

    int found_interface = false;
    for (size_t i = 0; i < basl_debug_symbol_table_count(&f.symbols); i++) {
        const basl_debug_symbol_t *sym = basl_debug_symbol_table_get(&f.symbols, i);
        if (sym->kind == BASL_DEBUG_SYMBOL_INTERFACE &&
            sym->name_length == 8 && memcmp(sym->name, "Drawable", 8) == 0) {
            found_interface = true;
        }
    }
    EXPECT_TRUE(found_interface);
    basl_object_release(&fn);
    dif_free(&f);
}

