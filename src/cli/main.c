#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "basl/basl.h"
#include "basl/cli_lib.h"
#include "basl/dap.h"
#include "basl/doc.h"
#include "basl/embed.h"
#include "basl/fmt.h"
#include "basl/package.h"
#include "basl/stdlib.h"
#include "basl/toml.h"
#include "platform/platform.h"

/* ── Shared helpers ──────────────────────────────────────────────── */

static void log_cli_message(
    basl_runtime_t *runtime,
    basl_log_level_t level,
    const char *message,
    const char *field_key,
    const char *field_value
) {
    basl_log_field_t field;
    const basl_logger_t *logger;

    logger = basl_runtime_logger(runtime);
    if (field_key != NULL && field_value != NULL) {
        field.key = field_key;
        field.value = field_value;
        (void)basl_logger_log(logger, level, message, &field, 1U, NULL);
        return;
    }
    (void)basl_logger_log(logger, level, message, NULL, 0U, NULL);
}

static void set_cli_error(
    basl_error_t *error, basl_status_t type, const char *message
) {
    if (error == NULL) return;
    basl_error_clear(error);
    error->type = type;
    error->value = message;
    error->length = message == NULL ? 0U : strlen(message);
}

static int print_diagnostics(
    const basl_source_registry_t *registry,
    const basl_diagnostic_list_t *diagnostics
) {
    size_t index;
    basl_string_t line;
    basl_runtime_t *runtime;
    basl_error_t error;

    runtime = registry == NULL ? NULL : registry->runtime;
    basl_string_init(&line, runtime);
    memset(&error, 0, sizeof(error));
    for (index = 0U; index < basl_diagnostic_list_count(diagnostics); index += 1U) {
        const basl_diagnostic_t *diagnostic;
        diagnostic = basl_diagnostic_list_get(diagnostics, index);
        if (diagnostic == NULL) continue;
        if (basl_diagnostic_format(registry, diagnostic, &line, &error) == BASL_STATUS_OK) {
            fprintf(stderr, "%s\n", basl_string_c_str(&line));
        }
    }
    basl_string_free(&line);
    return 1;
}

static void print_error(
    const basl_source_registry_t *registry,
    const char *prefix,
    const basl_error_t *error
) {
    basl_source_location_t location;
    const basl_source_file_t *source;

    if (error == NULL) {
        fprintf(stderr, "%s: unknown error\n", prefix);
        return;
    }
    if (registry != NULL && error->location.source_id != 0U) {
        location = error->location;
        if (basl_source_registry_resolve_location(registry, &location, NULL) == BASL_STATUS_OK) {
            source = basl_source_registry_get(registry, location.source_id);
            fprintf(stderr, "%s: %s:%u:%u: %s\n", prefix,
                    source == NULL ? "<unknown>" : basl_string_c_str(&source->path),
                    location.line, location.column, basl_error_message(error));
            return;
        }
    }
    fprintf(stderr, "%s: %s\n", prefix, basl_error_message(error));
}

/* ── Source loading ──────────────────────────────────────────────── */

static int path_has_basl_extension(const char *path, size_t length) {
    return path != NULL && length >= 5U &&
           memcmp(path + length - 5U, ".basl", 5U) == 0;
}

static int path_is_absolute(const char *path, size_t length) {
    if (path == NULL || length == 0U) return 0;
    if (path[0] == '/' || path[0] == '\\') return 1;
    return length >= 2U &&
           ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':';
}

static int registry_find_source_path(
    const basl_source_registry_t *registry, const char *path,
    basl_source_id_t *out_source_id
) {
    size_t index;
    if (out_source_id != NULL) *out_source_id = 0U;
    if (registry == NULL || path == NULL) return 0;
    for (index = 1U; index <= basl_source_registry_count(registry); index += 1U) {
        const basl_source_file_t *source;
        source = basl_source_registry_get(registry, (basl_source_id_t)index);
        if (source == NULL) continue;
        if (strcmp(basl_string_c_str(&source->path), path) == 0) {
            if (out_source_id != NULL) *out_source_id = source->id;
            return 1;
        }
    }
    return 0;
}

static const char *source_token_text(
    const basl_source_file_t *source, const basl_token_t *token, size_t *out_length
) {
    size_t length;
    if (out_length != NULL) *out_length = 0U;
    if (source == NULL || token == NULL) return NULL;
    length = token->span.end_offset - token->span.start_offset;
    if (out_length != NULL) *out_length = length;
    return basl_string_c_str(&source->text) + token->span.start_offset;
}

static basl_status_t resolve_import_path(
    basl_runtime_t *runtime, const char *base_path,
    const char *import_text, size_t import_length,
    basl_string_t *out_path, basl_error_t *error
) {
    size_t base_length, prefix_length;
    basl_string_clear(out_path);
    if (runtime == NULL || base_path == NULL || import_text == NULL || out_path == NULL) {
        set_cli_error(error, BASL_STATUS_INVALID_ARGUMENT, "import path inputs must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (path_is_absolute(import_text, import_length))
        return basl_string_assign(out_path, import_text, import_length, error);

    base_length = strlen(base_path);
    prefix_length = base_length;
    while (prefix_length > 0U) {
        char current = base_path[prefix_length - 1U];
        if (current == '/' || current == '\\') break;
        prefix_length -= 1U;
    }
    if (prefix_length != 0U) {
        if (basl_string_assign(out_path, base_path, prefix_length, error) != BASL_STATUS_OK)
            return error->type;
        if (basl_string_append(out_path, import_text, import_length, error) != BASL_STATUS_OK)
            return error->type;
    } else if (basl_string_assign(out_path, import_text, import_length, error) != BASL_STATUS_OK) {
        return error->type;
    }
    if (!path_has_basl_extension(basl_string_c_str(out_path), basl_string_length(out_path))) {
        if (basl_string_append_cstr(out_path, ".basl", error) != BASL_STATUS_OK)
            return error->type;
    }
    (void)runtime;
    return BASL_STATUS_OK;
}

static int register_source_tree(
    basl_source_registry_t *registry, const char *path,
    basl_source_id_t *out_source_id, basl_error_t *error
) {
    basl_runtime_t *runtime;
    basl_source_id_t source_id;
    char *file_text;
    size_t file_length;
    const basl_source_file_t *source;
    basl_token_list_t tokens;
    basl_diagnostic_list_t diagnostics;
    const basl_token_t *token;
    size_t cursor, brace_depth;

    runtime = registry == NULL ? NULL : registry->runtime;
    source_id = 0U;
    if (registry_find_source_path(registry, path, &source_id)) {
        if (out_source_id != NULL) *out_source_id = source_id;
        basl_error_clear(error);
        return 1;
    }

    if (basl_platform_read_file(NULL, path, &file_text, &file_length, error) != BASL_STATUS_OK) {
        set_cli_error(error, BASL_STATUS_INVALID_ARGUMENT, "failed to read imported source");
        return 0;
    }

    if (basl_source_registry_register(registry, path, strlen(path),
            file_text, file_length, &source_id, error) != BASL_STATUS_OK) {
        free(file_text);
        return 0;
    }
    free(file_text);
    if (out_source_id != NULL) *out_source_id = source_id;

    source = basl_source_registry_get(registry, source_id);
    if (source == NULL) {
        set_cli_error(error, BASL_STATUS_INVALID_ARGUMENT, "registered source was not found");
        return 0;
    }

    basl_token_list_init(&tokens, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    if (basl_lex_source(registry, source_id, &tokens, &diagnostics, error) != BASL_STATUS_OK) {
        basl_error_clear(error);
        basl_token_list_free(&tokens);
        basl_diagnostic_list_free(&diagnostics);
        return 1;
    }

    cursor = 0U;
    brace_depth = 0U;
    while (1) {
        token = basl_token_list_get(&tokens, cursor);
        if (token == NULL || token->kind == BASL_TOKEN_EOF) break;
        if (token->kind == BASL_TOKEN_LBRACE) { brace_depth++; cursor++; continue; }
        if (token->kind == BASL_TOKEN_RBRACE) {
            if (brace_depth != 0U) brace_depth--;
            cursor++;
            continue;
        }
        if (brace_depth == 0U && token->kind == BASL_TOKEN_IMPORT) {
            const basl_token_t *path_token;
            basl_string_t import_path;
            const char *import_text;
            size_t import_length;

            cursor++;
            path_token = basl_token_list_get(&tokens, cursor);
            if (path_token == NULL ||
                (path_token->kind != BASL_TOKEN_STRING_LITERAL &&
                 path_token->kind != BASL_TOKEN_RAW_STRING_LITERAL))
                break;

            import_text = source_token_text(source, path_token, &import_length);
            if (import_text == NULL || import_length < 2U) break;

            if (basl_stdlib_is_native_module(import_text + 1U, import_length - 2U)) {
                cursor++;
                continue;
            }

            basl_string_init(&import_path, runtime);
            if (resolve_import_path(runtime, basl_string_c_str(&source->path),
                    import_text + 1U, import_length - 2U, &import_path, error) != BASL_STATUS_OK) {
                basl_string_free(&import_path);
                basl_token_list_free(&tokens);
                basl_diagnostic_list_free(&diagnostics);
                return 0;
            }
            if (!register_source_tree(registry, basl_string_c_str(&import_path), NULL, error)) {
                basl_string_free(&import_path);
                basl_token_list_free(&tokens);
                basl_diagnostic_list_free(&diagnostics);
                return 0;
            }
            basl_string_free(&import_path);
        }
        cursor++;
    }

    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_error_clear(error);
    return 1;
}

/* ── run command ─────────────────────────────────────────────────── */

static int cmd_run(const char *script_path, const char *const *script_argv, size_t script_argc) {
    basl_runtime_t *runtime = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_value_t result;
    basl_source_id_t source_id = 0U;
    basl_object_t *function = NULL;
    basl_status_t status;
    int exit_code = 0;

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize runtime: %s\n", basl_error_message(&error));
        return 1;
    }
    if (basl_vm_open(&vm, runtime, NULL, &error) != BASL_STATUS_OK) {
        log_cli_message(runtime, BASL_LOG_ERROR, "failed to initialize vm",
                        "error", basl_error_message(&error));
        basl_runtime_close(&runtime);
        return 1;
    }

    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_value_init_nil(&result);

    if (!register_source_tree(&registry, script_path, &source_id, &error)) {
        log_cli_message(runtime, BASL_LOG_ERROR, "failed to register source",
                        "error", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    {
        basl_native_registry_t natives;
        basl_native_registry_init(&natives);
        basl_stdlib_register_all(&natives, &error);
        status = basl_compile_source_with_natives(&registry, source_id, &natives,
                                                   &function, &diagnostics, &error);
        basl_native_registry_free(&natives);
    }
    if (status != BASL_STATUS_OK) {
        if (basl_diagnostic_list_count(&diagnostics) != 0U) {
            exit_code = print_diagnostics(&registry, &diagnostics);
        } else {
            print_error(&registry, "compile failed", &error);
            exit_code = 1;
        }
        basl_object_release(&function);
        goto cleanup;
    }

    basl_vm_set_args(vm, script_argv, script_argc);
    status = basl_vm_execute_function(vm, function, &result, &error);
    basl_object_release(&function);
    if (status != BASL_STATUS_OK) {
        print_error(&registry, "execution failed", &error);
        exit_code = 1;
        goto cleanup;
    }
    if (basl_value_kind(&result) != BASL_VALUE_INT) {
        log_cli_message(runtime, BASL_LOG_ERROR,
                        "compiled entrypoint did not return i32", NULL, NULL);
        exit_code = 1;
        goto cleanup;
    }
    exit_code = (int)basl_value_as_int(&result);

cleanup:
    if (basl_diagnostic_list_count(&diagnostics) != 0U)
        print_diagnostics(&registry, &diagnostics);
    basl_value_release(&result);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    return exit_code;
}

/* ── check command ───────────────────────────────────────────────── */

static int cmd_check(const char *script_path) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_source_id_t source_id = 0U;
    basl_status_t status;
    int exit_code = 0;

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize runtime: %s\n", basl_error_message(&error));
        return 1;
    }

    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    if (!register_source_tree(&registry, script_path, &source_id, &error)) {
        log_cli_message(runtime, BASL_LOG_ERROR, "failed to register source",
                        "error", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    status = basl_check_source(&registry, source_id, &diagnostics, &error);
    if (status != BASL_STATUS_OK) {
        if (basl_diagnostic_list_count(&diagnostics) != 0U) {
            exit_code = print_diagnostics(&registry, &diagnostics);
        } else {
            print_error(&registry, "check failed", &error);
            exit_code = 1;
        }
    }

cleanup:
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
    return exit_code;
}

/* ── new command ─────────────────────────────────────────────────── */

static basl_status_t write_text_file(const char *base, const char *name,
                                      const char *content, basl_error_t *error) {
    char path[4096];
    basl_status_t s = basl_platform_path_join(base, name, path, sizeof(path), error);
    if (s != BASL_STATUS_OK) return s;
    return basl_platform_write_file(path, content, strlen(content), error);
}

static basl_status_t make_subdir(const char *base, const char *name, basl_error_t *error) {
    char path[4096];
    basl_status_t s = basl_platform_path_join(base, name, path, sizeof(path), error);
    if (s != BASL_STATUS_OK) return s;
    return basl_platform_mkdir(path, error);
}

static int cmd_new(const char *name, int is_lib) {
    basl_error_t error = {0};
    basl_toml_value_t *root = NULL;
    basl_toml_value_t *str_val = NULL;
    char *toml_str = NULL;
    size_t toml_len = 0;
    int exists = 0;
    char project_name[256];

    /* If name not provided, prompt for it. */
    if (name == NULL || name[0] == '\0') {
        if (basl_platform_readline("Project name: ", project_name,
                                    sizeof(project_name), &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            return 1;
        }
        if (project_name[0] == '\0') {
            fprintf(stderr, "error: project name cannot be empty\n");
            return 1;
        }
        name = project_name;
    }

    /* Check if directory already exists. */
    if (basl_platform_file_exists(name, &exists) == BASL_STATUS_OK && exists) {
        fprintf(stderr, "error: '%s' already exists\n", name);
        return 1;
    }

    /* Create project directory tree. */
    if (basl_platform_mkdir_p(name, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error: %s\n", basl_error_message(&error));
        return 1;
    }
    if (make_subdir(name, "lib", &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error: %s\n", basl_error_message(&error));
        return 1;
    }
    if (make_subdir(name, "test", &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error: %s\n", basl_error_message(&error));
        return 1;
    }

    /* Generate basl.toml. */
    if (basl_toml_table_new(NULL, &root, &error) != BASL_STATUS_OK) goto toml_err;
    if (basl_toml_string_new(NULL, name, strlen(name), &str_val, &error) != BASL_STATUS_OK) goto toml_err;
    if (basl_toml_table_set(root, "name", 4, str_val, &error) != BASL_STATUS_OK) {
        basl_toml_free(&str_val);
        goto toml_err;
    }
    str_val = NULL;
    if (basl_toml_string_new(NULL, "0.1.0", 5, &str_val, &error) != BASL_STATUS_OK) goto toml_err;
    if (basl_toml_table_set(root, "version", 7, str_val, &error) != BASL_STATUS_OK) {
        basl_toml_free(&str_val);
        goto toml_err;
    }
    str_val = NULL;

    if (basl_toml_emit(root, &toml_str, &toml_len, &error) != BASL_STATUS_OK) goto toml_err;
    if (write_text_file(name, "basl.toml", toml_str, &error) != BASL_STATUS_OK) {
        free(toml_str);
        goto toml_err;
    }
    free(toml_str);
    basl_toml_free(&root);

    /* Write .gitignore. */
    if (write_text_file(name, ".gitignore", "deps/\n", &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error: %s\n", basl_error_message(&error));
        return 1;
    }

    if (is_lib) {
        /* Library project. */
        char lib_file[512];
        char test_file[512];
        char lib_content[512];
        char test_content[512];

        snprintf(lib_file, sizeof(lib_file), "lib/%s.basl", name);
        snprintf(test_file, sizeof(test_file), "test/%s_test.basl", name);

        snprintf(lib_content, sizeof(lib_content),
            "/// %s library module.\n"
            "\n"
            "pub fn hello() -> string {\n"
            "    return \"hello from %s\";\n"
            "}\n", name, name);

        snprintf(test_content, sizeof(test_content),
            "import \"test\";\n"
            "import \"%s\";\n"
            "\n"
            "fn test_hello(test.T t) -> void {\n"
            "    t.assert(%s.hello() == \"hello from %s\", \"hello should match\");\n"
            "}\n", name, name, name);

        if (write_text_file(name, lib_file, lib_content, &error) != BASL_STATUS_OK ||
            write_text_file(name, test_file, test_content, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            return 1;
        }
    } else {
        /* Application project. */
        const char *main_content =
            "import \"fmt\";\n"
            "\n"
            "fn main() -> i32 {\n"
            "    fmt.println(\"hello, world!\");\n"
            "    return 0;\n"
            "}\n";

        if (write_text_file(name, "main.basl", main_content, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            return 1;
        }
    }

    printf("created %s\n", name);
    printf("  basl.toml\n");
    if (is_lib) {
        printf("  lib/%s.basl\n", name);
        printf("  test/%s_test.basl\n", name);
    } else {
        printf("  main.basl\n");
    }
    printf("  lib/\n");
    printf("  test/\n");
    printf("  .gitignore\n");

    return 0;

toml_err:
    basl_toml_free(&root);
    fprintf(stderr, "error: %s\n", basl_error_message(&error));
    return 1;
}

/* ── debug command ────────────────────────────────────────────────── */

static int cmd_debug(const char *script_path) {
    basl_runtime_t *runtime = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_source_id_t source_id = 0U;
    basl_object_t *function = NULL;
    basl_status_t status;
    basl_dap_server_t *dap = NULL;
    int exit_code = 0;

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize runtime: %s\n", basl_error_message(&error));
        return 1;
    }
    if (basl_vm_open(&vm, runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize vm: %s\n", basl_error_message(&error));
        basl_runtime_close(&runtime);
        return 1;
    }

    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    if (!register_source_tree(&registry, script_path, &source_id, &error)) {
        fprintf(stderr, "failed to register source: %s\n", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Compile. */
    {
        basl_native_registry_t natives;
        basl_native_registry_init(&natives);
        basl_stdlib_register_all(&natives, &error);
        status = basl_compile_source_with_natives(&registry, source_id, &natives,
                                                   &function, &diagnostics, &error);
        basl_native_registry_free(&natives);
    }
    if (status != BASL_STATUS_OK) {
        if (basl_diagnostic_list_count(&diagnostics) != 0U) {
            print_diagnostics(&registry, &diagnostics);
        } else {
            print_error(&registry, "compile failed", &error);
        }
        exit_code = 1;
        goto cleanup;
    }

    /* Create DAP server on stdio. */
    if (basl_dap_server_create(&dap, stdin, stdout, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to create DAP server: %s\n", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    if (basl_dap_server_set_runtime(dap, vm, &registry, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to attach DAP server: %s\n", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    basl_dap_server_set_program(dap, function, source_id);

    /* Run the DAP message loop (blocks until disconnect). */
    status = basl_dap_server_run(dap, &error);
    if (status != BASL_STATUS_OK && status != BASL_STATUS_INTERNAL) {
        fprintf(stderr, "DAP server error: %s\n", basl_error_message(&error));
        exit_code = 1;
    }

cleanup:
    basl_dap_server_destroy(&dap);
    basl_object_release(&function);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    return exit_code;
}

/* ── doc command ──────────────────────────────────────────────────── */

static int cmd_doc(const char *file_path, const char *symbol) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;
    basl_token_list_t tokens;
    basl_diagnostic_list_t diagnostics;
    const basl_source_file_t *source;
    basl_doc_module_t doc_module;
    char *output = NULL;
    size_t output_len = 0;
    int exit_code = 0;

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize runtime: %s\n", basl_error_message(&error));
        return 1;
    }

    basl_source_registry_init(&registry, runtime);
    basl_token_list_init(&tokens, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    /* Read and register source. */
    {
        char *file_text = NULL;
        size_t file_length = 0;
        if (basl_platform_read_file(NULL, file_path, &file_text, &file_length, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
        if (basl_source_registry_register(&registry, file_path, strlen(file_path),
                file_text, file_length, &source_id, &error) != BASL_STATUS_OK) {
            free(file_text);
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
        free(file_text);
    }

    source = basl_source_registry_get(&registry, source_id);
    if (source == NULL) {
        fprintf(stderr, "error: failed to retrieve source\n");
        exit_code = 1;
        goto cleanup;
    }

    /* Lex. */
    if (basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error: %s\n", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Extract. */
    if (basl_doc_extract(NULL, file_path, strlen(file_path),
            basl_string_c_str(&source->text), basl_string_length(&source->text),
            &tokens, &doc_module, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error: %s\n", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Render. */
    if (basl_doc_render(&doc_module, symbol, &output, &output_len, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error[doc]: %s\n", basl_error_message(&error));
        basl_doc_module_free(&doc_module);
        exit_code = 1;
        goto cleanup;
    }

    if (output != NULL) {
        fwrite(output, 1, output_len, stdout);
        if (output_len > 0 && output[output_len - 1] != '\n')
            fputc('\n', stdout);
        free(output);
    }

    basl_doc_module_free(&doc_module);

cleanup:
    basl_diagnostic_list_free(&diagnostics);
    basl_token_list_free(&tokens);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
    return exit_code;
}

/* ── packaged binary runner ───────────────────────────────────────── */

static int try_run_packaged(int argc, char **argv) {
    basl_package_bundle_t bundle;
    basl_error_t error = {0};
    basl_status_t status;
    size_t i;
    const char *entry_src = NULL;

    status = basl_package_read_self(&bundle, &error);
    if (status != BASL_STATUS_OK) {
        /* Check if it's just "not a packaged binary" vs a real error. */
        if (error.value != NULL && (
            strcmp(basl_error_message(&error), "not a packaged binary") == 0 ||
            strcmp(basl_error_message(&error), "no bundle trailer") == 0))
            return -1; /* not packaged, continue as normal CLI */
        fprintf(stderr, "error[package]: %s\n", basl_error_message(&error));
        return 1;
    }

    /* Find entry.basl. */
    for (i = 0; i < bundle.file_count; i++) {
        if (strcmp(bundle.paths[i], "entry.basl") == 0) {
            entry_src = bundle.contents[i];
            break;
        }
    }
    if (entry_src == NULL) {
        fprintf(stderr, "error[package]: entry.basl not found in bundle\n");
        basl_package_bundle_free(&bundle);
        return 1;
    }

    /* Compile and run. */
    {
        basl_runtime_t *runtime = NULL;
        basl_vm_t *vm = NULL;
        basl_source_registry_t registry;
        basl_diagnostic_list_t diagnostics;
        basl_source_id_t source_id = 0;
        basl_object_t *function = NULL;
        basl_value_t result;
        int exit_code = 0;

        if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            basl_package_bundle_free(&bundle);
            return 1;
        }
        if (basl_vm_open(&vm, runtime, NULL, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            basl_runtime_close(&runtime);
            basl_package_bundle_free(&bundle);
            return 1;
        }

        basl_source_registry_init(&registry, runtime);
        basl_diagnostic_list_init(&diagnostics, runtime);
        basl_value_init_nil(&result);

        /* Register all bundled files as sources. */
        for (i = 0; i < bundle.file_count; i++) {
            basl_source_id_t sid = 0;
            basl_source_registry_register(&registry,
                bundle.paths[i], strlen(bundle.paths[i]),
                bundle.contents[i], bundle.content_lengths[i],
                &sid, &error);
            if (strcmp(bundle.paths[i], "entry.basl") == 0)
                source_id = sid;
        }

        {
            basl_native_registry_t natives;
            basl_native_registry_init(&natives);
            basl_stdlib_register_all(&natives, &error);
            status = basl_compile_source_with_natives(&registry, source_id, &natives,
                                                       &function, &diagnostics, &error);
            basl_native_registry_free(&natives);
        }
        if (status != BASL_STATUS_OK) {
            if (basl_diagnostic_list_count(&diagnostics) != 0U)
                print_diagnostics(&registry, &diagnostics);
            else
                print_error(&registry, "compile failed", &error);
            exit_code = 1;
        } else {
            /* Pass remaining argv as script args. */
            if (argc > 1)
                basl_vm_set_args(vm, (const char *const *)&argv[1], (size_t)(argc - 1));
            else
                basl_vm_set_args(vm, NULL, 0);

            status = basl_vm_execute_function(vm, function, &result, &error);
            if (status != BASL_STATUS_OK) {
                print_error(&registry, "execution failed", &error);
                exit_code = 1;
            } else if (basl_value_kind(&result) == BASL_VALUE_INT) {
                exit_code = (int)basl_value_as_int(&result);
            }
        }

        basl_object_release(&function);
        basl_value_release(&result);
        basl_diagnostic_list_free(&diagnostics);
        basl_source_registry_free(&registry);
        basl_vm_close(&vm);
        basl_runtime_close(&runtime);
        basl_package_bundle_free(&bundle);
        return exit_code;
    }
}

/* ── fmt command ─────────────────────────────────────────────────── */

static int fmt_one_file(const char *file_path, int check_only) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;
    basl_token_list_t tokens;
    basl_diagnostic_list_t diagnostics;
    const basl_source_file_t *source;
    char *formatted = NULL;
    size_t formatted_len = 0;
    int exit_code = 0;

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize runtime: %s\n", basl_error_message(&error));
        return 1;
    }

    basl_source_registry_init(&registry, runtime);
    basl_token_list_init(&tokens, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    /* Read and register source. */
    {
        char *file_text = NULL;
        size_t file_length = 0;
        if (basl_platform_read_file(NULL, file_path, &file_text, &file_length, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
        if (basl_source_registry_register(&registry, file_path, strlen(file_path),
                file_text, file_length, &source_id, &error) != BASL_STATUS_OK) {
            free(file_text);
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            exit_code = 1;
            goto cleanup;
        }
        free(file_text);
    }

    source = basl_source_registry_get(&registry, source_id);
    if (source == NULL) {
        fprintf(stderr, "error: failed to retrieve source\n");
        exit_code = 1;
        goto cleanup;
    }

    /* Lex. */
    if (basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error[fmt]: %s: %s\n", file_path, basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Format. */
    if (basl_fmt(basl_string_c_str(&source->text), basl_string_length(&source->text),
                 &tokens, &formatted, &formatted_len, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error[fmt]: %s: %s\n", file_path, basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    /* Compare. */
    {
        const char *orig = basl_string_c_str(&source->text);
        size_t orig_len = basl_string_length(&source->text);
        if (formatted_len == orig_len && memcmp(orig, formatted, orig_len) == 0) {
            /* Already formatted. */
            goto cleanup;
        }
    }

    if (check_only) {
        /* In check mode, print the filename and return failure. */
        fprintf(stderr, "%s\n", file_path);
        exit_code = 1;
    } else {
        /* Write back. */
        if (basl_platform_write_file(file_path, formatted, formatted_len, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            exit_code = 1;
        }
    }

cleanup:
    free(formatted);
    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
    return exit_code;
}

static int cmd_fmt(const char *file_path, int check_only) {
    return fmt_one_file(file_path, check_only);
}

/* ── shared directory listing helpers ─────────────────────────────── */

typedef struct { char name[256]; int is_dir; } dir_entry_t;
typedef struct { dir_entry_t *items; size_t count; size_t cap; } dir_list_t;

static basl_status_t dir_list_cb(const char *name, int is_dir, void *ud) {
    dir_list_t *dl = ud;
    if (dl->count == dl->cap) {
        dl->cap = dl->cap ? dl->cap * 2 : 32;
        dl->items = realloc(dl->items, dl->cap * sizeof(dir_entry_t));
    }
    snprintf(dl->items[dl->count].name, sizeof(dl->items[0].name), "%s", name);
    dl->items[dl->count].is_dir = is_dir;
    dl->count++;
    return BASL_STATUS_OK;
}

/* ── test command ────────────────────────────────────────────────── */

/* Simple string list for collecting test file paths and function names. */
typedef struct { char **items; size_t count; size_t cap; } str_list_t;

static void sl_add(str_list_t *sl, const char *s) {
    if (sl->count == sl->cap) {
        sl->cap = sl->cap ? sl->cap * 2 : 16;
        sl->items = realloc(sl->items, sl->cap * sizeof(char *));
    }
    sl->items[sl->count++] = strdup(s);
}

static void sl_free(str_list_t *sl) {
    for (size_t i = 0; i < sl->count; i++) free(sl->items[i]);
    free(sl->items);
    memset(sl, 0, sizeof(*sl));
}

/* Walk a directory recursively collecting *_test.basl files. */
static void collect_test_files(str_list_t *out, const char *dir) {
    basl_error_t err = {0};
    dir_list_t dl = {NULL, 0, 0};
    if (basl_platform_list_dir(dir, dir_list_cb, &dl, &err) != BASL_STATUS_OK) {
        free(dl.items); return;
    }
    for (size_t i = 0; i < dl.count; i++) {
        char full[4096];
        basl_platform_path_join(dir, dl.items[i].name, full, sizeof(full), &err);
        if (dl.items[i].is_dir) {
            collect_test_files(out, full);
        } else {
            size_t len = strlen(dl.items[i].name);
            if (len > 10 && strcmp(dl.items[i].name + len - 10, "_test.basl") == 0)
                sl_add(out, full);
        }
    }
    free(dl.items);
}

/* Scan a source file for top-level `fn test_*(...` function names. */
static void scan_test_functions(
    basl_runtime_t *runtime,
    basl_source_registry_t *registry,
    basl_source_id_t source_id,
    str_list_t *out
) {
    basl_token_list_t tokens;
    basl_diagnostic_list_t diags;
    const basl_source_file_t *source;
    size_t cursor = 0;
    size_t brace_depth = 0;

    source = basl_source_registry_get(registry, source_id);
    if (!source) return;

    basl_token_list_init(&tokens, runtime);
    basl_diagnostic_list_init(&diags, runtime);
    if (basl_lex_source(registry, source_id, &tokens, &diags, NULL) != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        basl_diagnostic_list_free(&diags);
        return;
    }

    while (1) {
        const basl_token_t *tok = basl_token_list_get(&tokens, cursor);
        if (!tok || tok->kind == BASL_TOKEN_EOF) break;
        if (tok->kind == BASL_TOKEN_LBRACE) { brace_depth++; cursor++; continue; }
        if (tok->kind == BASL_TOKEN_RBRACE) {
            if (brace_depth) brace_depth--;
            cursor++;
            continue;
        }
        if (brace_depth == 0 && tok->kind == BASL_TOKEN_FN) {
            const basl_token_t *name_tok = basl_token_list_get(&tokens, cursor + 1);
            if (name_tok && name_tok->kind == BASL_TOKEN_IDENTIFIER) {
                size_t len;
                const char *text = source_token_text(source, name_tok, &len);
                if (text && len > 5 && memcmp(text, "test_", 5) == 0) {
                    char buf[256];
                    if (len < sizeof(buf)) {
                        memcpy(buf, text, len);
                        buf[len] = '\0';
                        sl_add(out, buf);
                    }
                }
            }
        }
        cursor++;
    }

    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diags);
}

/* Check if filter matches a test name (|-separated substrings). */
static int test_matches_filter(const char *name, const char *filter) {
    const char *p = filter;
    while (*p) {
        const char *end = strchr(p, '|');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        /* Trim leading/trailing spaces. */
        while (len > 0 && p[0] == ' ') { p++; len--; }
        while (len > 0 && p[len - 1] == ' ') len--;
        if (len > 0 && len < 256) {
            char part[256];
            memcpy(part, p, len);
            part[len] = '\0';
            if (strstr(name, part)) return 1;
        }
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

/* Run a single test function by creating a synthetic main() wrapper. */
static int run_one_test(
    const char *test_file_path,
    const char *original_source,
    size_t original_length,
    const char *test_name,
    char *err_msg,
    size_t err_msg_size
) {
    basl_runtime_t *runtime = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_value_t result;
    basl_source_id_t source_id = 0;
    basl_object_t *function = NULL;
    basl_status_t status;
    int exit_code = 0;

    /* Build synthetic source: original + main() that calls the test function. */
    char wrapper[512];
    snprintf(wrapper, sizeof(wrapper),
        "\nfn main() -> i32 {\n"
        "    test.T t = test.T();\n"
        "    %s(t);\n"
        "    return 0;\n"
        "}\n", test_name);

    size_t wrapper_len = strlen(wrapper);
    size_t total_len = original_length + wrapper_len;
    char *combined = malloc(total_len + 1);
    if (!combined) {
        snprintf(err_msg, err_msg_size, "out of memory");
        return 1;
    }
    memcpy(combined, original_source, original_length);
    memcpy(combined + original_length, wrapper, wrapper_len);
    combined[total_len] = '\0';

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        snprintf(err_msg, err_msg_size, "runtime init failed");
        free(combined);
        return 1;
    }
    if (basl_vm_open(&vm, runtime, NULL, &error) != BASL_STATUS_OK) {
        free(combined);
        basl_runtime_close(&runtime);
        snprintf(err_msg, err_msg_size, "vm init failed");
        return 1;
    }

    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_value_init_nil(&result);

    /* Register the combined source under the test file's path so imports resolve. */
    if (basl_source_registry_register(&registry, test_file_path, strlen(test_file_path),
            combined, total_len, &source_id, &error) != BASL_STATUS_OK) {
        snprintf(err_msg, err_msg_size, "source registration failed");
        exit_code = 1;
        goto cleanup;
    }

    /* Recursively register imports. */
    {
        const basl_source_file_t *source = basl_source_registry_get(&registry, source_id);
        basl_token_list_t tokens;
        size_t cursor = 0, brace_depth = 0;

        basl_token_list_init(&tokens, runtime);
        basl_diagnostic_list_init(&diagnostics, runtime);
        if (basl_lex_source(&registry, source_id, &tokens, &diagnostics, &error) == BASL_STATUS_OK) {
            while (1) {
                const basl_token_t *tok = basl_token_list_get(&tokens, cursor);
                if (!tok || tok->kind == BASL_TOKEN_EOF) break;
                if (tok->kind == BASL_TOKEN_LBRACE) { brace_depth++; cursor++; continue; }
                if (tok->kind == BASL_TOKEN_RBRACE) {
                    if (brace_depth) brace_depth--;
                    cursor++;
                    continue;
                }
                if (brace_depth == 0 && tok->kind == BASL_TOKEN_IMPORT) {
                    const basl_token_t *path_tok = basl_token_list_get(&tokens, cursor + 1);
                    if (path_tok && (path_tok->kind == BASL_TOKEN_STRING_LITERAL ||
                                     path_tok->kind == BASL_TOKEN_RAW_STRING_LITERAL)) {
                        size_t import_len;
                        const char *import_text = source_token_text(source, path_tok, &import_len);
                        if (import_text && import_len >= 2 &&
                            !basl_stdlib_is_native_module(import_text + 1, import_len - 2)) {
                            basl_string_t import_path;
                            basl_string_init(&import_path, runtime);
                            if (resolve_import_path(runtime, basl_string_c_str(&source->path),
                                    import_text + 1, import_len - 2, &import_path, &error) == BASL_STATUS_OK) {
                                register_source_tree(&registry, basl_string_c_str(&import_path), NULL, &error);
                            }
                            basl_string_free(&import_path);
                        }
                    }
                }
                cursor++;
            }
        }
        basl_token_list_free(&tokens);
    }

    {
        basl_native_registry_t natives;
        basl_native_registry_init(&natives);
        basl_stdlib_register_all(&natives, &error);
        status = basl_compile_source_with_natives(&registry, source_id, &natives,
                                                   &function, &diagnostics, &error);
        basl_native_registry_free(&natives);
    }
    if (status != BASL_STATUS_OK) {
        if (basl_diagnostic_list_count(&diagnostics) != 0U) {
            /* Capture first diagnostic as error message. */
            snprintf(err_msg, err_msg_size, "compile error");
        } else {
            snprintf(err_msg, err_msg_size, "%s", basl_error_message(&error));
        }
        basl_object_release(&function);
        exit_code = 1;
        goto cleanup;
    }

    status = basl_vm_execute_function(vm, function, &result, &error);
    basl_object_release(&function);
    if (status != BASL_STATUS_OK) {
        snprintf(err_msg, err_msg_size, "%s", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

cleanup:
    basl_value_release(&result);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    free(combined);
    return exit_code;
}

static int cmd_test(int argc, char **argv) {
    int verbose = 0;
    const char *filter = NULL;
    str_list_t targets = {0};
    str_list_t test_files = {0};
    int total_pass = 0, total_fail = 0;
    int exit_code = 0;

    /* Parse args. */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-run") == 0 || strcmp(argv[i], "--run") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: -run requires a pattern\n");
                return 2;
            }
            filter = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: basl test [--run pattern] [-v] [path...]\n\n"
                   "Recursively finds and runs BASL test files (*_test.basl).\n"
                   "In a project root, defaults to the ./test directory.\n\n"
                   "Flags:\n"
                   "  -v, --verbose    Print passing tests\n"
                   "  -run, --run      Filter test names by substring\n");
            return 0;
        } else {
            sl_add(&targets, argv[i]);
        }
    }

    /* Default targets: if cwd has basl.toml and ./test exists, use ./test; else "." */
    if (targets.count == 0) {
        int is_dir = 0;
        if (basl_platform_is_directory("test", &is_dir) == BASL_STATUS_OK && is_dir) {
            FILE *f = fopen("basl.toml", "r");
            if (f) {
                fclose(f);
                sl_add(&targets, "test");
            } else {
                sl_add(&targets, ".");
            }
        } else {
            sl_add(&targets, ".");
        }
    }

    /* Collect _test.basl files. */
    for (size_t i = 0; i < targets.count; i++) {
        int is_dir = 0;
        basl_platform_is_directory(targets.items[i], &is_dir);
        if (is_dir) {
            collect_test_files(&test_files, targets.items[i]);
        } else {
            size_t len = strlen(targets.items[i]);
            if (len > 10 && strcmp(targets.items[i] + len - 10, "_test.basl") == 0)
                sl_add(&test_files, targets.items[i]);
        }
    }

    if (test_files.count == 0) {
        printf("no test files found\n");
        sl_free(&targets);
        sl_free(&test_files);
        return 0;
    }

    /* Run tests. */
    for (size_t fi = 0; fi < test_files.count; fi++) {
        const char *tf = test_files.items[fi];
        char *source = NULL;
        size_t source_len = 0;
        basl_error_t error = {0};

        if (basl_platform_read_file(NULL, tf, &source, &source_len, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s: %s\n", tf, basl_error_message(&error));
            exit_code = 1;
            continue;
        }

        /* Discover test function names. */
        str_list_t fn_names = {0};
        {
            basl_runtime_t *rt = NULL;
            basl_source_registry_t reg;
            basl_source_id_t sid = 0;
            if (basl_runtime_open(&rt, NULL, NULL) == BASL_STATUS_OK) {
                basl_source_registry_init(&reg, rt);
                if (basl_source_registry_register(&reg, tf, strlen(tf),
                        source, source_len, &sid, NULL) == BASL_STATUS_OK) {
                    scan_test_functions(rt, &reg, sid, &fn_names);
                }
                basl_source_registry_free(&reg);
                basl_runtime_close(&rt);
            }
        }

        int file_failed = 0;
        clock_t file_start = clock();

        for (size_t ti = 0; ti < fn_names.count; ti++) {
            const char *name = fn_names.items[ti];
            if (filter && !test_matches_filter(name, filter)) continue;

            clock_t t_start = clock();

            char err_msg[512] = {0};
            int result = run_one_test(tf, source, source_len, name, err_msg, sizeof(err_msg));

            double elapsed = (double)(clock() - t_start) / CLOCKS_PER_SEC;

            if (result == 0) {
                total_pass++;
                if (verbose)
                    printf("=== RUN   %s\n--- PASS: %s (%.3fs)\n", name, name, elapsed);
            } else {
                total_fail++;
                file_failed = 1;
                printf("--- FAIL: %s (%s)\n    %s\n", name, tf, err_msg);
            }
        }

        double file_elapsed = (double)(clock() - file_start) / CLOCKS_PER_SEC;

        if (file_failed) {
            printf("FAIL\t%s\t%.3fs\n", tf, file_elapsed);
            exit_code = 1;
        } else if (fn_names.count > 0) {
            printf("ok  \t%s\t%.3fs\n", tf, file_elapsed);
        }

        sl_free(&fn_names);
        free(source);
    }

    if (total_fail > 0)
        printf("\nFAIL: %d passed, %d failed\n", total_pass, total_fail);
    else if (total_pass > 0)
        printf("\nPASS: %d passed\n", total_pass);

    sl_free(&targets);
    sl_free(&test_files);
    return exit_code;
}

/* ── embed command ───────────────────────────────────────────────── */

typedef struct { char **paths; char **rels; size_t count; size_t cap; } file_list_t;

static void fl_add(file_list_t *fl, const char *path, const char *rel) {
    if (fl->count == fl->cap) {
        fl->cap = fl->cap ? fl->cap * 2 : 16;
        fl->paths = realloc(fl->paths, fl->cap * sizeof(char *));
        fl->rels = realloc(fl->rels, fl->cap * sizeof(char *));
    }
    fl->paths[fl->count] = strdup(path);
    fl->rels[fl->count] = strdup(rel);
    fl->count++;
}

static void fl_free(file_list_t *fl) {
    for (size_t i = 0; i < fl->count; i++) { free(fl->paths[i]); free(fl->rels[i]); }
    free(fl->paths); free(fl->rels);
}

static void collect_dir(file_list_t *fl, const char *dir, const char *rel_prefix) {
    basl_error_t err = {0};
    dir_list_t dl = {NULL, 0, 0};
    if (basl_platform_list_dir(dir, dir_list_cb, &dl, &err) != BASL_STATUS_OK) {
        free(dl.items); return;
    }
    for (size_t i = 0; i < dl.count; i++) {
        char full[4096], rel[4096];
        basl_platform_path_join(dir, dl.items[i].name, full, sizeof(full), &err);
        if (rel_prefix[0])
            snprintf(rel, sizeof(rel), "%s/%s", rel_prefix, dl.items[i].name);
        else
            snprintf(rel, sizeof(rel), "%s", dl.items[i].name);
        if (dl.items[i].is_dir) collect_dir(fl, full, rel);
        else fl_add(fl, full, rel);
    }
    free(dl.items);
}

static int cmd_embed(int argc, char **argv) {
    const char *output = NULL;
    const char **targets = NULL;
    size_t target_count = 0;
    basl_error_t error = {0};

    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc)
            output = argv[++i];
        else {
            targets = realloc(targets, (target_count + 1) * sizeof(char *));
            targets[target_count++] = argv[i];
        }
    }
    if (target_count == 0) {
        fprintf(stderr, "usage: basl embed <file|dir...> [-o output.basl]\n");
        free(targets); return 2;
    }

    /* Collect files: expand directories recursively. */
    file_list_t fl = {NULL, NULL, 0, 0};
    for (size_t i = 0; i < target_count; i++) {
        int is_dir = 0;
        basl_platform_is_directory(targets[i], &is_dir);
        if (is_dir) {
            collect_dir(&fl, targets[i], "");
        } else {
            /* Use basename as relative path. */
            const char *base = targets[i];
            for (const char *p = targets[i]; *p; p++)
                if (*p == '/' || *p == '\\') base = p + 1;
            fl_add(&fl, targets[i], base);
        }
    }

    if (fl.count == 0) {
        fprintf(stderr, "error: no files found\n");
        fl_free(&fl); free(targets); return 1;
    }

    char *text = NULL;
    size_t text_len = 0;
    basl_status_t status;

    if (fl.count == 1 && target_count == 1) {
        int is_dir = 0;
        basl_platform_is_directory(targets[0], &is_dir);
        if (!is_dir) {
            status = basl_embed_single(fl.paths[0], &text, &text_len, &error);
        } else {
            status = basl_embed_multi((const char **)fl.paths, (const char **)fl.rels,
                                      fl.count, &text, &text_len, &error);
        }
    } else {
        status = basl_embed_multi((const char **)fl.paths, (const char **)fl.rels,
                                  fl.count, &text, &text_len, &error);
    }

    if (status != BASL_STATUS_OK) {
        fprintf(stderr, "error: %s\n", basl_error_message(&error));
        fl_free(&fl); free(targets); return 1;
    }

    /* Determine output path. */
    char out_path[4096];
    if (output) {
        snprintf(out_path, sizeof(out_path), "%s", output);
    } else if (fl.count == 1 && target_count == 1) {
        /* Single file: strip ext, add .basl */
        const char *base = fl.rels[0];
        const char *dot = strrchr(base, '.');
        size_t nlen = dot ? (size_t)(dot - base) : strlen(base);
        snprintf(out_path, sizeof(out_path), "%.*s.basl", (int)nlen, base);
    } else {
        snprintf(out_path, sizeof(out_path), "assets.basl");
    }

    if (basl_platform_write_file(out_path, text, text_len, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error: %s\n", basl_error_message(&error));
        free(text); fl_free(&fl); free(targets); return 1;
    }

    printf("embedded %zu file(s) -> %s\n", fl.count, out_path);
    free(text); fl_free(&fl); free(targets);
    return 0;
}

/* ── package command ─────────────────────────────────────────────── */

static int cmd_package(const char *entry_path, const char *output_path,
                        const char *key, int inspect) {
    basl_error_t error = {0};

    if (inspect) {
        /* Inspect mode. */
        const char *target = entry_path ? entry_path : output_path;
        basl_package_bundle_t bundle;
        size_t i;
        if (target == NULL) {
            fprintf(stderr, "error: --inspect requires a binary path\n");
            return 2;
        }
        if (basl_package_read(target, &bundle, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error[package]: %s\n", basl_error_message(&error));
            return 1;
        }
        printf("ENTRY\n  entry.basl\n\nFILES\n");
        for (i = 0; i < bundle.file_count; i++)
            printf("  %s\n", bundle.paths[i]);
        basl_package_bundle_free(&bundle);
        return 0;
    }

    /* Build mode. */
    {
        basl_runtime_t *runtime = NULL;
        basl_source_registry_t registry;
        basl_source_id_t source_id = 0;
        const char *script_path;
        char out_path[4096];
        basl_package_file_t *pkg_files = NULL;
        size_t pkg_count = 0;
        size_t pkg_cap = 8;
        size_t i;

        script_path = entry_path ? entry_path : "main.basl";

        if (output_path != NULL) {
            snprintf(out_path, sizeof(out_path), "%s", output_path);
        } else {
            /* Derive from script name. */
            const char *base = script_path;
            const char *p;
            size_t blen;
            for (p = script_path; *p; p++) {
                if (*p == '/' || *p == '\\') base = p + 1;
            }
            blen = strlen(base);
            if (blen > 5 && memcmp(base + blen - 5, ".basl", 5) == 0)
                blen -= 5;
            snprintf(out_path, sizeof(out_path), "%.*s", (int)blen, base);
        }

        if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            return 1;
        }

        basl_source_registry_init(&registry, runtime);

        /* Register source tree (walks imports). */
        if (!register_source_tree(&registry, script_path, &source_id, &error)) {
            fprintf(stderr, "error: %s\n", basl_error_message(&error));
            basl_source_registry_free(&registry);
            basl_runtime_close(&runtime);
            return 1;
        }

        /* Collect all registered sources as package files. */
        pkg_cap = basl_source_registry_count(&registry) + 1;
        pkg_files = (basl_package_file_t *)calloc(pkg_cap, sizeof(basl_package_file_t));
        if (pkg_files == NULL) {
            basl_source_registry_free(&registry);
            basl_runtime_close(&runtime);
            return 1;
        }

        for (i = 1; i <= basl_source_registry_count(&registry); i++) {
            const basl_source_file_t *src = basl_source_registry_get(&registry, (basl_source_id_t)i);
            if (src == NULL) continue;

            if ((basl_source_id_t)i == source_id) {
                pkg_files[pkg_count].path = "entry.basl";
                pkg_files[pkg_count].path_length = 10;
            } else {
                pkg_files[pkg_count].path = basl_string_c_str(&src->path);
                pkg_files[pkg_count].path_length = basl_string_length(&src->path);
            }
            pkg_files[pkg_count].data = basl_string_c_str(&src->text);
            pkg_files[pkg_count].data_length = basl_string_length(&src->text);
            pkg_count++;
        }

        if (basl_package_build(out_path, pkg_files, pkg_count,
                                key, key ? strlen(key) : 0, &error) != BASL_STATUS_OK) {
            fprintf(stderr, "error[package]: %s\n", basl_error_message(&error));
            free(pkg_files);
            basl_source_registry_free(&registry);
            basl_runtime_close(&runtime);
            return 1;
        }

        printf("packaged %zu file(s) -> %s\n", pkg_count, out_path);
        free(pkg_files);
        basl_source_registry_free(&registry);
        basl_runtime_close(&runtime);
        return 0;
    }
}

/* ── main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    basl_cli_t cli;
    const char *check_file = NULL;
    const char *new_name = NULL;
    const char *debug_file = NULL;
    const char *doc_file = NULL;
    const char *doc_symbol = NULL;
    const char *fmt_file = NULL;
    int fmt_check = 0;
    const char *pkg_entry = NULL;
    const char *pkg_output = NULL;
    const char *pkg_key = NULL;
    int pkg_inspect = 0;
    int new_lib = 0;
    basl_error_t error = {0};
    const basl_cli_command_t *matched;
    basl_cli_command_t *cmd;

    /* Check if this is a packaged binary. */
    {
        int rc = try_run_packaged(argc, argv);
        if (rc >= 0) return rc;
    }

    /* Handle "basl run <file> [args...]" before CLI parser since run
     * needs to pass through arbitrary script arguments. */
    if (argc >= 3 && strcmp(argv[1], "run") == 0) {
        const char *const *script_argv = argc > 3 ? (const char *const *)&argv[3] : NULL;
        size_t script_argc = argc > 3 ? (size_t)(argc - 3) : 0;
        return cmd_run(argv[2], script_argv, script_argc);
    }

    /* Handle "basl embed <file|dir...> [-o output]" before CLI parser
     * since embed needs rest-args (multiple file targets). */
    if (argc >= 3 && strcmp(argv[1], "embed") == 0) {
        return cmd_embed(argc, argv);
    }

    /* Handle "basl test [flags...] [path...]" before CLI parser. */
    if (argc >= 2 && strcmp(argv[1], "test") == 0) {
        return cmd_test(argc, argv);
    }

    basl_cli_init(&cli, "basl", "Blazingly Awesome Scripting Language");

    cmd = basl_cli_add_command(&cli, "run", "Run a BASL script");
    basl_cli_add_positional(cmd, "file", "Script file to run", NULL);

    cmd = basl_cli_add_command(&cli, "check", "Type-check a BASL script");
    basl_cli_add_positional(cmd, "file", "Script file to check", &check_file);

    cmd = basl_cli_add_command(&cli, "new", "Create a new BASL project");
    basl_cli_add_positional(cmd, "name", "Project name", &new_name);
    basl_cli_add_bool_flag(cmd, "lib", 'l', "Create a library project", &new_lib);

    cmd = basl_cli_add_command(&cli, "debug", "Start DAP debug server for a BASL script");
    basl_cli_add_positional(cmd, "file", "Script file to debug", &debug_file);

    cmd = basl_cli_add_command(&cli, "doc", "Show public API documentation for a BASL source file");
    basl_cli_add_positional(cmd, "file", "Source file to document", &doc_file);
    basl_cli_add_positional(cmd, "symbol", "Symbol to look up (e.g. Point or Point.x)", &doc_symbol);

    cmd = basl_cli_add_command(&cli, "fmt", "Format BASL source files");
    basl_cli_add_positional(cmd, "file", "Source file to format", &fmt_file);
    basl_cli_add_bool_flag(cmd, "check", 'c', "Check formatting without rewriting", &fmt_check);

    cmd = basl_cli_add_command(&cli, "package", "Package a BASL program as a standalone binary");
    basl_cli_add_positional(cmd, "entry", "Entry script or project directory", &pkg_entry);
    basl_cli_add_string_flag(cmd, "output", 'o', "Output path", &pkg_output);
    basl_cli_add_string_flag(cmd, "key", 'k', "XOR encryption key for obfuscation", &pkg_key);
    basl_cli_add_bool_flag(cmd, "inspect", 'i', "Inspect a packaged binary", &pkg_inspect);

    if (basl_cli_parse(&cli, argc, argv, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "error: %s\n", basl_error_message(&error));
        basl_cli_free(&cli);
        return 2;
    }

    matched = basl_cli_matched_command(&cli);
    if (matched == NULL) {
        basl_cli_print_help(&cli);
        basl_cli_free(&cli);
        return 0;
    }

    /* Save matched command name before freeing CLI (name is a string literal). */
    {
        const char *matched_name = matched->name;
        basl_cli_free(&cli);

        if (strcmp(matched_name, "run") == 0) {
            /* Handled above before CLI parse. */
            return 0;
        }
        if (strcmp(matched_name, "check") == 0) {
            if (check_file == NULL) {
                fprintf(stderr, "error: missing file argument\n");
                return 2;
            }
            return cmd_check(check_file);
        }
        if (strcmp(matched_name, "new") == 0) {
            return cmd_new(new_name, new_lib);
        }
        if (strcmp(matched_name, "debug") == 0) {
            if (debug_file == NULL) {
                fprintf(stderr, "error: missing file argument\n");
                return 2;
            }
            return cmd_debug(debug_file);
        }
        if (strcmp(matched_name, "doc") == 0) {
            if (doc_file == NULL) {
                fprintf(stderr, "error: missing file argument\n");
                return 2;
            }
            return cmd_doc(doc_file, doc_symbol);
        }
        if (strcmp(matched_name, "fmt") == 0) {
            if (fmt_file == NULL) {
                fprintf(stderr, "error: missing file argument\n");
                return 2;
            }
            return cmd_fmt(fmt_file, fmt_check);
        }
        if (strcmp(matched_name, "package") == 0) {
            return cmd_package(pkg_entry, pkg_output, pkg_key, pkg_inspect);
        }
    }

    return 0;
}
