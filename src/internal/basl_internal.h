#ifndef BASL_INTERNAL_H
#define BASL_INTERNAL_H

#include "basl/runtime.h"

struct basl_runtime {
    basl_allocator_t allocator;
};

basl_allocator_t basl_default_allocator(void);
int basl_allocator_is_valid(const basl_allocator_t *allocator);
void basl_error_set_literal(
    basl_error_t *error,
    basl_status_t type,
    const char *value
);

#endif
