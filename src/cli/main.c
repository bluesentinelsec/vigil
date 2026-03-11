#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/basl.h"

typedef enum basl_cli_mode {
    BASL_CLI_MODE_RUN = 0,
    BASL_CLI_MODE_CHECK = 1
} basl_cli_mode_t;

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
            fprintf(stderr, "failed to format diagnostic: %s\n", basl_error_message(&error));
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
        fprintf(stderr, "compiled entrypoint did not return i32\n");
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
    char *file_text = NULL;
    size_t file_length = 0U;
    int exit_code = 0;

    if (!basl_parse_mode(argc, argv, &mode, &script_path)) {
        fprintf(stderr, "usage: %s <script.basl>\n", argv[0]);
        fprintf(stderr, "       %s check <script.basl>\n", argv[0]);
        return 2;
    }

    file_text = basl_read_file(script_path, &file_length);
    if (file_text == NULL) {
        fprintf(stderr, "failed to read script: %s\n", script_path);
        return 1;
    }

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize runtime: %s\n", basl_error_message(&error));
        free(file_text);
        return 1;
    }

    if (
        mode == BASL_CLI_MODE_RUN &&
        basl_vm_open(&vm, runtime, NULL, &error) != BASL_STATUS_OK
    ) {
        fprintf(stderr, "failed to initialize vm: %s\n", basl_error_message(&error));
        basl_runtime_close(&runtime);
        free(file_text);
        return 1;
    }

    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_value_init_nil(&result);

    if (
        !basl_register_script_source(
            &registry,
            script_path,
            file_text,
            file_length,
            &source_id,
            &error
        )
    ) {
        fprintf(stderr, "failed to register source: %s\n", basl_error_message(&error));
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
    free(file_text);
    return exit_code;
}
