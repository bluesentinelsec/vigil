#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/basl.h"

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

    for (index = 0U; index < basl_diagnostic_list_count(diagnostics); index += 1U) {
        const basl_diagnostic_t *diagnostic;
        const basl_source_file_t *source;

        diagnostic = basl_diagnostic_list_get(diagnostics, index);
        if (diagnostic == NULL) {
            continue;
        }

        source = basl_source_registry_get(registry, diagnostic->span.source_id);
        fprintf(
            stderr,
            "%s:%zu: %s\n",
            source == NULL ? "<unknown>" : basl_string_c_str(&source->path),
            diagnostic->span.start_offset,
            basl_string_c_str(&diagnostic->message)
        );
    }

    return 1;
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

int main(int argc, char **argv) {
    basl_runtime_t *runtime = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = NULL;
    basl_value_t result;
    basl_source_id_t source_id = 0U;
    char *file_text = NULL;
    size_t file_length = 0U;
    int exit_code = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <script.basl>\n", argv[0]);
        return 2;
    }

    file_text = basl_read_file(argv[1], &file_length);
    if (file_text == NULL) {
        fprintf(stderr, "failed to read script: %s\n", argv[1]);
        return 1;
    }

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize runtime: %s\n", basl_error_message(&error));
        free(file_text);
        return 1;
    }

    if (basl_vm_open(&vm, runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize vm: %s\n", basl_error_message(&error));
        basl_runtime_close(&runtime);
        free(file_text);
        return 1;
    }

    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_value_init_nil(&result);

    if (
        basl_source_registry_register(
            &registry,
            argv[1],
            strlen(argv[1]),
            file_text,
            file_length,
            &source_id,
            &error
        ) != BASL_STATUS_OK
    ) {
        fprintf(stderr, "failed to register source: %s\n", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    if (
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error)
        != BASL_STATUS_OK
    ) {
        if (basl_diagnostic_list_count(&diagnostics) != 0U) {
            exit_code = basl_print_diagnostics(&registry, &diagnostics);
        } else {
            fprintf(stderr, "compile failed: %s\n", basl_error_message(&error));
            exit_code = 1;
        }
        goto cleanup;
    }

    if (basl_vm_execute_function(vm, function, &result, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "execution failed: %s\n", basl_error_message(&error));
        exit_code = 1;
        goto cleanup;
    }

    if (basl_value_kind(&result) != BASL_VALUE_INT) {
        fprintf(stderr, "compiled entrypoint did not return i32\n");
        exit_code = 1;
        goto cleanup;
    }

    exit_code = (int)basl_value_as_int(&result);

cleanup:
    basl_value_release(&result);
    basl_object_release(&function);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    free(file_text);
    return exit_code;
}
