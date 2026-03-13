#include <stdint.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/map.h"

typedef enum basl_map_entry_state {
    BASL_MAP_ENTRY_EMPTY = 0,
    BASL_MAP_ENTRY_OCCUPIED = 1,
    BASL_MAP_ENTRY_TOMBSTONE = 2
} basl_map_entry_state_t;

typedef struct basl_map_entry {
    basl_map_entry_state_t state;
    uint64_t hash;
    basl_value_t key;
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

static uint64_t basl_map_mix_u64(uint64_t value) {
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return value;
}

static int basl_map_key_value_is_supported(const basl_value_t *key) {
    const basl_object_t *object;

    if (key == NULL) {
        return 0;
    }

    switch (basl_value_kind(key)) {
        case BASL_VALUE_BOOL:
        case BASL_VALUE_INT:
            return 1;
        case BASL_VALUE_OBJECT:
            object = basl_value_as_object(key);
            return object != NULL && basl_object_type(object) == BASL_OBJECT_STRING;
        default:
            return 0;
    }
}

static uint64_t basl_map_hash_value(const basl_value_t *key) {
    const basl_object_t *object;

    if (!basl_map_key_value_is_supported(key)) {
        return 0U;
    }

    switch (basl_value_kind(key)) {
        case BASL_VALUE_BOOL:
            return basl_map_mix_u64(basl_value_as_bool(key) ? UINT64_C(1) : UINT64_C(0));
        case BASL_VALUE_INT:
            return basl_map_mix_u64((uint64_t)basl_value_as_int(key));
        case BASL_VALUE_OBJECT:
            object = basl_value_as_object(key);
            return basl_map_hash_bytes(
                basl_string_object_c_str(object),
                basl_string_object_length(object)
            );
        default:
            return 0U;
    }
}

static int basl_map_key_values_equal(
    const basl_value_t *left,
    const basl_value_t *right
) {
    const basl_object_t *left_object;
    const basl_object_t *right_object;
    size_t left_length;
    size_t right_length;
    const char *left_text;
    const char *right_text;

    if (left == NULL || right == NULL || basl_value_kind(left) != basl_value_kind(right)) {
        return 0;
    }

    switch (basl_value_kind(left)) {
        case BASL_VALUE_BOOL:
            return basl_value_as_bool(left) == basl_value_as_bool(right);
        case BASL_VALUE_INT:
            return basl_value_as_int(left) == basl_value_as_int(right);
        case BASL_VALUE_OBJECT:
            left_object = basl_value_as_object(left);
            right_object = basl_value_as_object(right);
            if (
                left_object == NULL ||
                right_object == NULL ||
                basl_object_type(left_object) != BASL_OBJECT_STRING ||
                basl_object_type(right_object) != BASL_OBJECT_STRING
            ) {
                return 0;
            }
            left_length = basl_string_object_length(left_object);
            right_length = basl_string_object_length(right_object);
            left_text = basl_string_object_c_str(left_object);
            right_text = basl_string_object_c_str(right_object);
            return left_length == right_length &&
                   left_text != NULL &&
                   right_text != NULL &&
                   memcmp(left_text, right_text, left_length) == 0;
        default:
            return 0;
    }
}

static int basl_map_key_matches_string(
    const basl_value_t *key,
    const char *text,
    size_t length
) {
    const basl_object_t *object;
    const char *stored_text;

    if (key == NULL || text == NULL || basl_value_kind(key) != BASL_VALUE_OBJECT) {
        return 0;
    }

    object = basl_value_as_object(key);
    if (object == NULL || basl_object_type(object) != BASL_OBJECT_STRING) {
        return 0;
    }

    stored_text = basl_string_object_c_str(object);
    return stored_text != NULL &&
           basl_string_object_length(object) == length &&
           memcmp(stored_text, text, length) == 0;
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

    basl_value_release(&entry->key);
    basl_value_release(&entry->value);
    memset(entry, 0, sizeof(*entry));
}

static size_t basl_map_find_index_value(
    const basl_map_entry_t *entries,
    size_t capacity,
    const basl_value_t *key,
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
        } else if (entry->hash == hash && basl_map_key_values_equal(&entry->key, key)) {
            *out_found = 1;
            return index;
        }

        index = (index + 1U) & (capacity - 1U);
    }
}

static size_t basl_map_find_index_string(
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
        } else if (entry->hash == hash && basl_map_key_matches_string(&entry->key, key, key_length)) {
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
    memset(new_entries, 0, capacity * sizeof(*new_entries));
    for (index = 0U; index < old_capacity; index += 1U) {
        basl_map_entry_t *old_entry;
        basl_map_entry_t *new_entry;
        int found;
        size_t new_index;

        old_entry = &old_entries[index];
        if (old_entry->state != BASL_MAP_ENTRY_OCCUPIED) {
            continue;
        }

        new_index = basl_map_find_index_value(
            new_entries,
            capacity,
            &old_entry->key,
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
    basl_object_t *key_object;
    basl_value_t key_value;
    basl_status_t status;
    size_t index;
    int found;
    uint64_t hash;
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
    index = basl_map_find_index_string(
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

    key_object = NULL;
    status = basl_string_object_new(map->runtime, key, key_length, &key_object, error);
    if (status != BASL_STATUS_OK) {
        basl_value_release(&copied_value);
        return status;
    }

    if (entry->state == BASL_MAP_ENTRY_TOMBSTONE) {
        map->tombstone_count -= 1U;
    }

    basl_value_init_object(&key_value, &key_object);
    entry->state = BASL_MAP_ENTRY_OCCUPIED;
    entry->hash = hash;
    entry->key = key_value;
    entry->value = copied_value;
    map->count += 1U;
    basl_error_clear(error);
    return BASL_STATUS_OK;
}

basl_status_t basl_map_set_value(
    basl_map_t *map,
    const basl_value_t *key,
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

    if (!basl_map_key_value_is_supported(key)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map key must be i32, bool, or string"
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

    hash = basl_map_hash_value(key);
    index = basl_map_find_index_value(
        basl_map_entries(map),
        map->capacity,
        key,
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

    entry->state = BASL_MAP_ENTRY_OCCUPIED;
    entry->hash = hash;
    entry->key = basl_value_copy(key);
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
    index = basl_map_find_index_string(entries, map->capacity, key, key_length, hash, &found);
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

const basl_value_t *basl_map_get_value(
    const basl_map_t *map,
    const basl_value_t *key
) {
    int found;
    size_t index;
    uint64_t hash;
    const basl_map_entry_t *entries;

    if (
        map == NULL ||
        key == NULL ||
        !basl_map_key_value_is_supported(key) ||
        map->capacity == 0U ||
        map->entries == NULL
    ) {
        return NULL;
    }

    entries = basl_map_const_entries(map);
    hash = basl_map_hash_value(key);
    index = basl_map_find_index_value(entries, map->capacity, key, hash, &found);
    if (!found) {
        return NULL;
    }

    return &entries[index].value;
}

int basl_map_contains(
    const basl_map_t *map,
    const char *key,
    size_t key_length
) {
    return basl_map_get(map, key, key_length) != NULL;
}

int basl_map_contains_value(
    const basl_map_t *map,
    const basl_value_t *key
) {
    return basl_map_get_value(map, key) != NULL;
}

int basl_map_contains_cstr(
    const basl_map_t *map,
    const char *key
) {
    return basl_map_get_cstr(map, key) != NULL;
}

int basl_map_entry_at(
    const basl_map_t *map,
    size_t index,
    const char **out_key,
    size_t *out_key_length,
    const basl_value_t **out_value
) {
    const basl_map_entry_t *entries;
    size_t entry_index;
    size_t seen;

    if (out_key != NULL) {
        *out_key = NULL;
    }
    if (out_key_length != NULL) {
        *out_key_length = 0U;
    }
    if (out_value != NULL) {
        *out_value = NULL;
    }

    if (
        map == NULL ||
        out_key == NULL ||
        out_key_length == NULL ||
        out_value == NULL ||
        index >= map->count ||
        map->entries == NULL
    ) {
        return 0;
    }

    entries = basl_map_const_entries(map);
    seen = 0U;
    for (entry_index = 0U; entry_index < map->capacity; entry_index += 1U) {
        if (entries[entry_index].state != BASL_MAP_ENTRY_OCCUPIED) {
            continue;
        }
        if (seen == index) {
            if (
                basl_value_kind(&entries[entry_index].key) != BASL_VALUE_OBJECT ||
                basl_value_as_object(&entries[entry_index].key) == NULL ||
                basl_object_type(basl_value_as_object(&entries[entry_index].key)) != BASL_OBJECT_STRING
            ) {
                return 0;
            }
            *out_key = basl_string_object_c_str(basl_value_as_object(&entries[entry_index].key));
            *out_key_length = basl_string_object_length(basl_value_as_object(&entries[entry_index].key));
            *out_value = &entries[entry_index].value;
            return 1;
        }
        seen += 1U;
    }

    return 0;
}

int basl_map_entry_value_at(
    const basl_map_t *map,
    size_t index,
    const basl_value_t **out_key,
    const basl_value_t **out_value
) {
    const basl_map_entry_t *entries;
    size_t entry_index;
    size_t seen;

    if (out_key != NULL) {
        *out_key = NULL;
    }
    if (out_value != NULL) {
        *out_value = NULL;
    }

    if (
        map == NULL ||
        out_key == NULL ||
        out_value == NULL ||
        index >= map->count ||
        map->entries == NULL
    ) {
        return 0;
    }

    entries = basl_map_const_entries(map);
    seen = 0U;
    for (entry_index = 0U; entry_index < map->capacity; entry_index += 1U) {
        if (entries[entry_index].state != BASL_MAP_ENTRY_OCCUPIED) {
            continue;
        }
        if (seen == index) {
            *out_key = &entries[entry_index].key;
            *out_value = &entries[entry_index].value;
            return 1;
        }
        seen += 1U;
    }

    return 0;
}

basl_status_t basl_map_remove_value(
    basl_map_t *map,
    const basl_value_t *key,
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

    if (!basl_map_key_value_is_supported(key)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map key must be i32, bool, or string"
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

    hash = basl_map_hash_value(key);
    index = basl_map_find_index_value(basl_map_entries(map), map->capacity, key, hash, &found);
    if (!found) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    entry = &basl_map_entries(map)[index];
    basl_value_release(&entry->key);
    basl_value_release(&entry->value);
    entry->state = BASL_MAP_ENTRY_TOMBSTONE;
    entry->hash = 0U;
    map->count -= 1U;
    map->tombstone_count += 1U;
    *out_removed = 1;
    basl_error_clear(error);
    return BASL_STATUS_OK;
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
    index = basl_map_find_index_string(
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
    basl_value_release(&entry->key);
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
