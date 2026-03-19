/* VIGIL standard library: fs module.
 *
 * Provides filesystem operations: path manipulation, file I/O,
 * directory operations, and standard locations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strtok_r strtok_s
#endif

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"
#include "vigil/runtime.h"
#include "platform/platform.h"

#include "internal/vigil_nanbox.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool get_string_arg(vigil_vm_t *vm, size_t base, size_t idx,
                           const char **str, size_t *len) {
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v)) return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING) return false;
    *str = vigil_string_object_c_str(obj);
    *len = vigil_string_object_length(obj);
    return true;
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *str, size_t len,
                                  vigil_error_t *error) {
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), str, len, &obj, error);
    if (s != VIGIL_STATUS_OK) return s;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t push_bool(vigil_vm_t *vm, int b, vigil_error_t *error) {
    vigil_value_t val;
    vigil_value_init_bool(&val, b);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t n, vigil_error_t *error) {
    vigil_value_t val;
    vigil_value_init_int(&val, n);
    return vigil_vm_stack_push(vm, &val, error);
}

/* ── Path operations ─────────────────────────────────────────────── */

static vigil_status_t fs_join(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char result[4096] = "";
    
    for (size_t i = 0; i < arg_count; i++) {
        const char *part;
        size_t part_len;
        if (!get_string_arg(vm, base, i, &part, &part_len)) continue;
        if (part_len == 0) continue;
        
        if (result[0] != '\0') {
            size_t rlen = strlen(result);
            if (result[rlen - 1] != '/' && part[0] != '/') {
                if (rlen + 1 < sizeof(result)) {
                    result[rlen] = '/';
                    result[rlen + 1] = '\0';
                }
            }
        }
        size_t space = sizeof(result) - strlen(result) - 1;
        size_t plen = strlen(part);
        if (plen > space) plen = space;
        size_t rlen = strlen(result);
        memcpy(result + rlen, part, plen);
        result[rlen + plen] = '\0';
    }
    
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_string(vm, result, strlen(result), error);
}

static vigil_status_t fs_clean(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    vigil_runtime_t *runtime;
    void *memory = NULL;
    char *copy;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, ".", 1, error);
    }
    
    char result[4096];
    char *parts[256];
    size_t part_count = 0;
    int is_abs = (path_len > 0 && path[0] == '/');
    
    /* Copy and split by / */
    runtime = vigil_vm_runtime(vm);
    if (vigil_runtime_alloc(runtime, path_len + 1, &memory, error) != VIGIL_STATUS_OK) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    copy = (char *)memory;
    memcpy(copy, path, path_len);
    copy[path_len] = '\0';
    
    char *tok = copy;
    char *next = NULL;
    while ((tok = strtok_r(tok, "/", &next)) != NULL) {
        if (strcmp(tok, ".") == 0) {
            /* skip */
        } else if (strcmp(tok, "..") == 0) {
            if (part_count > 0 && strcmp(parts[part_count - 1], "..") != 0) {
                part_count--;
            } else if (!is_abs) {
                parts[part_count++] = "..";
            }
        } else if (tok[0] != '\0') {
            parts[part_count++] = tok;
        }
        tok = NULL;
    }
    
    /* Rebuild */
    result[0] = '\0';
    size_t pos = 0;
    if (is_abs) {
        result[0] = '/';
        result[1] = '\0';
        pos = 1;
    }
    for (size_t i = 0; i < part_count; i++) {
        size_t plen = strlen(parts[i]);
        if (i > 0 || is_abs) {
            if (pos < sizeof(result) - 1) result[pos++] = '/';
        }
        if (pos + plen < sizeof(result)) {
            memcpy(result + pos, parts[i], plen);
            pos += plen;
        }
        result[pos] = '\0';
    }
    if (result[0] == '\0') {
        result[0] = '.';
        result[1] = '\0';
    }
    /* Fix double slash at start */
    if (is_abs && result[1] == '/') memmove(result + 1, result + 2, strlen(result + 1));
    
    vigil_runtime_free(runtime, &memory);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_string(vm, result, strlen(result), error);
}

static vigil_status_t fs_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, ".", 1, error);
    }
    
    /* Find last / */
    const char *last_sep = NULL;
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '/') last_sep = path + i;
    }
    
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!last_sep) return push_string(vm, ".", 1, error);
    if (last_sep == path) return push_string(vm, "/", 1, error);
    return push_string(vm, path, (size_t)(last_sep - path), error);
}

static vigil_status_t fs_base(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    
    /* Find last / */
    const char *last_sep = NULL;
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '/') last_sep = path + i;
    }
    
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!last_sep) return push_string(vm, path, path_len, error);
    return push_string(vm, last_sep + 1, path_len - (size_t)(last_sep - path) - 1, error);
}

static vigil_status_t fs_ext(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    
    /* Find last . after last / */
    const char *last_dot = NULL;
    const char *last_sep = NULL;
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '/') last_sep = path + i;
        if (path[i] == '.') last_dot = path + i;
    }
    
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!last_dot || (last_sep && last_dot < last_sep)) {
        return push_string(vm, "", 0, error);
    }
    return push_string(vm, last_dot, path_len - (size_t)(last_dot - path), error);
}

static vigil_status_t fs_is_abs(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    vigil_vm_stack_pop_n(vm, arg_count);
#ifdef _WIN32
    int is_abs = (path_len >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) ||
                 (path_len > 0 && (path[0] == '/' || path[0] == '\\'));
#else
    int is_abs = (path_len > 0 && path[0] == '/');
#endif
    return push_bool(vm, is_abs, error);
}


/* ── File operations ─────────────────────────────────────────────── */

static vigil_status_t fs_read(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    vigil_runtime_t *runtime;
    const vigil_allocator_t *allocator;
    void *memory = NULL;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    char *data = NULL;
    size_t data_len = 0;
    runtime = vigil_vm_runtime(vm);
    allocator = vigil_runtime_allocator(runtime);
    vigil_status_t s = vigil_platform_read_file(allocator, pathbuf, &data, &data_len, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    if (s != VIGIL_STATUS_OK) return push_string(vm, "", 0, error);
    s = push_string(vm, data, data_len, error);
    memory = data;
    vigil_runtime_free(runtime, &memory);
    return s;
}

static vigil_status_t fs_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path, *data;
    size_t path_len, data_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len) ||
        !get_string_arg(vm, base, 1, &data, &data_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    vigil_status_t s = vigil_platform_write_file(pathbuf, data, data_len, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_append(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path, *data;
    size_t path_len, data_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len) ||
        !get_string_arg(vm, base, 1, &data, &data_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    vigil_status_t s = vigil_platform_append_file(pathbuf, data, data_len, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_copy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *src, *dst;
    size_t src_len, dst_len;
    
    if (!get_string_arg(vm, base, 0, &src, &src_len) ||
        !get_string_arg(vm, base, 1, &dst, &dst_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char srcbuf[4096], dstbuf[4096];
    snprintf(srcbuf, sizeof(srcbuf), "%.*s", (int)src_len, src);
    snprintf(dstbuf, sizeof(dstbuf), "%.*s", (int)dst_len, dst);
    
    vigil_status_t s = vigil_platform_copy_file(srcbuf, dstbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_move(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *src, *dst;
    size_t src_len, dst_len;
    
    if (!get_string_arg(vm, base, 0, &src, &src_len) ||
        !get_string_arg(vm, base, 1, &dst, &dst_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char srcbuf[4096], dstbuf[4096];
    snprintf(srcbuf, sizeof(srcbuf), "%.*s", (int)src_len, src);
    snprintf(dstbuf, sizeof(dstbuf), "%.*s", (int)dst_len, dst);
    
    vigil_status_t s = vigil_platform_rename(srcbuf, dstbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_remove(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    vigil_status_t s = vigil_platform_remove(pathbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_exists(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int exists = 0;
    vigil_platform_file_exists(pathbuf, &exists);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, exists, error);
}

static vigil_status_t fs_is_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int is_dir = 0;
    vigil_platform_is_directory(pathbuf, &is_dir);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, is_dir, error);
}

static vigil_status_t fs_is_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int is_file = 0;
    vigil_platform_is_file(pathbuf, &is_file);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, is_file, error);
}


/* ── Directory operations ────────────────────────────────────────── */

static vigil_status_t fs_mkdir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    vigil_status_t s = vigil_platform_mkdir(pathbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_mkdir_all(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    vigil_status_t s = vigil_platform_mkdir_p(pathbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

/* Callback data for list_dir */
typedef struct {
    vigil_vm_t *vm;
    vigil_object_t *array;
    vigil_error_t *error;
} list_ctx_t;

static vigil_status_t list_callback(const char *name, int is_dir, void *user_data) {
    (void)is_dir;
    list_ctx_t *ctx = (list_ctx_t *)user_data;
    
    vigil_object_t *str = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(ctx->vm), name, strlen(name), &str, ctx->error);
    if (s != VIGIL_STATUS_OK) return s;
    
    vigil_value_t val;
    vigil_value_init_object(&val, &str);
    s = vigil_array_object_append(ctx->array, &val, ctx->error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t fs_list(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        /* Return empty array */
        vigil_object_t *arr = NULL;
        vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        vigil_status_t s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    vigil_object_t *arr = NULL;
    vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return s;
    }
    
    list_ctx_t ctx = { vm, arr, error };
    vigil_platform_list_dir(pathbuf, list_callback, &ctx, error);
    
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

/* Recursive walk helper */
typedef struct {
    vigil_vm_t *vm;
    vigil_object_t *array;
    vigil_error_t *error;
    char base_path[4096];
} walk_ctx_t;

static vigil_status_t walk_dir(walk_ctx_t *ctx, const char *dir);

static vigil_status_t walk_callback(const char *name, int is_dir, void *user_data) {
    walk_ctx_t *ctx = (walk_ctx_t *)user_data;
    
    char full_path[4096];
    size_t base_len = strlen(ctx->base_path);
    size_t name_len = strlen(name);
    if (base_len + 1 + name_len >= sizeof(full_path)) {
        return VIGIL_STATUS_OK; /* Skip paths that are too long */
    }
    memcpy(full_path, ctx->base_path, base_len);
    full_path[base_len] = '/';
    memcpy(full_path + base_len + 1, name, name_len + 1);
    
    vigil_object_t *str = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(ctx->vm), full_path, strlen(full_path), &str, ctx->error);
    if (s != VIGIL_STATUS_OK) return s;
    
    vigil_value_t val;
    vigil_value_init_object(&val, &str);
    s = vigil_array_object_append(ctx->array, &val, ctx->error);
    vigil_value_release(&val);
    if (s != VIGIL_STATUS_OK) return s;
    
    if (is_dir) {
        char saved[4096];
        size_t full_len = strlen(full_path);
        memcpy(saved, ctx->base_path, base_len + 1);
        if (full_len < sizeof(ctx->base_path)) {
            memcpy(ctx->base_path, full_path, full_len + 1);
        }
        walk_dir(ctx, full_path);
        memcpy(ctx->base_path, saved, base_len + 1);
    }
    
    return VIGIL_STATUS_OK;
}

static vigil_status_t walk_dir(walk_ctx_t *ctx, const char *dir) {
    return vigil_platform_list_dir(dir, walk_callback, ctx, ctx->error);
}

static vigil_status_t fs_walk(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *arr = NULL;
        vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        vigil_status_t s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    vigil_object_t *arr = NULL;
    vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return s;
    }
    
    walk_ctx_t ctx = { vm, arr, error, "" };
    size_t copy_len = path_len < sizeof(ctx.base_path) - 1 ? path_len : sizeof(ctx.base_path) - 1;
    memcpy(ctx.base_path, pathbuf, copy_len);
    ctx.base_path[copy_len] = '\0';
    walk_dir(&ctx, pathbuf);
    
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}


/* ── Metadata and locations ──────────────────────────────────────── */

static vigil_status_t fs_size(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int64_t size = vigil_platform_file_size(pathbuf);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, size, error);
}

static vigil_status_t fs_mtime(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int64_t mtime = vigil_platform_file_mtime(pathbuf);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, mtime, error);
}

static vigil_status_t fs_temp_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    vigil_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    vigil_status_t s = vigil_platform_temp_dir(&path, error);
    if (s != VIGIL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static vigil_status_t fs_temp_file(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *prefix = "tmp";
    size_t prefix_len = 3;
    
    if (arg_count > 0) {
        get_string_arg(vm, base, 0, &prefix, &prefix_len);
    }
    
    char prefixbuf[64];
    snprintf(prefixbuf, sizeof(prefixbuf), "%.*s", (int)prefix_len, prefix);
    
    vigil_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    vigil_status_t s = vigil_platform_temp_file(prefixbuf, &path, error);
    if (s != VIGIL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static vigil_status_t fs_home_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    vigil_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    vigil_status_t s = vigil_platform_home_dir(&path, error);
    if (s != VIGIL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static vigil_status_t fs_config_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    vigil_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    vigil_status_t s = vigil_platform_config_dir(&path, error);
    if (s != VIGIL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static vigil_status_t fs_cache_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    vigil_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    vigil_status_t s = vigil_platform_cache_dir(&path, error);
    if (s != VIGIL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static vigil_status_t fs_data_dir(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    vigil_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    vigil_status_t s = vigil_platform_data_dir(&path, error);
    if (s != VIGIL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static vigil_status_t fs_cwd(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    vigil_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    vigil_status_t s = vigil_platform_getcwd(&path, error);
    if (s != VIGIL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

/* ── Symlink operations ──────────────────────────────────────────── */

static vigil_status_t fs_symlink(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *target, *linkpath;
    size_t target_len, link_len;

    if (!get_string_arg(vm, base, 0, &target, &target_len) ||
        !get_string_arg(vm, base, 1, &linkpath, &link_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char tbuf[4096], lbuf[4096];
    snprintf(tbuf, sizeof(tbuf), "%.*s", (int)target_len, target);
    snprintf(lbuf, sizeof(lbuf), "%.*s", (int)link_len, linkpath);

    vigil_status_t s = vigil_platform_symlink(tbuf, lbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

static vigil_status_t fs_readlink(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    char *target = NULL;
    vigil_status_t s = vigil_platform_readlink(pathbuf, &target, error);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (s != VIGIL_STATUS_OK) return push_string(vm, "", 0, error);
    s = push_string(vm, target, strlen(target), error);
    free(target);
    return s;
}

static vigil_status_t fs_is_symlink(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    int result = 0;
    vigil_platform_is_symlink(pathbuf, &result);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, result, error);
}

/* ── Recursive remove ────────────────────────────────────────────── */

static vigil_status_t fs_remove_all(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;

    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);

    vigil_status_t s = vigil_platform_remove_all(pathbuf, error);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == VIGIL_STATUS_OK, error);
}

/* ── Glob matching ───────────────────────────────────────────────── */

typedef struct {
    vigil_vm_t *vm;
    vigil_object_t *array;
    vigil_error_t *error;
    const char *pattern;
    const char *dir_path;
} glob_ctx_t;

static vigil_status_t glob_callback(const char *name, int is_dir, void *user_data) {
    glob_ctx_t *ctx = (glob_ctx_t *)user_data;
    (void)is_dir;

    if (!vigil_platform_glob_match(ctx->pattern, name)) return VIGIL_STATUS_OK;

    char full[4096];
    snprintf(full, sizeof(full), "%s/%s", ctx->dir_path, name);

    vigil_object_t *str = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(ctx->vm), full, strlen(full), &str, ctx->error);
    if (s != VIGIL_STATUS_OK) return s;

    vigil_value_t val;
    vigil_value_init_object(&val, &str);
    s = vigil_array_object_append(ctx->array, &val, ctx->error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t fs_glob(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *dir, *pattern;
    size_t dir_len, pat_len;

    if (!get_string_arg(vm, base, 0, &dir, &dir_len) ||
        !get_string_arg(vm, base, 1, &pattern, &pat_len)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_object_t *arr = NULL;
        vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
        vigil_value_t val;
        vigil_value_init_object(&val, &arr);
        vigil_status_t s = vigil_vm_stack_push(vm, &val, error);
        vigil_value_release(&val);
        return s;
    }

    char dirbuf[4096], patbuf[4096];
    snprintf(dirbuf, sizeof(dirbuf), "%.*s", (int)dir_len, dir);
    snprintf(patbuf, sizeof(patbuf), "%.*s", (int)pat_len, pattern);

    vigil_object_t *arr = NULL;
    vigil_status_t s = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != VIGIL_STATUS_OK) { vigil_vm_stack_pop_n(vm, arg_count); return s; }

    glob_ctx_t ctx = { vm, arr, error, patbuf, dirbuf };
    vigil_platform_list_dir(dirbuf, glob_callback, &ctx, error);

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_value_t val;
    vigil_value_init_object(&val, &arr);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_param[] = {VIGIL_TYPE_STRING};
static const int str_str_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};

static const vigil_native_module_function_t vigil_fs_functions[] = {
    /* Path operations */
    {"join", 4U, fs_join, 2U, str_str_params, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"clean", 5U, fs_clean, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"dir", 3U, fs_dir, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"base", 4U, fs_base, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"ext", 3U, fs_ext, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"is_abs", 6U, fs_is_abs, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    /* File operations */
    {"read", 4U, fs_read, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"write", 5U, fs_write, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"append", 6U, fs_append, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"copy", 4U, fs_copy, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"move", 4U, fs_move, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"remove", 6U, fs_remove, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"remove_all", 10U, fs_remove_all, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"exists", 6U, fs_exists, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"is_dir", 6U, fs_is_dir, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"is_file", 7U, fs_is_file, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"is_symlink", 10U, fs_is_symlink, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    /* Directory operations */
    {"mkdir", 5U, fs_mkdir, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"mkdir_all", 9U, fs_mkdir_all, 1U, str_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"list", 4U, fs_list, 1U, str_param, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL, NULL},
    {"walk", 4U, fs_walk, 1U, str_param, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL, NULL},
    {"glob", 4U, fs_glob, 2U, str_str_params, VIGIL_TYPE_OBJECT, 1U, NULL, VIGIL_TYPE_STRING, NULL, NULL},
    /* Symlink operations */
    {"symlink", 7U, fs_symlink, 2U, str_str_params, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"readlink", 8U, fs_readlink, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    /* Metadata */
    {"size", 4U, fs_size, 1U, str_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"mtime", 5U, fs_mtime, 1U, str_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    /* Locations */
    {"temp_dir", 8U, fs_temp_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"temp_file", 9U, fs_temp_file, 1U, str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"home_dir", 8U, fs_home_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"config_dir", 10U, fs_config_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"cache_dir", 9U, fs_cache_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"data_dir", 8U, fs_data_dir, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"cwd", 3U, fs_cwd, 0U, NULL, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
};

#define FS_FUNCTION_COUNT \
    (sizeof(vigil_fs_functions) / sizeof(vigil_fs_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_fs = {
    "fs", 2U,
    vigil_fs_functions,
    FS_FUNCTION_COUNT,
    NULL, 0U
};
