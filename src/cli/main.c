#include <stdio.h>

#include "basl/basl.h"

int main(int argc, char **argv) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};

    if (argc > 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 2;
    }

    if (basl_runtime_open(&runtime, NULL, &error) != BASL_STATUS_OK) {
        fprintf(stderr, "failed to initialize runtime: %s\n", basl_error_message(&error));
        return 1;
    }

    puts("basl CLI scaffold");
    basl_runtime_close(&runtime);
    return 0;
}
