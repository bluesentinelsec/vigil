#define BASL_TEST_IMPLEMENTATION
#include "basl_test.h"

extern void register_array_tests(void);
extern void register_basl_new_tests(void);
extern void register_binding_tests(void);
extern void register_checker_tests(void);
extern void register_chunk_tests(void);
extern void register_cli_lib_tests(void);
extern void register_compiler_tests(void);
extern void register_dap_tests(void);
extern void register_debug_info_tests(void);
extern void register_debugger_tests(void);
extern void register_diagnostic_tests(void);
extern void register_doc_tests(void);
extern void register_ffi_tests(void);
extern void register_unsafe_tests(void);
extern void register_json_tests(void);
extern void register_lexer_tests(void);
extern void register_log_tests(void);
extern void register_map_tests(void);
extern void register_platform_tests(void);
extern void register_runtime_tests(void);
extern void register_source_tests(void);
extern void register_status_tests(void);
extern void register_stdlib_tests(void);
extern void register_string_tests(void);
extern void register_symbol_tests(void);
extern void register_token_tests(void);
extern void register_toml_tests(void);
extern void register_url_tests(void);
extern void register_semantic_tests(void);
extern void register_lsp_tests(void);
extern void register_doc_registry_tests(void);
extern void register_type_tests(void);
extern void register_value_tests(void);
extern void register_vm_tests(void);

int main(void) {
    register_array_tests();
    register_basl_new_tests();
    register_binding_tests();
    register_checker_tests();
    register_chunk_tests();
    register_cli_lib_tests();
    register_compiler_tests();
    register_dap_tests();
    register_debug_info_tests();
    register_debugger_tests();
    register_diagnostic_tests();
    register_doc_tests();
    register_ffi_tests();
    register_unsafe_tests();
    register_json_tests();
    register_lexer_tests();
    register_log_tests();
    register_map_tests();
    register_platform_tests();
    register_runtime_tests();
    register_source_tests();
    register_status_tests();
    register_stdlib_tests();
    register_string_tests();
    register_symbol_tests();
    register_token_tests();
    register_toml_tests();
    register_url_tests();
    register_semantic_tests();
    register_lsp_tests();
    register_doc_registry_tests();
    register_type_tests();
    register_value_tests();
    register_vm_tests();
    return basl_test_run_all_();
}

