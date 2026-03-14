#include "basl/native_module.h"

#include <stdlib.h>
#include <string.h>

void basl_native_registry_init(basl_native_registry_t *registry) {
    if (registry == NULL) {
        return;
    }
    memset(registry, 0, sizeof(*registry));
}

void basl_native_registry_free(basl_native_registry_t *registry) {
    if (registry == NULL) {
        return;
    }
    free(registry->modules);
    memset(registry, 0, sizeof(*registry));
}

basl_status_t basl_native_registry_add(
    basl_native_registry_t *registry,
    const basl_native_module_t *module,
    basl_error_t *error
) {
    (void)error;
    if (registry == NULL || module == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (registry->module_count >= registry->module_capacity) {
        size_t new_cap;
        const basl_native_module_t **new_buf;

        new_cap = registry->module_capacity < 8U ? 8U : registry->module_capacity * 2U;
        new_buf = (const basl_native_module_t **)realloc(
            registry->modules,
            new_cap * sizeof(*new_buf)
        );
        if (new_buf == NULL) {
            return BASL_STATUS_OUT_OF_MEMORY;
        }
        registry->modules = new_buf;
        registry->module_capacity = new_cap;
    }
    registry->modules[registry->module_count] = module;
    registry->module_count += 1U;
    return BASL_STATUS_OK;
}

const basl_native_module_t *basl_native_registry_find(
    const basl_native_registry_t *registry,
    const char *name,
    size_t name_length
) {
    size_t i;

    if (registry == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0U; i < registry->module_count; i++) {
        if (registry->modules[i]->name_length == name_length &&
            memcmp(registry->modules[i]->name, name, name_length) == 0) {
            return registry->modules[i];
        }
    }
    return NULL;
}

int basl_native_registry_find_index(
    const basl_native_registry_t *registry,
    const char *name,
    size_t name_length,
    size_t *out_index
) {
    size_t i;

    if (registry == NULL || name == NULL || out_index == NULL) {
        return 0;
    }
    for (i = 0U; i < registry->module_count; i++) {
        if (registry->modules[i]->name_length == name_length &&
            memcmp(registry->modules[i]->name, name, name_length) == 0) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}
