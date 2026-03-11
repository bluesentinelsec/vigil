#include <stdint.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/map.h"
#include "basl/string.h"

typedef enum basl_map_entry_state {
    BASL_MAP_ENTRY_EMPTY = 0,
    BASL_MAP_ENTRY_OCCUPIED = 1,
    BASL_MAP_ENTRY_TOMBSTONE = 2
} basl_map_entry_state_t;

typedef struct basl_map_entry {
    basl_map_entry_state_t state;
    uint64_t hash;
    basl_string_t key;
    basl_value_t value;
} basl_map_entry_t;

static uint64_t basl_map_hash_bytes(const char *key, size_t key_length) {
    size_t index;
    uint64_t hash;

    hash = UINT64_C(14695981039346656037);
    for (index = 0U; index < key_length; index += 1U) {
        hash ^= (uint8_t)key[index];
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

static int basl_map_validate_mutable(
    const basl_map_t *map,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (map == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map must not be null"
        );
        return 0;
    }

    if (map->runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map runtime must not be null"
        );
        return 0;
    }

    return 1;
}

static basl_map_entry_t *basl_map_entries(basl_map_t *map) {
    return (basl_map_entry_t *)map->entries;
}

static const basl_map_entry_t *basl_map_const_entries(const basl_map_t *map) {
    return (const basl_map_entry_t *)map->entries;
}

static size_t basl_map_next_capacity(size_t current_capacity) {
    if (current_capacity == 0U) {
        return 16U;
    }

    return current_capacity * 2U;
}

static void basl_map_entry_clear(basl_map_entry_t *entry) {
    if (entry == NULL) {
        return;
    }

    basl_string_free(&entry->key);
    basl_value_release(&entry->value);
    memset(entry, 0, sizeof(*entry));
}

static size_t basl_map_find_index(
    const basl_map_entry_t *entries,
    size_t capacity,
    const char *key,
    size_t key_length,
    uint64_t hash,
    int *out_found
) {
    size_t index;
    size_t first_tombstone;

    *out_found = 0;
    first_tombstone = SIZE_MAX;
    index = (size_t)(hash & (uint64_t)(capacity - 1U));

    for (;;) {
        const basl_map_entry_t *entry;

        entry = &entries[index];
        if (entry->state == BASL_MAP_ENTRY_EMPTY) {
            if (first_tombstone != SIZE_MAX) {
                return first_tombstone;
            }

            return index;
        }

        if (entry->state == BASL_MAP_ENTRY_TOMBSTONE) {
            if (first_tombstone == SIZE_MAX) {
                first_tombstone = index;
            }
        } else if (
            entry->hash == hash &&
            basl_string_length(&entry->key) == key_length &&
            memcmp(basl_string_c_str(&entry->key), key, key_length) == 0
        ) {
            *out_found = 1;
            return index;
        }

        index = (index + 1U) & (capacity - 1U);
    }
}

static basl_status_t basl_map_rebuild(
    basl_map_t *map,
    size_t capacity,
    basl_error_t *error
) {
    basl_status_t status;
    basl_map_entry_t *old_entries;
    basl_map_entry_t *new_entries;
    size_t old_capacity;
    size_t index;
    void *memory;

    old_entries = basl_map_entries(map);
    old_capacity = map->capacity;
    new_entries = NULL;
    memory = NULL;

    if (capacity > SIZE_MAX / sizeof(*new_entries)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map capacity would overflow"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_runtime_alloc(
        map->runtime,
        capacity * sizeof(*new_entries),
        &memory,
        error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    new_entries = (basl_map_entry_t *)memory;
    for (index = 0U; index < old_capacity; index += 1U) {
        basl_map_entry_t *old_entry;
        basl_map_entry_t *new_entry;
        int found;
        size_t new_index;

        old_entry = &old_entries[index];
        if (old_entry->state != BASL_MAP_ENTRY_OCCUPIED) {
            continue;
        }

        new_index = basl_map_find_index(
            new_entries,
            capacity,
            basl_string_c_str(&old_entry->key),
            basl_string_length(&old_entry->key),
            old_entry->hash,
            &found
        );
        new_entry = &new_entries[new_index];
        *new_entry = *old_entry;
        memset(old_entry, 0, sizeof(*old_entry));
    }

    memory = old_entries;
    basl_runtime_free(map->runtime, &memory);
    map->entries = new_entries;
    map->capacity = capacity;
    map->tombstone_count = 0U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_map_ensure_capacity(
    basl_map_t *map,
    basl_error_t *error
) {
    size_t capacity;

    if (map->capacity != 0U) {
        if ((map->count + map->tombstone_count + 1U) * 4U <= map->capacity * 3U) {
            basl_error_clear(error);
            return BASL_STATUS_OK;
        }
    }

    capacity = basl_map_next_capacity(map->capacity);
    while ((map->count + 1U) * 4U > capacity * 3U) {
        if (capacity > SIZE_MAX / 2U) {
            basl_error_set_literal(
                error,
                BASL_STATUS_INVALID_ARGUMENT,
                "map capacity would overflow"
            );
            return BASL_STATUS_INVALID_ARGUMENT;
        }

        capacity *= 2U;
    }

    return basl_map_rebuild(map, capacity, error);
}

void basl_map_init(
    basl_map_t *map,
    basl_runtime_t *runtime
) {
    if (map == NULL) {
        return;
    }

    memset(map, 0, sizeof(*map));
    map->runtime = runtime;
}

void basl_map_clear(basl_map_t *map) {
    size_t index;
    basl_map_entry_t *entries;

    if (map == NULL || map->entries == NULL) {
        return;
    }

    entries = basl_map_entries(map);
    for (index = 0U; index < map->capacity; index += 1U) {
        if (entries[index].state == BASL_MAP_ENTRY_OCCUPIED) {
            basl_map_entry_clear(&entries[index]);
        } else if (entries[index].state == BASL_MAP_ENTRY_TOMBSTONE) {
            memset(&entries[index], 0, sizeof(entries[index]));
        }
    }

    map->count = 0U;
    map->tombstone_count = 0U;
}

void basl_map_free(basl_map_t *map) {
    void *memory;

    if (map == NULL) {
        return;
    }

    basl_map_clear(map);
    memory = map->entries;
    if (map->runtime != NULL) {
        basl_runtime_free(map->runtime, &memory);
    }
    memset(map, 0, sizeof(*map));
}

size_t basl_map_count(const basl_map_t *map) {
    if (map == NULL) {
        return 0U;
    }

    return map->count;
}

basl_status_t basl_map_set(
    basl_map_t *map,
    const char *key,
    size_t key_length,
    const basl_value_t *value,
    basl_error_t *error
) {
    basl_status_t status;
    uint64_t hash;
    size_t index;
    int found;
    basl_map_entry_t *entry;
    basl_value_t copied_value;

    if (!basl_map_validate_mutable(map, error)) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (key == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map key must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_map_ensure_capacity(map, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    hash = basl_map_hash_bytes(key, key_length);
    index = basl_map_find_index(
        basl_map_entries(map),
        map->capacity,
        key,
        key_length,
        hash,
        &found
    );
    entry = &basl_map_entries(map)[index];
    copied_value = basl_value_copy(value);

    if (found) {
        basl_value_release(&entry->value);
        entry->value = copied_value;
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    if (entry->state == BASL_MAP_ENTRY_TOMBSTONE) {
        map->tombstone_count -= 1U;
    }

    basl_string_init(&entry->key, map->runtime);
    status = basl_string_assign(&entry->key, key, key_length, error);
    if (status != BASL_STATUS_OK) {
        basl_value_release(&copied_value);
        basl_map_entry_clear(entry);
        return status;
    }

    entry->state = BASL_MAP_ENTRY_OCCUPIED;
    entry->hash = hash;
    entry->value = copied_value;
    map->count += 1U;
    basl_error_clear(error);
    return BASL_STATUS_OK;
}

basl_status_t basl_map_set_cstr(
    basl_map_t *map,
    const char *key,
    const basl_value_t *value,
    basl_error_t *error
) {
    size_t key_length;

    if (key == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map key must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    key_length = strlen(key);
    return basl_map_set(map, key, key_length, value, error);
}

const basl_value_t *basl_map_get(
    const basl_map_t *map,
    const char *key,
    size_t key_length
) {
    int found;
    size_t index;
    uint64_t hash;
    const basl_map_entry_t *entries;

    if (map == NULL || key == NULL || map->capacity == 0U || map->entries == NULL) {
        return NULL;
    }

    entries = basl_map_const_entries(map);
    hash = basl_map_hash_bytes(key, key_length);
    index = basl_map_find_index(entries, map->capacity, key, key_length, hash, &found);
    if (!found) {
        return NULL;
    }

    return &entries[index].value;
}

const basl_value_t *basl_map_get_cstr(
    const basl_map_t *map,
    const char *key
) {
    if (key == NULL) {
        return NULL;
    }

    return basl_map_get(map, key, strlen(key));
}

int basl_map_contains(
    const basl_map_t *map,
    const char *key,
    size_t key_length
) {
    return basl_map_get(map, key, key_length) != NULL;
}

int basl_map_contains_cstr(
    const basl_map_t *map,
    const char *key
) {
    return basl_map_get_cstr(map, key) != NULL;
}

basl_status_t basl_map_remove(
    basl_map_t *map,
    const char *key,
    size_t key_length,
    int *out_removed,
    basl_error_t *error
) {
    uint64_t hash;
    size_t index;
    int found;
    basl_map_entry_t *entry;

    if (out_removed != NULL) {
        *out_removed = 0;
    }

    if (!basl_map_validate_mutable(map, error)) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (key == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map key must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (out_removed == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "out_removed must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (map->capacity == 0U || map->entries == NULL) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    hash = basl_map_hash_bytes(key, key_length);
    index = basl_map_find_index(
        basl_map_entries(map),
        map->capacity,
        key,
        key_length,
        hash,
        &found
    );
    if (!found) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    entry = &basl_map_entries(map)[index];
    basl_string_free(&entry->key);
    basl_value_release(&entry->value);
    entry->state = BASL_MAP_ENTRY_TOMBSTONE;
    entry->hash = 0U;
    map->count -= 1U;
    map->tombstone_count += 1U;
    *out_removed = 1;
    basl_error_clear(error);
    return BASL_STATUS_OK;
}

basl_status_t basl_map_remove_cstr(
    basl_map_t *map,
    const char *key,
    int *out_removed,
    basl_error_t *error
) {
    if (key == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map key must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return basl_map_remove(map, key, strlen(key), out_removed, error);
}
