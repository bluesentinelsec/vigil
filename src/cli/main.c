#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/basl.h"

typedef enum basl_cli_mode {
    BASL_CLI_MODE_RUN = 0,
    BASL_CLI_MODE_CHECK = 1
} basl_cli_mode_t;

static void basl_log_cli_message(
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
        (void)basl_logger_log(
            logger,
            level,
            message,
            &field,
            1U,
            NULL
        );
        return;
    }

    (void)basl_logger_log(logger, level, message, NULL, 0U, NULL);
}

static FILE *basl_open_file(const char *path, const char *mode) {
#ifdef _WIN32
    FILE *file = NULL;

    if (fopen_s(&file, path, mode) != 0) {
        return NULL;
    }
    return file;
#else
    return fopen(path, mode);
#endif
}

static void basl_set_cli_error(
    basl_error_t *error,
    basl_status_t type,
    const char *message
) {
    if (error == NULL) {
        return;
    }

    basl_error_clear(error);
    error->type = type;
    error->value = message;
    error->length = message == NULL ? 0U : strlen(message);
}

static int basl_print_diagnostics(
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
        if (diagnostic == NULL) {
            continue;
        }

        if (basl_diagnostic_format(registry, diagnostic, &line, &error) == BASL_STATUS_OK) {
            fprintf(stderr, "%s\n", basl_string_c_str(&line));
        } else {
            basl_log_cli_message(
                runtime,
                BASL_LOG_ERROR,
                "failed to format diagnostic",
                "error",
                basl_error_message(&error)
            );
        }
    }
    basl_string_free(&line);

    return 1;
}

static void basl_print_error(
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
            fprintf(
                stderr,
                "%s: %s:%u:%u: %s\n",
                prefix,
                source == NULL ? "<unknown>" : basl_string_c_str(&source->path),
                location.line,
                location.column,
                basl_error_message(error)
            );
            return;
        }
    }

    fprintf(stderr, "%s: %s\n", prefix, basl_error_message(error));
}

static char *basl_read_file(const char *path, size_t *out_length) {
    FILE *file;
    long size;
    char *buffer;
    size_t read_length;

    *out_length = 0U;
    file = basl_open_file(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0L) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *)malloc((size_t)size + 1U);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    read_length = fread(buffer, 1U, (size_t)size, file);
    fclose(file);
    if (read_length != (size_t)size) {
        free(buffer);
        return NULL;
    }

    buffer[read_length] = '\0';
    *out_length = read_length;
    return buffer;
}

static int basl_path_has_basl_extension(const char *path, size_t length) {
    return path != NULL &&
           length >= 5U &&
           memcmp(path + length - 5U, ".basl", 5U) == 0;
}

static int basl_path_is_absolute(const char *path, size_t length) {
    if (path == NULL || length == 0U) {
        return 0;
    }

    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }

    return length >= 2U &&
           ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':';
}

static int basl_registry_find_source_path(
    const basl_source_registry_t *registry,
    const char *path,
    basl_source_id_t *out_source_id
) {
    size_t index;

    if (out_source_id != NULL) {
        *out_source_id = 0U;
    }
    if (registry == NULL || path == NULL) {
        return 0;
    }

    for (index = 1U; index <= basl_source_registry_count(registry); index += 1U) {
        const basl_source_file_t *source;

        source = basl_source_registry_get(registry, (basl_source_id_t)index);
        if (source == NULL) {
            continue;
        }
        if (strcmp(basl_string_c_str(&source->path), path) == 0) {
            if (out_source_id != NULL) {
                *out_source_id = source->id;
            }
            return 1;
        }
    }

    return 0;
}

static const char *basl_source_token_text(
    const basl_source_file_t *source,
    const basl_token_t *token,
    size_t *out_length
) {
    size_t length;

    if (out_length != NULL) {
        *out_length = 0U;
    }
    if (source == NULL || token == NULL) {
        return NULL;
    }

    length = token->span.end_offset - token->span.start_offset;
    if (out_length != NULL) {
        *out_length = length;
    }
    return basl_string_c_str(&source->text) + token->span.start_offset;
}

static basl_status_t basl_resolve_import_path(
    basl_runtime_t *runtime,
    const char *base_path,
    const char *import_text,
    size_t import_length,
    basl_string_t *out_path,
    basl_error_t *error
) {
    size_t base_length;
    size_t prefix_length;

    basl_string_clear(out_path);
    if (
        runtime == NULL ||
        base_path == NULL ||
        import_text == NULL ||
        out_path == NULL
    ) {
        basl_set_cli_error(error, BASL_STATUS_INVALID_ARGUMENT, "import path inputs must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (basl_path_is_absolute(import_text, import_length)) {
        return basl_string_assign(out_path, import_text, import_length, error);
    }

    base_length = strlen(base_path);
    prefix_length = base_length;
    while (prefix_length > 0U) {
        char current = base_path[prefix_length - 1U];

        if (current == '/' || current == '\\') {
            break;
        }
        prefix_length -= 1U;
    }

    if (prefix_length != 0U) {
        if (basl_string_assign(out_path, base_path, prefix_length, error) != BASL_STATUS_OK) {
            return error->type;
        }
        if (basl_string_append(out_path, import_text, import_length, error) != BASL_STATUS_OK) {
            return error->type;
        }
    } else if (
        basl_string_assign(out_path, import_text, import_length, error) != BASL_STATUS_OK
    ) {
        return error->type;
    }

    if (!basl_path_has_basl_extension(basl_string_c_str(out_path), basl_string_length(out_path))) {
        if (basl_string_append_cstr(out_path, ".basl", error) != BASL_STATUS_OK) {
            return error->type;
        }
    }

    (void)runtime;
    return BASL_STATUS_OK;
}

static int basl_parse_mode(
    int argc,
    char **argv,
    basl_cli_mode_t *out_mode,
    const char **out_path
) {
    if (argc == 2) {
        *out_mode = BASL_CLI_MODE_RUN;
        *out_path = argv[1];
        return 1;
    }

    if (argc == 3 && strcmp(argv[1], "check") == 0) {
        *out_mode = BASL_CLI_MODE_CHECK;
        *out_path = argv[2];
        return 1;
    }

    return 0;
}

static int basl_register_script_source(
    basl_source_registry_t *registry,
    const char *path,
    const char *file_text,
    size_t file_length,
    basl_source_id_t *out_source_id,
    basl_error_t *error
) {
    return basl_source_registry_register(
               registry,
               path,
               strlen(path),
               file_text,
               file_length,
               out_source_id,
               error
           ) == BASL_STATUS_OK;
}

static int basl_register_source_tree(
    basl_source_registry_t *registry,
    const char *path,
    basl_source_id_t *out_source_id,
    basl_error_t *error
) {
    basl_runtime_t *runtime;
    basl_source_id_t source_id;
    char *file_text;
    size_t file_length;
    const basl_source_file_t *source;
    basl_token_list_t tokens;
    basl_diagnostic_list_t diagnostics;
    const basl_token_t *token;
    size_t cursor;
    size_t brace_depth;

    runtime = registry == NULL ? NULL : registry->runtime;
    source_id = 0U;
    if (basl_registry_find_source_path(registry, path, &source_id)) {
        if (out_source_id != NULL) {
            *out_source_id = source_id;
        }
        basl_error_clear(error);
        return 1;
    }

    file_text = basl_read_file(path, &file_length);
    if (file_text == NULL) {
        basl_set_cli_error(error, BASL_STATUS_INVALID_ARGUMENT, "failed to read imported source");
        return 0;
    }

    if (
        !basl_register_script_source(
            registry,
            path,
            file_text,
            file_length,
            &source_id,
            error
        )
    ) {
        free(file_text);
        return 0;
    }
    free(file_text);

    if (out_source_id != NULL) {
        *out_source_id = source_id;
    }

    source = basl_source_registry_get(registry, source_id);
    if (source == NULL) {
        basl_set_cli_error(error, BASL_STATUS_INVALID_ARGUMENT, "registered source was not found");
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
        if (token == NULL || token->kind == BASL_TOKEN_EOF) {
            break;
        }

        if (token->kind == BASL_TOKEN_LBRACE) {
            brace_depth += 1U;
            cursor += 1U;
            continue;
        }
        if (token->kind == BASL_TOKEN_RBRACE) {
            if (brace_depth != 0U) {
                brace_depth -= 1U;
            }
            cursor += 1U;
            continue;
        }

        if (brace_depth == 0U && token->kind == BASL_TOKEN_IMPORT) {
            const basl_token_t *path_token;
            basl_string_t import_path;
            const char *import_text;
            size_t import_length;

            cursor += 1U;
            path_token = basl_token_list_get(&tokens, cursor);
            if (
                path_token == NULL ||
                (path_token->kind != BASL_TOKEN_STRING_LITERAL &&
                 path_token->kind != BASL_TOKEN_RAW_STRING_LITERAL)
            ) {
                break;
            }

            import_text = basl_source_token_text(source, path_token, &import_length);
            if (import_text == NULL || import_length < 2U) {
                break;
            }

            basl_string_init(&import_path, runtime);
            if (
                basl_resolve_import_path(
                    runtime,
                    basl_string_c_str(&source->path),
                    import_text + 1U,
                    import_length - 2U,
                    &import_path,
                    error
                ) != BASL_STATUS_OK
            ) {
                basl_string_free(&import_path);
                basl_token_list_free(&tokens);
                basl_diagnostic_list_free(&diagnostics);
                return 0;
            }
            if (
                !basl_register_source_tree(
                    registry,
                    basl_string_c_str(&import_path),
                    NULL,
                    error
                )
            ) {
                basl_string_free(&import_path);
                basl_token_list_free(&tokens);
                basl_diagnostic_list_free(&diagnostics);
                return 0;
            }
            basl_string_free(&import_path);
        }

        cursor += 1U;
    }

    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_error_clear(error);
    return 1;
}

static int basl_check_script(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
) {
    basl_status_t status;

    status = basl_check_source(registry, source_id, diagnostics, error);
    if (status == BASL_STATUS_OK) {
        return 0;
    }

    if (basl_diagnostic_list_count(diagnostics) != 0U) {
        return 2;
    }

    basl_print_error(registry, "check failed", error);
    return 1;
}

static int basl_run_script(
    basl_runtime_t *runtime,
    basl_vm_t *vm,
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_diagnostic_list_t *diagnostics,
    basl_value_t *result,
    basl_error_t *error
) {
    basl_object_t *function;
    basl_status_t status;

    function = NULL;
    status = basl_compile_source(registry, source_id, &function, diagnostics, error);
    if (status != BASL_STATUS_OK) {
        if (basl_diagnostic_list_count(diagnostics) != 0U) {
            basl_object_release(&function);
            return 2;
        }

        basl_print_error(registry, "compile failed", error);
        basl_object_release(&function);
        return 1;
    }

    status = basl_vm_execute_function(vm, function, result, error);
    basl_object_release(&function);
    if (status != BASL_STATUS_OK) {
        basl_print_error(registry, "execution failed", error);
        return 1;
    }

    if (basl_value_kind(result) != BASL_VALUE_INT) {
        basl_log_cli_message(
            runtime,
            BASL_LOG_ERROR,
            "compiled entrypoint did not return i32",
            NULL,
            NULL
        );
        return 1;
    }

    return (int)basl_value_as_int(result);
}

int main(int argc, char **argv) {
    basl_cli_mode_t mode;
    const char *script_path;
    basl_runtime_t *runtime = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_value_t result;
    basl_source_id_t source_id = 0U;
    int exit_code = 0;

    if (!basl_parse_mode(argc, argv, &mode, &script_path)) {
        fprintf(stderr, "usage: %s <script.basl>\n", argv[0]);
        fprintf(stderr, "       %s check <script.basl>\n", argv[0]);
        return 2;
    }

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize runtime: %s\n", basl_error_message(&error));
        return 1;
    }

    if (
        mode == BASL_CLI_MODE_RUN &&
        basl_vm_open(&vm, runtime, NULL, &error) != BASL_STATUS_OK
    ) {
        basl_log_cli_message(
            runtime,
            BASL_LOG_ERROR,
            "failed to initialize vm",
            "error",
            basl_error_message(&error)
        );
        basl_runtime_close(&runtime);
        return 1;
    }

    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_value_init_nil(&result);

    if (
        !basl_register_source_tree(
            &registry,
            script_path,
            &source_id,
            &error
        )
    ) {
        basl_log_cli_message(
            runtime,
            BASL_LOG_ERROR,
            "failed to register source",
            "error",
            basl_error_message(&error)
        );
        exit_code = 1;
        goto cleanup;
    }

    if (mode == BASL_CLI_MODE_CHECK) {
        exit_code = basl_check_script(&registry, source_id, &diagnostics, &error);
        if (basl_diagnostic_list_count(&diagnostics) != 0U) {
            exit_code = basl_print_diagnostics(&registry, &diagnostics);
        }
        goto cleanup;
    }

    exit_code = basl_run_script(
        runtime,
        vm,
        &registry,
        source_id,
        &diagnostics,
        &result,
        &error
    );
    if (basl_diagnostic_list_count(&diagnostics) != 0U) {
        exit_code = basl_print_diagnostics(&registry, &diagnostics);
    }

cleanup:
    basl_value_release(&result);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    return exit_code;
}
