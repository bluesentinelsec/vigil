/* BASL standard library: fs module.
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

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"
#include "basl/runtime.h"
#include "platform/platform.h"

#include "internal/basl_nanbox.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool get_string_arg(basl_vm_t *vm, size_t base, size_t idx,
                           const char **str, size_t *len) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    if (!basl_nanbox_is_object(v)) return false;
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(v);
    if (!obj || basl_object_type(obj) != BASL_OBJECT_STRING) return false;
    *str = basl_string_object_c_str(obj);
    *len = basl_string_object_length(obj);
    return true;
}

static basl_status_t push_string(basl_vm_t *vm, const char *str, size_t len,
                                  basl_error_t *error) {
    basl_object_t *obj = NULL;
    basl_status_t s = basl_string_object_new(basl_vm_runtime(vm), str, len, &obj, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_t val;
    basl_value_init_object(&val, &obj);
    s = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return s;
}

static basl_status_t push_bool(basl_vm_t *vm, int b, basl_error_t *error) {
    basl_value_t val;
    basl_value_init_bool(&val, b);
    return basl_vm_stack_push(vm, &val, error);
}

static basl_status_t push_i64(basl_vm_t *vm, int64_t n, basl_error_t *error) {
    basl_value_t val;
    basl_value_init_int(&val, n);
    return basl_vm_stack_push(vm, &val, error);
}

/* ── Path operations ─────────────────────────────────────────────── */

static basl_status_t fs_join(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
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
    
    basl_vm_stack_pop_n(vm, arg_count);
    return push_string(vm, result, strlen(result), error);
}

static basl_status_t fs_clean(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, ".", 1, error);
    }
    
    char result[4096];
    char *parts[256];
    size_t part_count = 0;
    int is_abs = (path_len > 0 && path[0] == '/');
    
    /* Copy and split by / */
    char *copy = malloc(path_len + 1);
    if (!copy) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, path, path_len, error);
    }
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
    
    free(copy);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_string(vm, result, strlen(result), error);
}

static basl_status_t fs_dir(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, ".", 1, error);
    }
    
    /* Find last / */
    const char *last_sep = NULL;
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '/') last_sep = path + i;
    }
    
    basl_vm_stack_pop_n(vm, arg_count);
    if (!last_sep) return push_string(vm, ".", 1, error);
    if (last_sep == path) return push_string(vm, "/", 1, error);
    return push_string(vm, path, (size_t)(last_sep - path), error);
}

static basl_status_t fs_base(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    
    /* Find last / */
    const char *last_sep = NULL;
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '/') last_sep = path + i;
    }
    
    basl_vm_stack_pop_n(vm, arg_count);
    if (!last_sep) return push_string(vm, path, path_len, error);
    return push_string(vm, last_sep + 1, path_len - (size_t)(last_sep - path) - 1, error);
}

static basl_status_t fs_ext(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    
    /* Find last . after last / */
    const char *last_dot = NULL;
    const char *last_sep = NULL;
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '/') last_sep = path + i;
        if (path[i] == '.') last_dot = path + i;
    }
    
    basl_vm_stack_pop_n(vm, arg_count);
    if (!last_dot || (last_sep && last_dot < last_sep)) {
        return push_string(vm, "", 0, error);
    }
    return push_string(vm, last_dot, path_len - (size_t)(last_dot - path), error);
}

static basl_status_t fs_is_abs(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    basl_vm_stack_pop_n(vm, arg_count);
#ifdef _WIN32
    int is_abs = (path_len >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) ||
                 (path_len > 0 && (path[0] == '/' || path[0] == '\\'));
#else
    int is_abs = (path_len > 0 && path[0] == '/');
#endif
    return push_bool(vm, is_abs, error);
}


/* ── File operations ─────────────────────────────────────────────── */

static basl_status_t fs_read(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    char *data = NULL;
    size_t data_len = 0;
    basl_status_t s = basl_platform_read_file(NULL, pathbuf, &data, &data_len, error);
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (s != BASL_STATUS_OK) return push_string(vm, "", 0, error);
    s = push_string(vm, data, data_len, error);
    free(data);
    return s;
}

static basl_status_t fs_write(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path, *data;
    size_t path_len, data_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len) ||
        !get_string_arg(vm, base, 1, &data, &data_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    basl_status_t s = basl_platform_write_file(pathbuf, data, data_len, error);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == BASL_STATUS_OK, error);
}

static basl_status_t fs_append(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path, *data;
    size_t path_len, data_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len) ||
        !get_string_arg(vm, base, 1, &data, &data_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    basl_status_t s = basl_platform_append_file(pathbuf, data, data_len, error);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == BASL_STATUS_OK, error);
}

static basl_status_t fs_copy(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *src, *dst;
    size_t src_len, dst_len;
    
    if (!get_string_arg(vm, base, 0, &src, &src_len) ||
        !get_string_arg(vm, base, 1, &dst, &dst_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char srcbuf[4096], dstbuf[4096];
    snprintf(srcbuf, sizeof(srcbuf), "%.*s", (int)src_len, src);
    snprintf(dstbuf, sizeof(dstbuf), "%.*s", (int)dst_len, dst);
    
    basl_status_t s = basl_platform_copy_file(srcbuf, dstbuf, error);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == BASL_STATUS_OK, error);
}

static basl_status_t fs_move(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *src, *dst;
    size_t src_len, dst_len;
    
    if (!get_string_arg(vm, base, 0, &src, &src_len) ||
        !get_string_arg(vm, base, 1, &dst, &dst_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char srcbuf[4096], dstbuf[4096];
    snprintf(srcbuf, sizeof(srcbuf), "%.*s", (int)src_len, src);
    snprintf(dstbuf, sizeof(dstbuf), "%.*s", (int)dst_len, dst);
    
    basl_status_t s = basl_platform_rename(srcbuf, dstbuf, error);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == BASL_STATUS_OK, error);
}

static basl_status_t fs_remove(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    basl_status_t s = basl_platform_remove(pathbuf, error);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == BASL_STATUS_OK, error);
}

static basl_status_t fs_exists(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int exists = 0;
    basl_platform_file_exists(pathbuf, &exists);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, exists, error);
}

static basl_status_t fs_is_dir(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int is_dir = 0;
    basl_platform_is_directory(pathbuf, &is_dir);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, is_dir, error);
}

static basl_status_t fs_is_file(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int is_file = 0;
    basl_platform_is_file(pathbuf, &is_file);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, is_file, error);
}


/* ── Directory operations ────────────────────────────────────────── */

static basl_status_t fs_mkdir(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    basl_status_t s = basl_platform_mkdir(pathbuf, error);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == BASL_STATUS_OK, error);
}

static basl_status_t fs_mkdir_all(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    basl_status_t s = basl_platform_mkdir_p(pathbuf, error);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_bool(vm, s == BASL_STATUS_OK, error);
}

/* Callback data for list_dir */
typedef struct {
    basl_vm_t *vm;
    basl_object_t *array;
    basl_error_t *error;
} list_ctx_t;

static basl_status_t list_callback(const char *name, int is_dir, void *user_data) {
    (void)is_dir;
    list_ctx_t *ctx = (list_ctx_t *)user_data;
    
    basl_object_t *str = NULL;
    basl_status_t s = basl_string_object_new(basl_vm_runtime(ctx->vm), name, strlen(name), &str, ctx->error);
    if (s != BASL_STATUS_OK) return s;
    
    basl_value_t val;
    basl_value_init_object(&val, &str);
    s = basl_array_object_append(ctx->array, &val, ctx->error);
    basl_value_release(&val);
    return s;
}

static basl_status_t fs_list(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        /* Return empty array */
        basl_object_t *arr = NULL;
        basl_array_object_new(basl_vm_runtime(vm), NULL, 0, &arr, error);
        basl_value_t val;
        basl_value_init_object(&val, &arr);
        basl_status_t s = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return s;
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    basl_object_t *arr = NULL;
    basl_status_t s = basl_array_object_new(basl_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return s;
    }
    
    list_ctx_t ctx = { vm, arr, error };
    basl_platform_list_dir(pathbuf, list_callback, &ctx, error);
    
    basl_vm_stack_pop_n(vm, arg_count);
    basl_value_t val;
    basl_value_init_object(&val, &arr);
    s = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return s;
}

/* Recursive walk helper */
typedef struct {
    basl_vm_t *vm;
    basl_object_t *array;
    basl_error_t *error;
    char base_path[4096];
} walk_ctx_t;

static basl_status_t walk_dir(walk_ctx_t *ctx, const char *dir);

static basl_status_t walk_callback(const char *name, int is_dir, void *user_data) {
    walk_ctx_t *ctx = (walk_ctx_t *)user_data;
    
    char full_path[4096];
    size_t base_len = strlen(ctx->base_path);
    size_t name_len = strlen(name);
    if (base_len + 1 + name_len >= sizeof(full_path)) {
        return BASL_STATUS_OK; /* Skip paths that are too long */
    }
    memcpy(full_path, ctx->base_path, base_len);
    full_path[base_len] = '/';
    memcpy(full_path + base_len + 1, name, name_len + 1);
    
    basl_object_t *str = NULL;
    basl_status_t s = basl_string_object_new(basl_vm_runtime(ctx->vm), full_path, strlen(full_path), &str, ctx->error);
    if (s != BASL_STATUS_OK) return s;
    
    basl_value_t val;
    basl_value_init_object(&val, &str);
    s = basl_array_object_append(ctx->array, &val, ctx->error);
    basl_value_release(&val);
    if (s != BASL_STATUS_OK) return s;
    
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
    
    return BASL_STATUS_OK;
}

static basl_status_t walk_dir(walk_ctx_t *ctx, const char *dir) {
    return basl_platform_list_dir(dir, walk_callback, ctx, ctx->error);
}

static basl_status_t fs_walk(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        basl_object_t *arr = NULL;
        basl_array_object_new(basl_vm_runtime(vm), NULL, 0, &arr, error);
        basl_value_t val;
        basl_value_init_object(&val, &arr);
        basl_status_t s = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return s;
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    basl_object_t *arr = NULL;
    basl_status_t s = basl_array_object_new(basl_vm_runtime(vm), NULL, 0, &arr, error);
    if (s != BASL_STATUS_OK) {
        basl_vm_stack_pop_n(vm, arg_count);
        return s;
    }
    
    walk_ctx_t ctx = { vm, arr, error, "" };
    size_t copy_len = path_len < sizeof(ctx.base_path) - 1 ? path_len : sizeof(ctx.base_path) - 1;
    memcpy(ctx.base_path, pathbuf, copy_len);
    ctx.base_path[copy_len] = '\0';
    walk_dir(&ctx, pathbuf);
    
    basl_vm_stack_pop_n(vm, arg_count);
    basl_value_t val;
    basl_value_init_object(&val, &arr);
    s = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return s;
}


/* ── Metadata and locations ──────────────────────────────────────── */

static basl_status_t fs_size(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int64_t size = basl_platform_file_size(pathbuf);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, size, error);
}

static basl_status_t fs_mtime(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path;
    size_t path_len;
    
    if (!get_string_arg(vm, base, 0, &path, &path_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    
    char pathbuf[4096];
    snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)path_len, path);
    
    int64_t mtime = basl_platform_file_mtime(pathbuf);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, mtime, error);
}

static basl_status_t fs_temp_dir(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    basl_status_t s = basl_platform_temp_dir(&path, error);
    if (s != BASL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static basl_status_t fs_temp_file(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *prefix = "tmp";
    size_t prefix_len = 3;
    
    if (arg_count > 0) {
        get_string_arg(vm, base, 0, &prefix, &prefix_len);
    }
    
    char prefixbuf[64];
    snprintf(prefixbuf, sizeof(prefixbuf), "%.*s", (int)prefix_len, prefix);
    
    basl_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    basl_status_t s = basl_platform_temp_file(prefixbuf, &path, error);
    if (s != BASL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static basl_status_t fs_home_dir(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    basl_status_t s = basl_platform_home_dir(&path, error);
    if (s != BASL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static basl_status_t fs_config_dir(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    basl_status_t s = basl_platform_config_dir(&path, error);
    if (s != BASL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static basl_status_t fs_cache_dir(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    basl_status_t s = basl_platform_cache_dir(&path, error);
    if (s != BASL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static basl_status_t fs_data_dir(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    basl_status_t s = basl_platform_data_dir(&path, error);
    if (s != BASL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

static basl_status_t fs_cwd(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    char *path = NULL;
    basl_status_t s = basl_platform_getcwd(&path, error);
    if (s != BASL_STATUS_OK) return push_string(vm, "", 0, error);
    
    s = push_string(vm, path, strlen(path), error);
    free(path);
    return s;
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_param[] = {BASL_TYPE_STRING};
static const int str_str_params[] = {BASL_TYPE_STRING, BASL_TYPE_STRING};

static const basl_native_module_function_t basl_fs_functions[] = {
    /* Path operations */
    {"join", 4U, fs_join, 2U, str_str_params, BASL_TYPE_STRING, 1U, NULL, 0},
    {"clean", 5U, fs_clean, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"dir", 3U, fs_dir, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"base", 4U, fs_base, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"ext", 3U, fs_ext, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"is_abs", 6U, fs_is_abs, 1U, str_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    /* File operations */
    {"read", 4U, fs_read, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"write", 5U, fs_write, 2U, str_str_params, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"append", 6U, fs_append, 2U, str_str_params, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"copy", 4U, fs_copy, 2U, str_str_params, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"move", 4U, fs_move, 2U, str_str_params, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"remove", 6U, fs_remove, 1U, str_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"exists", 6U, fs_exists, 1U, str_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"is_dir", 6U, fs_is_dir, 1U, str_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"is_file", 7U, fs_is_file, 1U, str_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    /* Directory operations */
    {"mkdir", 5U, fs_mkdir, 1U, str_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"mkdir_all", 9U, fs_mkdir_all, 1U, str_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"list", 4U, fs_list, 1U, str_param, BASL_TYPE_OBJECT, 1U, NULL, BASL_TYPE_STRING},
    {"walk", 4U, fs_walk, 1U, str_param, BASL_TYPE_OBJECT, 1U, NULL, BASL_TYPE_STRING},
    /* Metadata */
    {"size", 4U, fs_size, 1U, str_param, BASL_TYPE_I64, 1U, NULL, 0},
    {"mtime", 5U, fs_mtime, 1U, str_param, BASL_TYPE_I64, 1U, NULL, 0},
    /* Locations */
    {"temp_dir", 8U, fs_temp_dir, 0U, NULL, BASL_TYPE_STRING, 1U, NULL, 0},
    {"temp_file", 9U, fs_temp_file, 1U, str_param, BASL_TYPE_STRING, 1U, NULL, 0},
    {"home_dir", 8U, fs_home_dir, 0U, NULL, BASL_TYPE_STRING, 1U, NULL, 0},
    {"config_dir", 10U, fs_config_dir, 0U, NULL, BASL_TYPE_STRING, 1U, NULL, 0},
    {"cache_dir", 9U, fs_cache_dir, 0U, NULL, BASL_TYPE_STRING, 1U, NULL, 0},
    {"data_dir", 8U, fs_data_dir, 0U, NULL, BASL_TYPE_STRING, 1U, NULL, 0},
    {"cwd", 3U, fs_cwd, 0U, NULL, BASL_TYPE_STRING, 1U, NULL, 0},
};

#define FS_FUNCTION_COUNT \
    (sizeof(basl_fs_functions) / sizeof(basl_fs_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_fs = {
    "fs", 2U,
    basl_fs_functions,
    FS_FUNCTION_COUNT,
    NULL, 0U
};
