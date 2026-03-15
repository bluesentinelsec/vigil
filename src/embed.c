/*
 * basl_embed — generate BASL source modules from embedded file data.
 *
 * Reads files, base64-encodes them, and produces .basl source that
 * exposes the data via base64.decode() at runtime.
 */

#include "basl/embed.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

/* ── dynamic buffer ──────────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} buf_t;

static void buf_init(buf_t *b) { b->data = NULL; b->len = 0; b->cap = 0; }

static void buf_grow(buf_t *b, size_t need) {
    if (b->len + need <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + need) cap *= 2;
    b->data = realloc(b->data, cap);
    b->cap = cap;
}

static void buf_write(buf_t *b, const char *s, size_t n) {
    buf_grow(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void buf_puts(buf_t *b, const char *s) { buf_write(b, s, strlen(s)); }

static void buf_printf(buf_t *b, const char *fmt, ...) {
    va_list ap;
    char tmp[512];
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) buf_write(b, tmp, (size_t)n);
}

/* ── base64 encoder ──────────────────────────────────────────────── */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const unsigned char *data, size_t len, size_t *out_len) {
    size_t olen = 4 * ((len + 2) / 3);
    char *out = malloc(olen + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; ) {
        unsigned int a = i < len ? data[i++] : 0;
        unsigned int b = i < len ? data[i++] : 0;
        unsigned int c = i < len ? data[i++] : 0;
        unsigned int triple = (a << 16) | (b << 8) | c;
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }
    /* Padding. */
    size_t mod = len % 3;
    if (mod == 1) { out[j - 1] = '='; out[j - 2] = '='; }
    else if (mod == 2) { out[j - 1] = '='; }
    out[j] = '\0';
    if (out_len) *out_len = j;
    return out;
}

/* ── name sanitization ───────────────────────────────────────────── */

static void sanitize_name(const char *filename, char *out, size_t out_cap) {
    /* Strip directory and extension, replace non-alnum with _. */
    const char *base = filename;
    for (const char *p = filename; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    /* Find extension. */
    const char *dot = NULL;
    for (const char *p = base; *p; p++) {
        if (*p == '.') dot = p;
    }
    size_t name_len = dot ? (size_t)(dot - base) : strlen(base);
    size_t j = 0;
    for (size_t i = 0; i < name_len && j < out_cap - 1; i++) {
        char c = base[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            out[j++] = c;
        } else if (c >= '0' && c <= '9') {
            if (j == 0) out[j++] = '_';
            out[j++] = c;
        } else {
            out[j++] = '_';
        }
    }
    if (j == 0) { out[j++] = 'e'; out[j++] = 'm'; out[j++] = 'b'; }
    out[j] = '\0';
}

/* Extract just the filename from a path. */
static const char *basename_of(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

/* ── escape a string for BASL string literal ─────────────────────── */

static void buf_write_quoted(buf_t *b, const char *s, size_t len) {
    buf_puts(b, "\"");
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '"') buf_puts(b, "\\\"");
        else if (c == '\\') buf_puts(b, "\\\\");
        else if (c == '\n') buf_puts(b, "\\n");
        else if (c == '\r') buf_puts(b, "\\r");
        else if (c == '\t') buf_puts(b, "\\t");
        else { buf_grow(b, 1); b->data[b->len++] = c; }
    }
    buf_puts(b, "\"");
}

/* ── single file embed ───────────────────────────────────────────── */

basl_status_t basl_embed_single(
    const char *file_path,
    char **out_text,
    size_t *out_length,
    basl_error_t *error
) {
    char *file_data = NULL;
    size_t file_len = 0;
    basl_status_t status;

    status = basl_platform_read_file(NULL, file_path, &file_data, &file_len, error);
    if (status != BASL_STATUS_OK) return status;

    size_t b64_len = 0;
    char *b64 = base64_encode((const unsigned char *)file_data, file_len, &b64_len);
    free(file_data);
    if (!b64) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "out of memory"; error->length = strlen("out of memory"); };
        return BASL_STATUS_INTERNAL;
    }

    const char *base = basename_of(file_path);
    char var_name[256];
    sanitize_name(file_path, var_name, sizeof(var_name));

    buf_t out;
    buf_init(&out);

    buf_puts(&out, "// generated by basl embed — do not edit (base64)\n");
    buf_puts(&out, "import \"base64\";\n\n");
    buf_printf(&out, "pub string name = ");
    buf_write_quoted(&out, base, strlen(base));
    buf_puts(&out, ";\n");
    buf_printf(&out, "pub i32 size = %zu;\n\n", file_len);
    buf_puts(&out, "string _b64 = ");
    buf_write_quoted(&out, b64, b64_len);
    buf_puts(&out, ";\n\n");
    buf_puts(&out, "pub fn bytes() -> (string, err) {\n");
    buf_puts(&out, "    string data, err e1 = base64.decode(_b64);\n");
    buf_puts(&out, "    return (data, e1);\n");
    buf_puts(&out, "}\n");

    free(b64);

    buf_grow(&out, 1);
    out.data[out.len] = '\0';
    *out_text = out.data;
    *out_length = out.len;
    return BASL_STATUS_OK;
}

/* ── multi file embed ────────────────────────────────────────────── */

basl_status_t basl_embed_multi(
    const char **file_paths,
    const char **rel_paths,
    size_t file_count,
    char **out_text,
    size_t *out_length,
    basl_error_t *error
) {
    buf_t out;
    buf_init(&out);

    /* Read and encode all files. */
    char **b64s = calloc(file_count, sizeof(char *));
    size_t *sizes = calloc(file_count, sizeof(size_t));
    if (!b64s || !sizes) {
        free(b64s); free(sizes);
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "out of memory"; error->length = strlen("out of memory"); };
        return BASL_STATUS_INTERNAL;
    }

    for (size_t i = 0; i < file_count; i++) {
        char *file_data = NULL;
        size_t file_len = 0;
        basl_status_t status = basl_platform_read_file(
            NULL, file_paths[i], &file_data, &file_len, error);
        if (status != BASL_STATUS_OK) {
            for (size_t j = 0; j < i; j++) free(b64s[j]);
            free(b64s); free(sizes);
            return status;
        }
        sizes[i] = file_len;
        b64s[i] = base64_encode((const unsigned char *)file_data, file_len, NULL);
        free(file_data);
        if (!b64s[i]) {
            for (size_t j = 0; j < i; j++) free(b64s[j]);
            free(b64s); free(sizes);
            if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "out of memory"; error->length = strlen("out of memory"); };
            return BASL_STATUS_INTERNAL;
        }
    }

    /* Header. */
    buf_printf(&out, "// generated by basl embed — do not edit\n");
    buf_printf(&out, "// source: %zu files\n", file_count);
    buf_puts(&out, "import \"base64\";\n\n");

    /* Blob variables. */
    for (size_t i = 0; i < file_count; i++) {
        buf_printf(&out, "string _%zu = ", i);
        buf_write_quoted(&out, b64s[i], strlen(b64s[i]));
        buf_puts(&out, ";\n");
    }
    buf_puts(&out, "\n");

    /* get(path) function. */
    buf_puts(&out, "pub fn get(string path) -> (string, err) {\n");
    for (size_t i = 0; i < file_count; i++) {
        buf_puts(&out, "    if (path == ");
        buf_write_quoted(&out, rel_paths[i], strlen(rel_paths[i]));
        buf_puts(&out, ") {\n");
        buf_printf(&out, "        string data, err e1 = base64.decode(_%zu);\n", i);
        buf_puts(&out, "        return (data, e1);\n");
        buf_puts(&out, "    }\n");
    }
    buf_puts(&out, "    return (\"\", err(\"embed: not found: \" + path, err.not_found));\n");
    buf_puts(&out, "}\n\n");

    /* list() function. */
    buf_puts(&out, "pub fn list() -> array<string> {\n");
    buf_puts(&out, "    return [");
    for (size_t i = 0; i < file_count; i++) {
        if (i > 0) buf_puts(&out, ", ");
        buf_write_quoted(&out, rel_paths[i], strlen(rel_paths[i]));
    }
    buf_puts(&out, "];\n");
    buf_puts(&out, "}\n\n");

    buf_printf(&out, "pub i32 count = %zu;\n", file_count);

    /* Cleanup. */
    for (size_t i = 0; i < file_count; i++) free(b64s[i]);
    free(b64s);
    free(sizes);

    buf_grow(&out, 1);
    out.data[out.len] = '\0';
    *out_text = out.data;
    *out_length = out.len;
    return BASL_STATUS_OK;
}
