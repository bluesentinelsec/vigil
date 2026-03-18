/* BASL standard library: args module.
 *
 * Provides access to CLI arguments and an argparse builder.
 *
 * Module functions:
 *   args.count() -> i32           returns argument count
 *   args.at(i32 index) -> string  returns argument at index
 *
 * Parser class (builder pattern):
 *   args.Parser.new(string prog, string desc) -> Parser
 *   p.flag(string name, string short, string desc) -> Parser
 *   p.option(string name, string short, string desc, string default) -> Parser
 *   p.option_int(string name, string short, string desc, string default) -> Parser
 *   p.option_multi(string name, string short, string desc) -> Parser
 *   p.required() -> Parser
 *   p.positional(string name, string desc) -> Parser
 *   p.parse(i32 start) -> err
 *   p.get(string name) -> string
 *   p.get_bool(string name) -> bool
 *   p.get_multi(string name) -> array<string>
 *   p.get_positionals() -> array<string>
 *   p.help() -> string
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"

/* ── Parser field indices ────────────────────────────────────────── */

enum {
    F_PROG = 0,        /* string */
    F_DESC,            /* string */
    F_NAMES,           /* array<string> */
    F_SHORTS,          /* array<string> */
    F_TYPES,           /* array<string> */
    F_DEFAULTS,        /* array<string> */
    F_DESCS,           /* array<string> */
    F_REQUIRED,        /* array<string> */
    F_POS_NAMES,       /* array<string> */
    F_POS_DESCS,       /* array<string> */
    F_VALUES,          /* array<string>  key=value pairs after parse */
    F_POSITIONALS,     /* array<string>  positional args after parse */
    FIELD_COUNT
};

/* ── Helpers ─────────────────────────────────────────────────────── */

static basl_object_t *get_self(basl_vm_t *vm, size_t base) {
    basl_value_t v = basl_vm_stack_get(vm, base);
    return (basl_object_t *)basl_nanbox_decode_ptr(v);
}

static basl_object_t *get_field_obj(basl_object_t *self, size_t idx) {
    basl_value_t v;
    basl_instance_object_get_field(self, idx, &v);
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(v);
    return obj;
}

static const char *get_field_str(basl_object_t *self, size_t idx) {
    basl_object_t *obj = get_field_obj(self, idx);
    if (obj == NULL) return "";
    return basl_string_object_c_str(obj);
}

static const char *get_string_val(basl_vm_t *vm, size_t slot) {
    basl_value_t v = basl_vm_stack_get(vm, slot);
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(v);
    if (obj == NULL) return "";
    return basl_string_object_c_str(obj);
}

static basl_status_t make_string(basl_runtime_t *rt, const char *s,
                                 basl_value_t *out, basl_error_t *error) {
    basl_object_t *obj = NULL;
    basl_status_t st = basl_string_object_new_cstr(rt, s, &obj, error);
    if (st != BASL_STATUS_OK) return st;
    basl_value_init_object(out, &obj);
    return BASL_STATUS_OK;
}

static basl_status_t make_empty_array(basl_runtime_t *rt,
                                      basl_value_t *out, basl_error_t *error) {
    basl_object_t *arr = NULL;
    basl_status_t st = basl_array_object_new(rt, NULL, 0, &arr, error);
    if (st != BASL_STATUS_OK) return st;
    basl_value_init_object(out, &arr);
    return BASL_STATUS_OK;
}

static basl_status_t array_push_str(basl_object_t *arr, basl_runtime_t *rt,
                                    const char *s, basl_error_t *error) {
    basl_value_t v;
    basl_status_t st = make_string(rt, s, &v, error);
    if (st != BASL_STATUS_OK) return st;
    st = basl_array_object_append(arr, &v, error);
    basl_value_release(&v);
    return st;
}

static const char *array_get_str(basl_object_t *arr, size_t idx) {
    basl_value_t v;
    if (!basl_array_object_get(arr, idx, &v)) return "";
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(v);
    const char *s = (obj != NULL) ? basl_string_object_c_str(obj) : "";
    basl_value_release(&v);
    return s;
}

/* Push self back as return value (for builder pattern). */
static basl_status_t return_self(basl_vm_t *vm, basl_object_t *self,
                                 size_t arg_count, basl_error_t *error) {
    basl_object_retain(self);
    basl_vm_stack_pop_n(vm, arg_count);
    basl_value_t v = basl_nanbox_encode_object(self);
    basl_status_t st = basl_vm_stack_push(vm, &v, error);
    basl_object_release(&self);
    return st;
}


/* ── Module-level functions (count, at) ──────────────────────────── */

static basl_status_t basl_args_count(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    const char *const *argv = NULL;
    size_t argc = 0;
    basl_value_t val;
    (void)arg_count;
    (void)error;
    basl_vm_get_args(vm, &argv, &argc);
    basl_value_init_int(&val, (int64_t)argc);
    return basl_vm_stack_push(vm, &val, error);
}

static basl_status_t basl_args_at(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    const char *const *argv = NULL;
    size_t argc = 0;
    size_t base;
    int64_t index;
    basl_value_t val;
    basl_object_t *str = NULL;
    basl_status_t status;

    (void)arg_count;
    base = basl_vm_stack_depth(vm) - 1;
    index = basl_value_as_int(&(basl_value_t){basl_vm_stack_get(vm, base)});
    basl_vm_stack_pop_n(vm, 1);
    basl_vm_get_args(vm, &argv, &argc);

    if (index < 0 || (size_t)index >= argc) {
        status = basl_string_object_new(basl_vm_runtime(vm), "", 0, &str, error);
        if (status != BASL_STATUS_OK) return status;
        basl_value_init_object(&val, &str);
        status = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return status;
    }

    status = basl_string_object_new_cstr(basl_vm_runtime(vm), argv[index], &str, error);
    if (status != BASL_STATUS_OK) return status;
    basl_value_init_object(&val, &str);
    status = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return status;
}


/* ── Parser.new (static factory) ─────────────────────────────────── */

static basl_status_t parser_new(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [class_index, prog_str, desc_str] */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    size_t ci = (size_t)basl_nanbox_decode_i32(basl_vm_stack_get(vm, base));
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_value_t fields[FIELD_COUNT];
    basl_object_t *inst = NULL;
    basl_value_t result;
    basl_status_t s;
    size_t i;

    /* Copy prog and desc strings from stack */
    fields[F_PROG] = basl_value_copy(&(basl_value_t){basl_vm_stack_get(vm, base + 1)});
    fields[F_DESC] = basl_value_copy(&(basl_value_t){basl_vm_stack_get(vm, base + 2)});

    /* Create empty arrays for all array fields */
    for (i = F_NAMES; i < FIELD_COUNT; i++) {
        s = make_empty_array(rt, &fields[i], error);
        if (s != BASL_STATUS_OK) goto cleanup;
    }

    s = basl_instance_object_new(rt, ci, fields, FIELD_COUNT, &inst, error);
    if (s != BASL_STATUS_OK) goto cleanup;

    basl_vm_stack_pop_n(vm, arg_count);
    basl_value_init_object(&result, &inst);
    s = basl_vm_stack_push(vm, &result, error);
    basl_value_release(&result);
    for (i = 0; i < FIELD_COUNT; i++) basl_value_release(&fields[i]);
    return s;

cleanup:
    for (i = 0; i < FIELD_COUNT; i++) basl_value_release(&fields[i]);
    return s;
}


/* ── Builder methods (flag, option, option_int, option_multi, required, positional) ── */

/* Append one option definition to the parallel arrays. */
static basl_status_t append_opt(basl_object_t *self, basl_runtime_t *rt,
                                const char *name, const char *sht,
                                const char *typ, const char *def,
                                const char *desc, const char *req,
                                basl_error_t *error) {
    basl_status_t s;
    s = array_push_str(get_field_obj(self, F_NAMES), rt, name, error);
    if (s != BASL_STATUS_OK) return s;
    s = array_push_str(get_field_obj(self, F_SHORTS), rt, sht, error);
    if (s != BASL_STATUS_OK) return s;
    s = array_push_str(get_field_obj(self, F_TYPES), rt, typ, error);
    if (s != BASL_STATUS_OK) return s;
    s = array_push_str(get_field_obj(self, F_DEFAULTS), rt, def, error);
    if (s != BASL_STATUS_OK) return s;
    s = array_push_str(get_field_obj(self, F_DESCS), rt, desc, error);
    if (s != BASL_STATUS_OK) return s;
    return array_push_str(get_field_obj(self, F_REQUIRED), rt, req, error);
}

static basl_status_t parser_flag(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self, name, short, desc] */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht  = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    basl_status_t s = append_opt(self, basl_vm_runtime(vm),
                                 name, sht, "bool", "false", desc, "false", error);
    if (s != BASL_STATUS_OK) return s;
    return return_self(vm, self, arg_count, error);
}

static basl_status_t parser_option(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self, name, short, desc, default] */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht  = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    const char *def  = get_string_val(vm, base + 4);
    basl_status_t s = append_opt(self, basl_vm_runtime(vm),
                                 name, sht, "string", def, desc, "false", error);
    if (s != BASL_STATUS_OK) return s;
    return return_self(vm, self, arg_count, error);
}

static basl_status_t parser_option_int(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht  = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    const char *def  = get_string_val(vm, base + 4);
    basl_status_t s = append_opt(self, basl_vm_runtime(vm),
                                 name, sht, "int", def, desc, "false", error);
    if (s != BASL_STATUS_OK) return s;
    return return_self(vm, self, arg_count, error);
}

static basl_status_t parser_option_multi(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self, name, short, desc] */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *sht  = get_string_val(vm, base + 2);
    const char *desc = get_string_val(vm, base + 3);
    basl_status_t s = append_opt(self, basl_vm_runtime(vm),
                                 name, sht, "multi", "", desc, "false", error);
    if (s != BASL_STATUS_OK) return s;
    return return_self(vm, self, arg_count, error);
}

static basl_status_t parser_mark_required(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* Mark the last-added option as required. stack: [self] */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    basl_object_t *req_arr = get_field_obj(self, F_REQUIRED);
    size_t len = basl_array_object_length(req_arr);
    if (len > 0) {
        basl_value_t v;
        basl_status_t s = make_string(basl_vm_runtime(vm), "true", &v, error);
        if (s != BASL_STATUS_OK) return s;
        s = basl_array_object_set(req_arr, len - 1, &v, error);
        basl_value_release(&v);
        if (s != BASL_STATUS_OK) return s;
    }
    return return_self(vm, self, arg_count, error);
}

static basl_status_t parser_positional(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self, name, desc] */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    const char *desc = get_string_val(vm, base + 2);
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_status_t s;
    s = array_push_str(get_field_obj(self, F_POS_NAMES), rt, name, error);
    if (s != BASL_STATUS_OK) return s;
    s = array_push_str(get_field_obj(self, F_POS_DESCS), rt, desc, error);
    if (s != BASL_STATUS_OK) return s;
    return return_self(vm, self, arg_count, error);
}


/* ── parse() — the core argument parser ──────────────────────────── */

/* Find option index by long or short name. Returns -1 if not found. */
static int find_opt_idx(basl_object_t *names_arr, basl_object_t *shorts_arr,
                        const char *key) {
    size_t len = basl_array_object_length(names_arr);
    size_t i;
    for (i = 0; i < len; i++) {
        const char *n = array_get_str(names_arr, i);
        const char *s = array_get_str(shorts_arr, i);
        if (strcmp(n, key) == 0 || (s[0] != '\0' && strcmp(s, key) == 0))
            return (int)i;
    }
    return -1;
}

static basl_status_t parser_parse(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self, start_i32] */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    int64_t start = basl_value_as_int(
        &(basl_value_t){basl_vm_stack_get(vm, base + 1)});
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_status_t s;

    const char *const *argv = NULL;
    size_t argc = 0;
    basl_vm_get_args(vm, &argv, &argc);

    basl_object_t *names_arr = get_field_obj(self, F_NAMES);
    basl_object_t *shorts_arr = get_field_obj(self, F_SHORTS);
    basl_object_t *types_arr = get_field_obj(self, F_TYPES);
    basl_object_t *defaults_arr = get_field_obj(self, F_DEFAULTS);
    basl_object_t *req_arr = get_field_obj(self, F_REQUIRED);
    size_t opt_count = basl_array_object_length(names_arr);

    /* Clear and rebuild values + positionals arrays */
    basl_object_t *vals_arr = NULL;
    basl_object_t *pos_arr = NULL;
    s = basl_array_object_new(rt, NULL, 0, &vals_arr, error);
    if (s != BASL_STATUS_OK) return s;
    s = basl_array_object_new(rt, NULL, 0, &pos_arr, error);
    if (s != BASL_STATUS_OK) { basl_object_release(&vals_arr); return s; }

    /* Set defaults */
    size_t i;
    for (i = 0; i < opt_count; i++) {
        const char *name = array_get_str(names_arr, i);
        const char *typ = array_get_str(types_arr, i);
        const char *def = array_get_str(defaults_arr, i);
        char buf[512];
        if (strcmp(typ, "bool") == 0)
            snprintf(buf, sizeof(buf), "%s=false", name);
        else if (strcmp(typ, "multi") == 0)
            continue; /* multi values tracked separately */
        else
            snprintf(buf, sizeof(buf), "%s=%s", name, def);
        s = array_push_str(vals_arr, rt, buf, error);
        if (s != BASL_STATUS_OK) goto fail;
    }

    /* Parse loop */
    int64_t pos = start;
    int past_dd = 0;
    char err_buf[256];

    while (pos >= 0 && (size_t)pos < argc) {
        const char *arg = argv[pos];

        if (!past_dd && strcmp(arg, "--") == 0) {
            past_dd = 1; pos++; continue;
        }

        if (past_dd || arg[0] != '-') {
            s = array_push_str(pos_arr, rt, arg, error);
            if (s != BASL_STATUS_OK) goto fail;
            pos++; continue;
        }

        /* Extract key and optional inline value from --key=value */
        const char *key = NULL;
        const char *inline_val = NULL;
        char key_buf[256];

        if (arg[0] == '-' && arg[1] == '-') {
            const char *eq = strchr(arg + 2, '=');
            if (eq != NULL) {
                size_t klen = (size_t)(eq - (arg + 2));
                if (klen >= sizeof(key_buf)) klen = sizeof(key_buf) - 1;
                memcpy(key_buf, arg + 2, klen);
                key_buf[klen] = '\0';
                key = key_buf;
                inline_val = eq + 1;
            } else {
                key = arg + 2;
            }
        } else {
            key = arg + 1;
        }

        int idx = find_opt_idx(names_arr, shorts_arr, key);
        if (idx < 0) {
            snprintf(err_buf, sizeof(err_buf), "unknown option: %s", arg);
            goto err_out;
        }

        const char *name = array_get_str(names_arr, (size_t)idx);
        const char *typ = array_get_str(types_arr, (size_t)idx);

        if (strcmp(typ, "bool") == 0) {
            /* Update the existing entry in vals_arr */
            char buf[512];
            snprintf(buf, sizeof(buf), "%s=true", name);
            /* Find and replace in vals_arr */
            size_t vi;
            size_t vlen = basl_array_object_length(vals_arr);
            size_t nlen = strlen(name);
            for (vi = 0; vi < vlen; vi++) {
                const char *entry = array_get_str(vals_arr, vi);
                if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=') {
                    basl_value_t sv;
                    s = make_string(rt, buf, &sv, error);
                    if (s != BASL_STATUS_OK) goto fail;
                    s = basl_array_object_set(vals_arr, vi, &sv, error);
                    basl_value_release(&sv);
                    if (s != BASL_STATUS_OK) goto fail;
                    break;
                }
            }
            pos++;
        } else {
            /* string, int, or multi — needs a value */
            const char *val = NULL;
            if (inline_val != NULL) {
                val = inline_val;
            } else {
                pos++;
                if ((size_t)pos >= argc) {
                    snprintf(err_buf, sizeof(err_buf),
                             "option --%s requires a value", name);
                    goto err_out;
                }
                val = argv[pos];
            }

            /* int validation */
            if (strcmp(typ, "int") == 0) {
                char *end = NULL;
                errno = 0;
                (void)strtol(val, &end, 10);
                if (errno != 0 || end == val || *end != '\0') {
                    snprintf(err_buf, sizeof(err_buf),
                             "option --%s requires an integer, got: %s",
                             name, val);
                    goto err_out;
                }
            }

            if (strcmp(typ, "multi") == 0) {
                /* Append as __multi__name=value */
                char buf[512];
                snprintf(buf, sizeof(buf), "__multi__%s=%s", name, val);
                s = array_push_str(vals_arr, rt, buf, error);
                if (s != BASL_STATUS_OK) goto fail;
            } else {
                /* Update existing entry */
                char buf[512];
                snprintf(buf, sizeof(buf), "%s=%s", name, val);
                size_t vi;
                size_t vlen = basl_array_object_length(vals_arr);
                size_t nlen = strlen(name);
                for (vi = 0; vi < vlen; vi++) {
                    const char *entry = array_get_str(vals_arr, vi);
                    if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=') {
                        basl_value_t sv;
                        s = make_string(rt, buf, &sv, error);
                        if (s != BASL_STATUS_OK) goto fail;
                        s = basl_array_object_set(vals_arr, vi, &sv, error);
                        basl_value_release(&sv);
                        if (s != BASL_STATUS_OK) goto fail;
                        break;
                    }
                }
            }
            pos++;
        }
    }

    /* Check required options */
    for (i = 0; i < opt_count; i++) {
        const char *req = array_get_str(req_arr, i);
        if (strcmp(req, "true") != 0) continue;
        const char *name = array_get_str(names_arr, i);
        const char *typ = array_get_str(types_arr, i);
        if (strcmp(typ, "bool") == 0) continue;

        if (strcmp(typ, "multi") == 0) {
            /* Check if any __multi__name= entry exists */
            char prefix[280];
            snprintf(prefix, sizeof(prefix), "__multi__%s=", name);
            size_t plen = strlen(prefix);
            int found = 0;
            size_t vi;
            size_t vlen = basl_array_object_length(vals_arr);
            for (vi = 0; vi < vlen; vi++) {
                const char *entry = array_get_str(vals_arr, vi);
                if (strncmp(entry, prefix, plen) == 0) { found = 1; break; }
            }
            if (!found) {
                snprintf(err_buf, sizeof(err_buf),
                         "required option --%s not provided", name);
                goto err_out;
            }
        } else {
            /* Check if value is non-empty */
            char prefix[280];
            snprintf(prefix, sizeof(prefix), "%s=", name);
            size_t plen = strlen(prefix);
            size_t vi;
            size_t vlen = basl_array_object_length(vals_arr);
            for (vi = 0; vi < vlen; vi++) {
                const char *entry = array_get_str(vals_arr, vi);
                if (strncmp(entry, prefix, plen) == 0) {
                    if (entry[plen] == '\0') {
                        snprintf(err_buf, sizeof(err_buf),
                                 "required option --%s not provided", name);
                        goto err_out;
                    }
                    break;
                }
            }
        }
    }

    /* Store results in instance fields */
    {
        basl_value_t vv;
        basl_value_init_object(&vv, &vals_arr);
        s = basl_instance_object_set_field(self, F_VALUES, &vv, error);
        basl_value_release(&vv);
        if (s != BASL_STATUS_OK) goto fail;
        basl_value_init_object(&vv, &pos_arr);
        s = basl_instance_object_set_field(self, F_POSITIONALS, &vv, error);
        basl_value_release(&vv);
        if (s != BASL_STATUS_OK) goto fail;
    }

    /* Return ok */
    basl_vm_stack_pop_n(vm, arg_count);
    {
        basl_object_t *ok_obj = NULL;
        s = basl_error_object_new_cstr(rt, "", 0, &ok_obj, error);
        if (s != BASL_STATUS_OK) return s;
        basl_value_t ev;
        basl_value_init_object(&ev, &ok_obj);
        s = basl_vm_stack_push(vm, &ev, error);
        basl_value_release(&ev);
    }
    return s;

err_out:
    basl_object_release(&vals_arr);
    basl_object_release(&pos_arr);
    basl_vm_stack_pop_n(vm, arg_count);
    {
        basl_object_t *err_obj = NULL;
        s = basl_error_object_new_cstr(rt, err_buf, 1, &err_obj, error);
        if (s != BASL_STATUS_OK) return s;
        basl_value_t ev;
        basl_value_init_object(&ev, &err_obj);
        s = basl_vm_stack_push(vm, &ev, error);
        basl_value_release(&ev);
    }
    return s;

fail:
    basl_object_release(&vals_arr);
    basl_object_release(&pos_arr);
    return s;
}


/* ── Getter methods ──────────────────────────────────────────────── */

static basl_status_t parser_get(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self, name] -> string */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    basl_object_t *vals = get_field_obj(self, F_VALUES);
    size_t nlen = strlen(name);
    size_t vlen = basl_array_object_length(vals);
    const char *result = "";
    size_t i;

    for (i = 0; i < vlen; i++) {
        const char *entry = array_get_str(vals, i);
        if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=') {
            result = entry + nlen + 1;
            break;
        }
    }

    basl_vm_stack_pop_n(vm, arg_count);
    basl_object_t *str = NULL;
    basl_status_t s = basl_string_object_new_cstr(basl_vm_runtime(vm), result, &str, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_t v;
    basl_value_init_object(&v, &str);
    s = basl_vm_stack_push(vm, &v, error);
    basl_value_release(&v);
    return s;
}

static basl_status_t parser_get_bool(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self, name] -> bool */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    basl_object_t *vals = get_field_obj(self, F_VALUES);
    size_t nlen = strlen(name);
    size_t vlen = basl_array_object_length(vals);
    int result = 0;
    size_t i;

    for (i = 0; i < vlen; i++) {
        const char *entry = array_get_str(vals, i);
        if (strncmp(entry, name, nlen) == 0 && entry[nlen] == '=') {
            result = strcmp(entry + nlen + 1, "true") == 0;
            break;
        }
    }

    basl_vm_stack_pop_n(vm, arg_count);
    basl_value_t v;
    basl_value_init_bool(&v, result);
    return basl_vm_stack_push(vm, &v, error);
}

static basl_status_t parser_get_multi(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self, name] -> array<string> */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    const char *name = get_string_val(vm, base + 1);
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_object_t *vals = get_field_obj(self, F_VALUES);
    size_t vlen = basl_array_object_length(vals);
    basl_status_t s;

    char prefix[280];
    snprintf(prefix, sizeof(prefix), "__multi__%s=", name);
    size_t plen = strlen(prefix);

    basl_object_t *result = NULL;
    s = basl_array_object_new(rt, NULL, 0, &result, error);
    if (s != BASL_STATUS_OK) return s;

    size_t i;
    for (i = 0; i < vlen; i++) {
        const char *entry = array_get_str(vals, i);
        if (strncmp(entry, prefix, plen) == 0) {
            s = array_push_str(result, rt, entry + plen, error);
            if (s != BASL_STATUS_OK) { basl_object_release(&result); return s; }
        }
    }

    basl_vm_stack_pop_n(vm, arg_count);
    basl_value_t v;
    basl_value_init_object(&v, &result);
    s = basl_vm_stack_push(vm, &v, error);
    basl_value_release(&v);
    return s;
}

static basl_status_t parser_get_positionals(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self] -> array<string> */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    basl_value_t field_val;
    basl_instance_object_get_field(self, F_POSITIONALS, &field_val);
    basl_value_t copy = basl_value_copy(&field_val);
    basl_value_release(&field_val);
    basl_vm_stack_pop_n(vm, arg_count);
    basl_status_t s = basl_vm_stack_push(vm, &copy, error);
    basl_value_release(&copy);
    return s;
}


/* ── help() — generate help text ─────────────────────────────────── */

static basl_status_t parser_help(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    /* stack: [self] -> string */
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_object_t *self = get_self(vm, base);
    const char *prog = get_field_str(self, F_PROG);
    const char *desc = get_field_str(self, F_DESC);
    basl_object_t *names_arr = get_field_obj(self, F_NAMES);
    basl_object_t *shorts_arr = get_field_obj(self, F_SHORTS);
    basl_object_t *types_arr = get_field_obj(self, F_TYPES);
    basl_object_t *defaults_arr = get_field_obj(self, F_DEFAULTS);
    basl_object_t *descs_arr = get_field_obj(self, F_DESCS);
    basl_object_t *req_arr = get_field_obj(self, F_REQUIRED);
    basl_object_t *pn_arr = get_field_obj(self, F_POS_NAMES);
    basl_object_t *pd_arr = get_field_obj(self, F_POS_DESCS);
    size_t opt_count = basl_array_object_length(names_arr);
    size_t pos_count = basl_array_object_length(pn_arr);

    /* Build help string into a buffer */
    char buf[4096];
    size_t off = 0;
    size_t i;

    off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                            "Usage: %s [options]", prog);
    for (i = 0; i < pos_count; i++) {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                                " <%s>", array_get_str(pn_arr, i));
    }
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\n\n%s\n", desc);

    if (opt_count > 0) {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\nOptions:\n");
        for (i = 0; i < opt_count; i++) {
            const char *name = array_get_str(names_arr, i);
            const char *sht = array_get_str(shorts_arr, i);
            const char *typ = array_get_str(types_arr, i);
            const char *def = array_get_str(defaults_arr, i);
            const char *d = array_get_str(descs_arr, i);
            const char *req = array_get_str(req_arr, i);

            char flag[64];
            if (sht[0] != '\0')
                snprintf(flag, sizeof(flag), "  --%s, -%s", name, sht);
            else
                snprintf(flag, sizeof(flag), "  --%s", name);

            if (strcmp(typ, "string") == 0 || strcmp(typ, "int") == 0 ||
                strcmp(typ, "multi") == 0) {
                size_t fl = strlen(flag);
                snprintf(flag + fl, sizeof(flag) - fl, " VALUE");
            }

            /* Pad to 30 chars */
            size_t fl = strlen(flag);
            while (fl < 30 && fl < sizeof(flag) - 1) { flag[fl++] = ' '; }
            flag[fl] = '\0';

            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", flag, d);
            if (strcmp(req, "true") == 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off, " (required)");
            else if (def[0] != '\0' && strcmp(typ, "bool") != 0)
                off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                                        " [default: %s]", def);
            off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\n");
            if (off >= sizeof(buf) - 1) break;
        }
    }

    if (pos_count > 0) {
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "\nArguments:\n");
        for (i = 0; i < pos_count && off < sizeof(buf) - 1; i++) {
            char pname[64];
            snprintf(pname, sizeof(pname), "  %s", array_get_str(pn_arr, i));
            size_t pl = strlen(pname);
            while (pl < 30 && pl < sizeof(pname) - 1) { pname[pl++] = ' '; }
            pname[pl] = '\0';
            off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                                    "%s%s\n", pname, array_get_str(pd_arr, i));
        }
    }

    /* Remove trailing newline */
    if (off > 0 && buf[off - 1] == '\n') off--;
    buf[off] = '\0';

    basl_vm_stack_pop_n(vm, arg_count);
    basl_object_t *str = NULL;
    basl_status_t s = basl_string_object_new(basl_vm_runtime(vm), buf, off, &str, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_t v;
    basl_value_init_object(&v, &str);
    s = basl_vm_stack_push(vm, &v, error);
    basl_value_release(&v);
    return s;
}


/* ── Module descriptor ───────────────────────────────────────────── */

static const int basl_args_at_params[] = { BASL_TYPE_I32 };

static const basl_native_module_function_t basl_args_functions[] = {
    { "count", 5, basl_args_count, 0, NULL,
      BASL_TYPE_I32, 1, NULL, 0, NULL, NULL },
    { "at", 2, basl_args_at, 1, basl_args_at_params,
      BASL_TYPE_STRING, 1, NULL, 0, NULL, NULL }
};

/* ── Parser class descriptor ─────────────────────────────────────── */

#define BASL_PFIELD(n, nl, t) { n, nl, t, 0, NULL, 0U, 0 }
#define BASL_AFIELD(n, nl, elem) { n, nl, BASL_TYPE_OBJECT, BASL_NATIVE_FIELD_ARRAY, NULL, 0U, elem }
#define BASL_METHOD(n, nl, fn, pc, pt, rt, rc, rts) \
    { n, nl, fn, pc, pt, rt, rc, rts, 0, NULL, 0U, 0 }
#define BASL_METHOD_ARR(n, nl, fn, pc, pt, rc, rts, elem) \
    { n, nl, fn, pc, pt, BASL_TYPE_OBJECT, rc, rts, 0, NULL, 0U, elem }
#define BASL_STATIC(n, nl, fn, pc, pt, rt, rc, rts) \
    { n, nl, fn, pc, pt, rt, rc, rts, 1, NULL, 0U, 0 }

static const basl_native_class_field_t parser_fields[] = {
    BASL_PFIELD("prog",       4U, BASL_TYPE_STRING),
    BASL_PFIELD("desc",       4U, BASL_TYPE_STRING),
    BASL_AFIELD("names",      5U, BASL_TYPE_STRING),
    BASL_AFIELD("shorts",     6U, BASL_TYPE_STRING),
    BASL_AFIELD("types",      5U, BASL_TYPE_STRING),
    BASL_AFIELD("defaults",   8U, BASL_TYPE_STRING),
    BASL_AFIELD("descs",      5U, BASL_TYPE_STRING),
    BASL_AFIELD("required",   8U, BASL_TYPE_STRING),
    BASL_AFIELD("pos_names",  9U, BASL_TYPE_STRING),
    BASL_AFIELD("pos_descs",  9U, BASL_TYPE_STRING),
    BASL_AFIELD("values",     6U, BASL_TYPE_STRING),
    BASL_AFIELD("positionals",11U, BASL_TYPE_STRING),
};

static const int str3_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int str4_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING, BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int str2_params[] = { BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int str1_params[] = { BASL_TYPE_STRING };
static const int i32_params[] = { BASL_TYPE_I32 };

static const basl_native_class_method_t parser_methods[] = {
    /* static factory */
    BASL_STATIC("new", 3U, parser_new, 2U, str2_params,
                BASL_TYPE_OBJECT, 1U, NULL),
    /* builder methods (return self = OBJECT, same class) */
    BASL_METHOD("flag", 4U, parser_flag, 3U, str3_params,
                BASL_TYPE_OBJECT, 1U, NULL),
    BASL_METHOD("option", 6U, parser_option, 4U, str4_params,
                BASL_TYPE_OBJECT, 1U, NULL),
    BASL_METHOD("option_int", 10U, parser_option_int, 4U, str4_params,
                BASL_TYPE_OBJECT, 1U, NULL),
    BASL_METHOD("option_multi", 12U, parser_option_multi, 3U, str3_params,
                BASL_TYPE_OBJECT, 1U, NULL),
    BASL_METHOD("mark_required", 13U, parser_mark_required, 0U, NULL,
                BASL_TYPE_OBJECT, 1U, NULL),
    BASL_METHOD("positional", 10U, parser_positional, 2U, str2_params,
                BASL_TYPE_OBJECT, 1U, NULL),
    /* terminal methods */
    BASL_METHOD("parse", 5U, parser_parse, 1U, i32_params,
                BASL_TYPE_ERR, 1U, NULL),
    BASL_METHOD("get", 3U, parser_get, 1U, str1_params,
                BASL_TYPE_STRING, 1U, NULL),
    BASL_METHOD("get_bool", 8U, parser_get_bool, 1U, str1_params,
                BASL_TYPE_BOOL, 1U, NULL),
    BASL_METHOD_ARR("get_multi", 9U, parser_get_multi, 1U, str1_params,
                1U, NULL, BASL_TYPE_STRING),
    BASL_METHOD_ARR("get_positionals", 15U, parser_get_positionals, 0U, NULL,
                1U, NULL, BASL_TYPE_STRING),
    BASL_METHOD("help", 4U, parser_help, 0U, NULL,
                BASL_TYPE_STRING, 1U, NULL),
};

#undef BASL_PFIELD
#undef BASL_AFIELD
#undef BASL_METHOD
#undef BASL_STATIC

static const basl_native_class_t basl_args_classes[] = {
    { "Parser", 6U, parser_fields, FIELD_COUNT, parser_methods,
      sizeof(parser_methods) / sizeof(parser_methods[0]), NULL },
};

BASL_API const basl_native_module_t basl_stdlib_args = {
    "args", 4,
    basl_args_functions,
    sizeof(basl_args_functions) / sizeof(basl_args_functions[0]),
    basl_args_classes, 1U
};
