#ifndef BASL_ALLOCATOR_H
#define BASL_ALLOCATOR_H

#include <stddef.h>

#include "basl/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*basl_allocate_fn)(void *user_data, size_t size);
typedef void *(*basl_reallocate_fn)(void *user_data, void *memory, size_t size);
typedef void (*basl_deallocate_fn)(void *user_data, void *memory);

typedef struct basl_allocator {
    void *user_data;
    basl_allocate_fn allocate;
    basl_reallocate_fn reallocate;
    basl_deallocate_fn deallocate;
} basl_allocator_t;

#ifdef __cplusplus
}
#endif

#endif
