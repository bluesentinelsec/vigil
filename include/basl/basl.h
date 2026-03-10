#ifndef BASL_BASL_H
#define BASL_BASL_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#if defined(BASL_SHARED)
#if defined(BASL_EXPORTS)
#define BASL_API __declspec(dllexport)
#else
#define BASL_API __declspec(dllimport)
#endif
#else
#define BASL_API
#endif
#else
#define BASL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum basl_status {
    BASL_STATUS_OK = 0,
    BASL_STATUS_INVALID_ARGUMENT = 1,
    BASL_STATUS_OUT_OF_MEMORY = 2,
    BASL_STATUS_INTERNAL = 3,
    BASL_STATUS_UNSUPPORTED = 4
} basl_status_t;

typedef struct basl_source_location {
    uint32_t source_id;
    uint32_t line;
    uint32_t column;
} basl_source_location_t;

typedef struct basl_error {
    basl_status_t type;
    const char *value;
    size_t length;
    basl_source_location_t location;
} basl_error_t;

typedef void *(*basl_allocate_fn)(void *user_data, size_t size);
typedef void *(*basl_reallocate_fn)(void *user_data, void *memory, size_t size);
typedef void (*basl_deallocate_fn)(void *user_data, void *memory);

typedef struct basl_allocator {
    void *user_data;
    basl_allocate_fn allocate;
    basl_reallocate_fn reallocate;
    basl_deallocate_fn deallocate;
} basl_allocator_t;

typedef struct basl_runtime_options {
    const basl_allocator_t *allocator;
} basl_runtime_options_t;

typedef struct basl_runtime basl_runtime_t;

BASL_API void basl_error_clear(basl_error_t *error);
BASL_API void basl_source_location_clear(basl_source_location_t *location);
BASL_API const char *basl_status_name(basl_status_t status);
BASL_API void basl_runtime_options_init(basl_runtime_options_t *options);
BASL_API basl_status_t basl_runtime_open(
    basl_runtime_t **out_runtime,
    const basl_runtime_options_t *options,
    basl_error_t *error
);
BASL_API void basl_runtime_close(basl_runtime_t **runtime);
BASL_API const basl_allocator_t *basl_runtime_allocator(const basl_runtime_t *runtime);
BASL_API basl_status_t basl_runtime_alloc(
    basl_runtime_t *runtime,
    size_t size,
    void **out_memory,
    basl_error_t *error
);
BASL_API basl_status_t basl_runtime_realloc(
    basl_runtime_t *runtime,
    void **memory,
    size_t size,
    basl_error_t *error
);
BASL_API void basl_runtime_free(basl_runtime_t *runtime, void **memory);

BASL_API int basl_sum(int a, int b);

#ifdef __cplusplus
}
#endif

#endif
