#include <stdio.h>
#include <stdlib.h>

#include "basl/basl.h"

static int parse_int(const char *value, int *out_value) {
    char *end = NULL;
    long parsed;

    if (value == NULL || out_value == NULL) {
        return -1;
    }

    parsed = strtol(value, &end, 10);
    if (end == value || end == NULL || *end != '\0') {
        return -1;
    }

    *out_value = (int)parsed;
    return 0;
}

int main(int argc, char **argv) {
    int left;
    int right;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <a> <b>\n", argv[0]);
        return 1;
    }

    if (parse_int(argv[1], &left) != 0 || parse_int(argv[2], &right) != 0) {
        fprintf(stderr, "arguments must be integers\n");
        return 1;
    }

    printf("%d\n", basl_sum(left, right));
    return 0;
}
