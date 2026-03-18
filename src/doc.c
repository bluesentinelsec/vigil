#include "vigil/doc.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Allocation helpers ──────────────────────────────────────────── */

static void *doc_alloc(const vigil_allocator_t *a, size_t size) {
    if (a != NULL && a->allocate != NULL)
        return a->allocate(a->user_data, size);
    return malloc(size);
}

static void *doc_realloc(const vigil_allocator_t *a, void *p, size_t size) {
    if (a != NULL && a->reallocate != NULL)
        return a->reallocate(a->user_data, p, size);
    return realloc(p, size);
}

static void doc_free_ptr(const vigil_allocator_t *a, void *p) {
    if (p == NULL) return;
    if (a != NULL && a->deallocate != NULL) {
        a->deallocate(a->user_data, p);
        return;
    }
    free(p);
}

static char *doc_strdup(const vigil_allocator_t *a, const char *s, size_t len) {
    char *out;
    if (s == NULL || len == 0) return NULL;
    out = (char *)doc_alloc(a, len + 1);
    if (out == NULL) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

/* ── String builder ──────────────────────────────────────────────── */

typedef struct doc_buf {
    const vigil_allocator_t *allocator;
    char *data;
    size_t length;
    size_t capacity;
} doc_buf_t;

static void buf_init(doc_buf_t *b, const vigil_allocator_t *a) {
    b->allocator = a;
    b->data = NULL;
    b->length = 0;
    b->capacity = 0;
}

static int buf_grow(doc_buf_t *b, size_t needed) {
    size_t cap;
    char *p;
    if (b->length + needed + 1 <= b->capacity) return 1;
    cap = b->capacity == 0 ? 256 : b->capacity;
    while (cap < b->length + needed + 1) cap *= 2;
    p = (char *)doc_realloc(b->allocator, b->data, cap);
    if (p == NULL) return 0;
    b->data = p;
    b->capacity = cap;
    return 1;
}

static void buf_append(doc_buf_t *b, const char *s, size_t len) {
    if (len == 0 || s == NULL) return;
    if (!buf_grow(b, len)) return;
    memcpy(b->data + b->length, s, len);
    b->length += len;
    b->data[b->length] = '\0';
}

static void buf_append_cstr(doc_buf_t *b, const char *s) {
    if (s != NULL) buf_append(b, s, strlen(s));
}

static void buf_append_char(doc_buf_t *b, char c) {
    buf_append(b, &c, 1);
}

static void buf_append_indent(doc_buf_t *b, int spaces) {
    int i;
    for (i = 0; i < spaces; i++) buf_append_char(b, ' ');
}

static void buf_free(doc_buf_t *b) {
    doc_free_ptr(b->allocator, b->data);
    b->data = NULL;
    b->length = 0;
    b->capacity = 0;
}

/* ── Token helpers ───────────────────────────────────────────────── */

static const vigil_token_t *tok_at(const vigil_token_list_t *tokens, size_t i) {
    return vigil_token_list_get(tokens, i);
}

static const char *tok_text(const char *src, const vigil_token_t *t, size_t *out_len) {
    size_t len;
    if (t == NULL) { if (out_len) *out_len = 0; return NULL; }
    len = t->span.end_offset - t->span.start_offset;
    if (out_len) *out_len = len;
    return src + t->span.start_offset;
}

static int tok_is_ident_text(const char *src, const vigil_token_t *t,
                              const char *text, size_t text_len) {
    size_t len;
    const char *s;
    if (t == NULL || t->kind != VIGIL_TOKEN_IDENTIFIER) return 0;
    s = tok_text(src, t, &len);
    return len == text_len && memcmp(s, text, len) == 0;
}

static int tok_is_type_start(const vigil_token_t *t) {
    if (t == NULL) return 0;
    return t->kind == VIGIL_TOKEN_IDENTIFIER || t->kind == VIGIL_TOKEN_FN;
}

/* ── Comment extraction ──────────────────────────────────────────── */

static void extract_comment_before(const vigil_allocator_t *a,
                                    const char *src, size_t src_len,
                                    size_t decl_offset,
                                    vigil_doc_comment_t *out) {
    size_t end;
    doc_buf_t buf;
    int first;

    (void)src_len;
    out->text = NULL;
    out->length = 0;
    if (decl_offset == 0 || src == NULL) return;

    /* Move to the end of the line immediately before the declaration line. */
    end = decl_offset;
    /* Back up to start of declaration line. */
    while (end > 0 && src[end - 1] != '\n') end--;
    /* Now end is at the start of the decl line (or 0). Skip the \n. */
    if (end > 0) end--; /* skip \n */
    /* Skip trailing \r. */
    while (end > 0 && src[end - 1] == '\r') end--;

    /* Walk backward collecting // comment lines. */
    buf_init(&buf, a);
    first = 1;

    while (end > 0) {
        size_t line_end = end;
        size_t line_start = end;
        const char *trimmed;
        size_t trimmed_len;

        /* Find start of this line. */
        while (line_start > 0 && src[line_start - 1] != '\n')
            line_start--;

        /* Trim leading whitespace. */
        trimmed = src + line_start;
        trimmed_len = line_end - line_start;
        while (trimmed_len > 0 && (*trimmed == ' ' || *trimmed == '\t')) {
            trimmed++;
            trimmed_len--;
        }

        /* Must start with // */
        if (trimmed_len < 2 || trimmed[0] != '/' || trimmed[1] != '/') break;

        /* Strip the // prefix and optional space. */
        trimmed += 2;
        trimmed_len -= 2;
        if (trimmed_len > 0 && *trimmed == ' ') {
            trimmed++;
            trimmed_len--;
        }

        /* Prepend to buffer (we're going backward). */
        if (!first) {
            /* Insert newline before previous content. */
            char *old = buf.data;
            size_t old_len = buf.length;
            doc_buf_t new_buf;
            buf_init(&new_buf, a);
            buf_append(&new_buf, trimmed, trimmed_len);
            buf_append_char(&new_buf, '\n');
            buf_append(&new_buf, old, old_len);
            buf_free(&buf);
            buf = new_buf;
        } else {
            buf_append(&buf, trimmed, trimmed_len);
            first = 0;
        }

        /* Move to previous line. */
        if (line_start == 0) break;
        end = line_start - 1; /* skip \n before this line */
        while (end > 0 && src[end - 1] == '\r') end--;
    }

    if (buf.length > 0) {
        out->text = buf.data;
        out->length = buf.length;
    } else {
        buf_free(&buf);
    }
}

static void extract_module_summary(const vigil_allocator_t *a,
                                    const char *src, size_t src_len,
                                    size_t first_decl_offset,
                                    vigil_doc_comment_t *out) {
    /* Module summary = leading // comments before any declaration. */
    size_t pos = 0;
    doc_buf_t buf;
    int found = 0;

    out->text = NULL;
    out->length = 0;
    if (src == NULL || first_decl_offset == 0) return;

    /* Skip leading blank lines. */
    while (pos < first_decl_offset && (src[pos] == ' ' || src[pos] == '\t' ||
                                        src[pos] == '\r' || src[pos] == '\n'))
        pos++;

    if (pos >= first_decl_offset) return;
    if (src[pos] != '/') return;
    if (pos + 1 >= src_len || src[pos + 1] != '/') return;

    buf_init(&buf, a);

    while (pos < first_decl_offset) {
        const char *line_start = src + pos;
        size_t line_len = 0;
        const char *trimmed;
        size_t trimmed_len;

        /* Find end of line. */
        while (pos + line_len < src_len && src[pos + line_len] != '\n')
            line_len++;

        trimmed = line_start;
        trimmed_len = line_len;
        /* Trim trailing \r */
        if (trimmed_len > 0 && trimmed[trimmed_len - 1] == '\r')
            trimmed_len--;
        /* Trim leading whitespace. */
        while (trimmed_len > 0 && (*trimmed == ' ' || *trimmed == '\t')) {
            trimmed++;
            trimmed_len--;
        }

        if (trimmed_len < 2 || trimmed[0] != '/' || trimmed[1] != '/') break;

        trimmed += 2;
        trimmed_len -= 2;
        if (trimmed_len > 0 && *trimmed == ' ') {
            trimmed++;
            trimmed_len--;
        }

        if (found) buf_append_char(&buf, '\n');
        buf_append(&buf, trimmed, trimmed_len);
        found = 1;

        pos += line_len;
        if (pos < src_len && src[pos] == '\n') pos++;
    }

    if (buf.length > 0) {
        out->text = buf.data;
        out->length = buf.length;
    } else {
        buf_free(&buf);
    }
}

/* ── Type text extraction from tokens ────────────────────────────── */

static void extract_type_text(const char *src, const vigil_token_list_t *tokens,
                               size_t *cursor, doc_buf_t *buf) {
    /* Extracts a type expression: identifier, module.Type, array<T>, map<K,V>, fn(...) -> R */
    const vigil_token_t *t = tok_at(tokens, *cursor);
    size_t len;
    const char *text;
    int angle_depth;

    if (t == NULL) return;

    text = tok_text(src, t, &len);
    buf_append(buf, text, len);
    (*cursor)++;

    /* Handle dot-qualified types: math.Vec3, module.Type */
    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_DOT) {
        buf_append_char(buf, '.');
        (*cursor)++;
        t = tok_at(tokens, *cursor);
        if (t != NULL) {
            text = tok_text(src, t, &len);
            buf_append(buf, text, len);
            (*cursor)++;
        }
        t = tok_at(tokens, *cursor);
    }

    /* Handle generic types: array<T>, map<K,V> */
    if (t != NULL && t->kind == VIGIL_TOKEN_LESS) {
        buf_append_char(buf, '<');
        (*cursor)++;
        angle_depth = 1;
        while (angle_depth > 0) {
            t = tok_at(tokens, *cursor);
            if (t == NULL || t->kind == VIGIL_TOKEN_EOF) break;
            if (t->kind == VIGIL_TOKEN_LESS) {
                angle_depth++;
                buf_append_char(buf, '<');
                (*cursor)++;
            } else if (t->kind == VIGIL_TOKEN_GREATER) {
                angle_depth--;
                buf_append_char(buf, '>');
                (*cursor)++;
            } else if (t->kind == VIGIL_TOKEN_SHIFT_RIGHT) {
                /* >> token closes two levels of nested generics */
                angle_depth -= 2;
                buf_append_cstr(buf, ">>");
                (*cursor)++;
            } else if (t->kind == VIGIL_TOKEN_COMMA) {
                buf_append_cstr(buf, ", ");
                (*cursor)++;
            } else {
                text = tok_text(src, t, &len);
                buf_append(buf, text, len);
                (*cursor)++;
            }
        }
    }
}

/* ── Parameter list extraction ───────────────────────────────────── */

static vigil_status_t extract_params(const vigil_allocator_t *a,
                                     const char *src,
                                     const vigil_token_list_t *tokens,
                                     size_t *cursor,
                                     vigil_doc_param_t **out_params,
                                     size_t *out_count) {
    const vigil_token_t *t;
    size_t cap = 4, count = 0;
    vigil_doc_param_t *params;

    *out_params = NULL;
    *out_count = 0;

    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LPAREN) return VIGIL_STATUS_OK;
    (*cursor)++;

    params = (vigil_doc_param_t *)doc_alloc(a, cap * sizeof(vigil_doc_param_t));
    if (params == NULL) return VIGIL_STATUS_OUT_OF_MEMORY;

    while (1) {
        doc_buf_t type_buf;
        size_t name_len;
        const char *name_text;

        t = tok_at(tokens, *cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_RPAREN) break;

        /* Type */
        buf_init(&type_buf, a);
        extract_type_text(src, tokens, cursor, &type_buf);

        /* Name */
        t = tok_at(tokens, *cursor);
        name_text = tok_text(src, t, &name_len);
        (*cursor)++;

        if (count >= cap) {
            cap *= 2;
            params = (vigil_doc_param_t *)doc_realloc(a, params, cap * sizeof(vigil_doc_param_t));
            if (params == NULL) { buf_free(&type_buf); return VIGIL_STATUS_OUT_OF_MEMORY; }
        }

        params[count].type_text = type_buf.data;
        params[count].type_length = type_buf.length;
        params[count].name = doc_strdup(a, name_text, name_len);
        params[count].name_length = name_len;
        count++;

        t = tok_at(tokens, *cursor);
        if (t != NULL && t->kind == VIGIL_TOKEN_COMMA) (*cursor)++;
    }

    /* Skip ) */
    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_RPAREN) (*cursor)++;

    *out_params = params;
    *out_count = count;
    return VIGIL_STATUS_OK;
}

/* ── Return type extraction ──────────────────────────────────────── */

static void extract_return_type(const vigil_allocator_t *a,
                                 const char *src,
                                 const vigil_token_list_t *tokens,
                                 size_t *cursor,
                                 char **out_text, size_t *out_len) {
    const vigil_token_t *t;
    doc_buf_t buf;

    *out_text = NULL;
    *out_len = 0;

    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_ARROW) return;
    (*cursor)++;

    buf_init(&buf, a);

    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_LPAREN) {
        /* Tuple return: (i32, err) */
        int first = 1;
        buf_append_char(&buf, '(');
        (*cursor)++;
        while (1) {
            t = tok_at(tokens, *cursor);
            if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_RPAREN) break;
            if (!first) buf_append_cstr(&buf, ", ");
            first = 0;
            extract_type_text(src, tokens, cursor, &buf);
            t = tok_at(tokens, *cursor);
            if (t != NULL && t->kind == VIGIL_TOKEN_COMMA) (*cursor)++;
        }
        buf_append_char(&buf, ')');
        t = tok_at(tokens, *cursor);
        if (t != NULL && t->kind == VIGIL_TOKEN_RPAREN) (*cursor)++;
    } else {
        extract_type_text(src, tokens, cursor, &buf);
    }

    *out_text = buf.data;
    *out_len = buf.length;
}

/* ── Skip brace-delimited body ───────────────────────────────────── */

static void skip_brace_body(const vigil_token_list_t *tokens, size_t *cursor) {
    const vigil_token_t *t;
    int depth = 0;

    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LBRACE) return;
    depth = 1;
    (*cursor)++;

    while (depth > 0) {
        t = tok_at(tokens, *cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF) break;
        if (t->kind == VIGIL_TOKEN_LBRACE) depth++;
        else if (t->kind == VIGIL_TOKEN_RBRACE) depth--;
        (*cursor)++;
    }
}

/* ── Module name from filename ───────────────────────────────────── */

static char *module_name_from_path(const vigil_allocator_t *a,
                                    const char *path, size_t path_len,
                                    size_t *out_len) {
    const char *base, *dot;
    size_t base_len;

    /* Find last / or \ */
    base = path + path_len;
    while (base > path && *(base - 1) != '/' && *(base - 1) != '\\') base--;
    base_len = (size_t)((path + path_len) - base);

    /* Strip .vigil extension. */
    dot = base + base_len;
    while (dot > base && *(dot - 1) != '.') dot--;
    if (dot > base) base_len = (size_t)(dot - 1 - base);

    *out_len = base_len;
    return doc_strdup(a, base, base_len);
}

/* ── Add symbol to module ────────────────────────────────────────── */

static vigil_doc_symbol_t *module_add_symbol(vigil_doc_module_t *m) {
    vigil_doc_symbol_t *s;
    if (m->symbol_count >= m->symbol_capacity) {
        size_t new_cap = m->symbol_capacity == 0 ? 8 : m->symbol_capacity * 2;
        vigil_doc_symbol_t *p = (vigil_doc_symbol_t *)doc_realloc(
            m->allocator, m->symbols, new_cap * sizeof(vigil_doc_symbol_t));
        if (p == NULL) return NULL;
        m->symbols = p;
        m->symbol_capacity = new_cap;
    }
    s = &m->symbols[m->symbol_count++];
    memset(s, 0, sizeof(*s));
    return s;
}


/* ── Parse function declaration ──────────────────────────────────── */

static vigil_status_t parse_function(const vigil_allocator_t *a,
                                     const char *src, size_t src_len,
                                     const vigil_token_list_t *tokens,
                                     size_t *cursor,
                                     vigil_doc_symbol_t *sym) {
    const vigil_token_t *t;
    size_t name_len;
    const char *name_text;
    vigil_status_t status;

    (void)src_len;
    /* cursor is on 'fn' */
    (*cursor)++;

    /* Name */
    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    sym->kind = VIGIL_DOC_FUNCTION;
    sym->name = doc_strdup(a, name_text, name_len);
    sym->name_length = name_len;
    (*cursor)++;

    /* Parameters */
    status = extract_params(a, src, tokens, cursor, &sym->params, &sym->param_count);
    if (status != VIGIL_STATUS_OK) return status;

    /* Return type */
    extract_return_type(a, src, tokens, cursor, &sym->return_text, &sym->return_length);

    /* Skip body */
    skip_brace_body(tokens, cursor);

    return VIGIL_STATUS_OK;
}

/* ── Parse class declaration ─────────────────────────────────────── */

static vigil_status_t parse_class(const vigil_allocator_t *a,
                                  const char *src, size_t src_len,
                                  const vigil_token_list_t *tokens,
                                  size_t *cursor,
                                  vigil_doc_symbol_t *sym) {
    const vigil_token_t *t;
    size_t name_len;
    const char *name_text;
    size_t field_cap = 4, method_cap = 4;

    (void)src_len;
    /* cursor is on 'class' */
    (*cursor)++;

    /* Name */
    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    sym->kind = VIGIL_DOC_CLASS;
    sym->name = doc_strdup(a, name_text, name_len);
    sym->name_length = name_len;
    (*cursor)++;

    /* implements clause */
    t = tok_at(tokens, *cursor);
    if (tok_is_ident_text(src, t, "implements", 10)) {
        doc_buf_t impl_buf;
        int first = 1;
        buf_init(&impl_buf, a);
        (*cursor)++;
        while (1) {
            t = tok_at(tokens, *cursor);
            if (t == NULL || t->kind == VIGIL_TOKEN_LBRACE || t->kind == VIGIL_TOKEN_EOF) break;
            if (!first) buf_append_cstr(&impl_buf, ", ");
            first = 0;
            name_text = tok_text(src, t, &name_len);
            buf_append(&impl_buf, name_text, name_len);
            (*cursor)++;
            t = tok_at(tokens, *cursor);
            if (t != NULL && t->kind == VIGIL_TOKEN_COMMA) (*cursor)++;
        }
        sym->implements_text = impl_buf.data;
        sym->implements_length = impl_buf.length;
    }

    /* Allocate fields and methods arrays. */
    sym->fields = (vigil_doc_symbol_t *)doc_alloc(a, field_cap * sizeof(vigil_doc_symbol_t));
    sym->methods = (vigil_doc_symbol_t *)doc_alloc(a, method_cap * sizeof(vigil_doc_symbol_t));
    sym->field_count = 0;
    sym->method_count = 0;

    /* Open brace */
    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LBRACE) return VIGIL_STATUS_OK;
    (*cursor)++;

    /* Parse class body. */
    while (1) {
        int is_pub = 0;
        vigil_doc_symbol_t member;

        t = tok_at(tokens, *cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_RBRACE) break;

        if (t->kind == VIGIL_TOKEN_PUB) {
            is_pub = 1;
            (*cursor)++;
            t = tok_at(tokens, *cursor);
        }

        if (t != NULL && t->kind == VIGIL_TOKEN_FN) {
            /* Method */
            memset(&member, 0, sizeof(member));
            extract_comment_before(a, src, src_len, t->span.start_offset, &member.comment);
            parse_function(a, src, src_len, tokens, cursor, &member);
            if (is_pub) {
                if (sym->method_count >= method_cap) {
                    method_cap *= 2;
                    sym->methods = (vigil_doc_symbol_t *)doc_realloc(
                        a, sym->methods, method_cap * sizeof(vigil_doc_symbol_t));
                }
                sym->methods[sym->method_count++] = member;
            } else {
                /* Free private method. */
                doc_free_ptr(a, member.name);
                doc_free_ptr(a, member.comment.text);
                doc_free_ptr(a, member.return_text);
                if (member.params) {
                    size_t pi;
                    for (pi = 0; pi < member.param_count; pi++) {
                        doc_free_ptr(a, member.params[pi].type_text);
                        doc_free_ptr(a, member.params[pi].name);
                    }
                    doc_free_ptr(a, member.params);
                }
            }
        } else if (tok_is_type_start(t)) {
            /* Field: type name; */
            doc_buf_t type_buf;
            size_t fname_len;
            const char *fname_text;
            size_t field_start = t->span.start_offset;

            memset(&member, 0, sizeof(member));
            buf_init(&type_buf, a);
            extract_type_text(src, tokens, cursor, &type_buf);

            t = tok_at(tokens, *cursor);
            fname_text = tok_text(src, t, &fname_len);
            (*cursor)++;

            /* Skip to semicolon. */
            while (1) {
                t = tok_at(tokens, *cursor);
                if (t == NULL || t->kind == VIGIL_TOKEN_EOF ||
                    t->kind == VIGIL_TOKEN_SEMICOLON) break;
                (*cursor)++;
            }
            if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON) (*cursor)++;

            if (is_pub) {
                extract_comment_before(a, src, src_len, field_start, &member.comment);
                member.kind = VIGIL_DOC_VARIABLE;
                member.name = doc_strdup(a, fname_text, fname_len);
                member.name_length = fname_len;
                member.type_text = type_buf.data;
                member.type_length = type_buf.length;
                if (sym->field_count >= field_cap) {
                    field_cap *= 2;
                    sym->fields = (vigil_doc_symbol_t *)doc_realloc(
                        a, sym->fields, field_cap * sizeof(vigil_doc_symbol_t));
                }
                sym->fields[sym->field_count++] = member;
            } else {
                buf_free(&type_buf);
            }
        } else {
            (*cursor)++;
        }
    }

    /* Close brace */
    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_RBRACE) (*cursor)++;

    return VIGIL_STATUS_OK;
}


/* ── Parse interface declaration ─────────────────────────────────── */

static vigil_status_t parse_interface(const vigil_allocator_t *a,
                                      const char *src, size_t src_len,
                                      const vigil_token_list_t *tokens,
                                      size_t *cursor,
                                      vigil_doc_symbol_t *sym) {
    const vigil_token_t *t;
    size_t name_len;
    const char *name_text;
    size_t method_cap = 4;

    (void)src_len;
    /* cursor is on 'interface' */
    (*cursor)++;

    /* Name */
    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    sym->kind = VIGIL_DOC_INTERFACE;
    sym->name = doc_strdup(a, name_text, name_len);
    sym->name_length = name_len;
    (*cursor)++;

    sym->iface_methods = (vigil_doc_symbol_t *)doc_alloc(a, method_cap * sizeof(vigil_doc_symbol_t));
    sym->iface_method_count = 0;

    /* Open brace */
    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LBRACE) return VIGIL_STATUS_OK;
    (*cursor)++;

    while (1) {
        vigil_doc_symbol_t method;

        t = tok_at(tokens, *cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_RBRACE) break;

        if (t->kind == VIGIL_TOKEN_FN) {
            memset(&method, 0, sizeof(method));
            extract_comment_before(a, src, src_len, t->span.start_offset, &method.comment);
            /* Interface methods have no body — just signature + ; */
            (*cursor)++; /* skip fn */

            t = tok_at(tokens, *cursor);
            name_text = tok_text(src, t, &name_len);
            method.kind = VIGIL_DOC_FUNCTION;
            method.name = doc_strdup(a, name_text, name_len);
            method.name_length = name_len;
            (*cursor)++;

            extract_params(a, src, tokens, cursor, &method.params, &method.param_count);
            extract_return_type(a, src, tokens, cursor, &method.return_text, &method.return_length);

            /* Skip semicolon. */
            t = tok_at(tokens, *cursor);
            if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON) (*cursor)++;

            if (sym->iface_method_count >= method_cap) {
                method_cap *= 2;
                sym->iface_methods = (vigil_doc_symbol_t *)doc_realloc(
                    a, sym->iface_methods, method_cap * sizeof(vigil_doc_symbol_t));
            }
            sym->iface_methods[sym->iface_method_count++] = method;
        } else {
            (*cursor)++;
        }
    }

    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_RBRACE) (*cursor)++;

    return VIGIL_STATUS_OK;
}

/* ── Parse enum declaration ──────────────────────────────────────── */

static vigil_status_t parse_enum(const vigil_allocator_t *a,
                                 const char *src, size_t src_len,
                                 const vigil_token_list_t *tokens,
                                 size_t *cursor,
                                 vigil_doc_symbol_t *sym) {
    const vigil_token_t *t;
    size_t name_len;
    const char *name_text;
    size_t var_cap = 8;

    (void)src_len;
    /* cursor is on 'enum' */
    (*cursor)++;

    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    sym->kind = VIGIL_DOC_ENUM;
    sym->name = doc_strdup(a, name_text, name_len);
    sym->name_length = name_len;
    (*cursor)++;

    sym->variant_names = (char **)doc_alloc(a, var_cap * sizeof(char *));
    sym->variant_name_lengths = (size_t *)doc_alloc(a, var_cap * sizeof(size_t));
    sym->variant_count = 0;

    t = tok_at(tokens, *cursor);
    if (t == NULL || t->kind != VIGIL_TOKEN_LBRACE) return VIGIL_STATUS_OK;
    (*cursor)++;

    while (1) {
        t = tok_at(tokens, *cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_RBRACE) break;

        if (t->kind == VIGIL_TOKEN_IDENTIFIER) {
            name_text = tok_text(src, t, &name_len);
            if (sym->variant_count >= var_cap) {
                var_cap *= 2;
                sym->variant_names = (char **)doc_realloc(a, sym->variant_names, var_cap * sizeof(char *));
                sym->variant_name_lengths = (size_t *)doc_realloc(a, sym->variant_name_lengths, var_cap * sizeof(size_t));
            }
            sym->variant_names[sym->variant_count] = doc_strdup(a, name_text, name_len);
            sym->variant_name_lengths[sym->variant_count] = name_len;
            sym->variant_count++;
        }
        (*cursor)++;
    }

    t = tok_at(tokens, *cursor);
    if (t != NULL && t->kind == VIGIL_TOKEN_RBRACE) (*cursor)++;

    return VIGIL_STATUS_OK;
}

/* ── Parse constant declaration ──────────────────────────────────── */

static void parse_const_or_var(const vigil_allocator_t *a,
                                const char *src,
                                const vigil_token_list_t *tokens,
                                size_t *cursor,
                                vigil_doc_symbol_t *sym,
                                int is_const) {
    const vigil_token_t *t;
    doc_buf_t type_buf;
    size_t name_len;
    const char *name_text;

    if (is_const) (*cursor)++; /* skip 'const' */

    /* Type */
    buf_init(&type_buf, a);
    extract_type_text(src, tokens, cursor, &type_buf);

    /* Name */
    t = tok_at(tokens, *cursor);
    name_text = tok_text(src, t, &name_len);
    (*cursor)++;

    sym->kind = is_const ? VIGIL_DOC_CONSTANT : VIGIL_DOC_VARIABLE;
    sym->name = doc_strdup(a, name_text, name_len);
    sym->name_length = name_len;
    sym->type_text = type_buf.data;
    sym->type_length = type_buf.length;

    /* Skip to semicolon. */
    while (1) {
        t = tok_at(tokens, *cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_SEMICOLON) break;
        (*cursor)++;
    }
    if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON) (*cursor)++;
}


/* ── vigil_doc_extract ────────────────────────────────────────────── */

vigil_status_t vigil_doc_extract(
    const vigil_allocator_t *allocator,
    const char *filename,
    size_t filename_length,
    const char *source_text,
    size_t source_length,
    const vigil_token_list_t *tokens,
    vigil_doc_module_t *out_module,
    vigil_error_t *error
) {
    size_t cursor = 0;
    const vigil_token_t *t;
    size_t first_decl_offset = (size_t)-1;

    (void)error;
    if (out_module == NULL) return VIGIL_STATUS_INVALID_ARGUMENT;
    memset(out_module, 0, sizeof(*out_module));
    out_module->allocator = allocator;

    out_module->name = module_name_from_path(allocator, filename, filename_length,
                                              &out_module->name_length);

    /* First pass: find first declaration offset for module summary. */
    {
        size_t scan = 0;
        int depth = 0;
        while (1) {
            t = tok_at(tokens, scan);
            if (t == NULL || t->kind == VIGIL_TOKEN_EOF) break;
            if (t->kind == VIGIL_TOKEN_LBRACE) { depth++; scan++; continue; }
            if (t->kind == VIGIL_TOKEN_RBRACE) { if (depth > 0) depth--; scan++; continue; }
            if (depth == 0) {
                if (t->kind == VIGIL_TOKEN_PUB || t->kind == VIGIL_TOKEN_FN ||
                    t->kind == VIGIL_TOKEN_CLASS || t->kind == VIGIL_TOKEN_INTERFACE ||
                    t->kind == VIGIL_TOKEN_ENUM || t->kind == VIGIL_TOKEN_CONST ||
                    t->kind == VIGIL_TOKEN_IMPORT || tok_is_type_start(t)) {
                    if (t->span.start_offset < first_decl_offset)
                        first_decl_offset = t->span.start_offset;
                    break;
                }
            }
            scan++;
        }
    }

    if (first_decl_offset != (size_t)-1) {
        extract_module_summary(allocator, source_text, source_length,
                                first_decl_offset, &out_module->summary);
    }

    /* Second pass: extract public declarations at brace depth 0.
     * For script files (those containing a main function), also extract
     * non-pub declarations so that `vigil doc` is useful on scripts. */
    int is_script = 0;
    {
        size_t scan = 0;
        while (1) {
            const vigil_token_t *st = tok_at(tokens, scan);
            if (st == NULL || st->kind == VIGIL_TOKEN_EOF) break;
            if (st->kind == VIGIL_TOKEN_FN) {
                const vigil_token_t *nt = tok_at(tokens, scan + 1);
                if (nt != NULL && nt->kind == VIGIL_TOKEN_IDENTIFIER &&
                    nt->span.end_offset - nt->span.start_offset == 4 &&
                    memcmp(source_text + nt->span.start_offset, "main", 4) == 0) {
                    is_script = 1;
                    break;
                }
            }
            scan++;
        }
    }
    cursor = 0;
    while (1) {
        int is_pub = 0;
        size_t decl_start;
        vigil_doc_symbol_t *sym;

        t = tok_at(tokens, cursor);
        if (t == NULL || t->kind == VIGIL_TOKEN_EOF) break;

        /* Skip imports at top level. */
        if (t->kind == VIGIL_TOKEN_IMPORT) {
            while (1) {
                t = tok_at(tokens, cursor);
                if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_SEMICOLON) break;
                cursor++;
            }
            if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON) cursor++;
            continue;
        }

        decl_start = t->span.start_offset;

        if (t->kind == VIGIL_TOKEN_PUB) {
            is_pub = 1;
            cursor++;
            t = tok_at(tokens, cursor);
        }

        if (t == NULL || t->kind == VIGIL_TOKEN_EOF) break;

        if (t->kind == VIGIL_TOKEN_FN) {
            if (is_pub || is_script) {
                sym = module_add_symbol(out_module);
                if (sym == NULL) return VIGIL_STATUS_OUT_OF_MEMORY;
                extract_comment_before(allocator, source_text, source_length,
                                        decl_start, &sym->comment);
                parse_function(allocator, source_text, source_length, tokens, &cursor, sym);
            } else {
                /* Skip private function. */
                cursor++; /* fn */
                cursor++; /* name */
                t = tok_at(tokens, cursor);
                if (t != NULL && t->kind == VIGIL_TOKEN_LPAREN) {
                    int depth = 1;
                    cursor++;
                    while (depth > 0) {
                        t = tok_at(tokens, cursor);
                        if (t == NULL || t->kind == VIGIL_TOKEN_EOF) break;
                        if (t->kind == VIGIL_TOKEN_LPAREN) depth++;
                        else if (t->kind == VIGIL_TOKEN_RPAREN) depth--;
                        cursor++;
                    }
                }
                t = tok_at(tokens, cursor);
                if (t != NULL && t->kind == VIGIL_TOKEN_ARROW) {
                    cursor++;
                    /* Skip return type tokens until { */
                    while (1) {
                        t = tok_at(tokens, cursor);
                        if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_LBRACE) break;
                        cursor++;
                    }
                }
                skip_brace_body(tokens, &cursor);
            }
        } else if (t->kind == VIGIL_TOKEN_CLASS) {
            if (is_pub || is_script) {
                sym = module_add_symbol(out_module);
                if (sym == NULL) return VIGIL_STATUS_OUT_OF_MEMORY;
                extract_comment_before(allocator, source_text, source_length,
                                        decl_start, &sym->comment);
                parse_class(allocator, source_text, source_length, tokens, &cursor, sym);
            } else {
                /* Skip private class. */
                cursor++; /* class */
                cursor++; /* name */
                /* Skip to { */
                while (1) {
                    t = tok_at(tokens, cursor);
                    if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_LBRACE) break;
                    cursor++;
                }
                skip_brace_body(tokens, &cursor);
            }
        } else if (t->kind == VIGIL_TOKEN_INTERFACE) {
            if (is_pub || is_script) {
                sym = module_add_symbol(out_module);
                if (sym == NULL) return VIGIL_STATUS_OUT_OF_MEMORY;
                extract_comment_before(allocator, source_text, source_length,
                                        decl_start, &sym->comment);
                parse_interface(allocator, source_text, source_length, tokens, &cursor, sym);
            } else {
                cursor++;
                cursor++;
                while (1) {
                    t = tok_at(tokens, cursor);
                    if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_LBRACE) break;
                    cursor++;
                }
                skip_brace_body(tokens, &cursor);
            }
        } else if (t->kind == VIGIL_TOKEN_ENUM) {
            if (is_pub || is_script) {
                sym = module_add_symbol(out_module);
                if (sym == NULL) return VIGIL_STATUS_OUT_OF_MEMORY;
                extract_comment_before(allocator, source_text, source_length,
                                        decl_start, &sym->comment);
                parse_enum(allocator, source_text, source_length, tokens, &cursor, sym);
            } else {
                cursor++;
                cursor++;
                skip_brace_body(tokens, &cursor);
            }
        } else if (t->kind == VIGIL_TOKEN_CONST) {
            if (is_pub || is_script) {
                sym = module_add_symbol(out_module);
                if (sym == NULL) return VIGIL_STATUS_OUT_OF_MEMORY;
                extract_comment_before(allocator, source_text, source_length,
                                        decl_start, &sym->comment);
                parse_const_or_var(allocator, source_text, tokens, &cursor, sym, 1);
            } else {
                /* Skip to semicolon. */
                while (1) {
                    t = tok_at(tokens, cursor);
                    if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_SEMICOLON) break;
                    cursor++;
                }
                if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON) cursor++;
            }
        } else if (tok_is_type_start(t)) {
            /* Variable declaration: [pub] type name = ...; */
            if (is_pub || is_script) {
                sym = module_add_symbol(out_module);
                if (sym == NULL) return VIGIL_STATUS_OUT_OF_MEMORY;
                extract_comment_before(allocator, source_text, source_length,
                                        decl_start, &sym->comment);
                parse_const_or_var(allocator, source_text, tokens, &cursor, sym, 0);
            } else {
                while (1) {
                    t = tok_at(tokens, cursor);
                    if (t == NULL || t->kind == VIGIL_TOKEN_EOF || t->kind == VIGIL_TOKEN_SEMICOLON) break;
                    cursor++;
                }
                if (t != NULL && t->kind == VIGIL_TOKEN_SEMICOLON) cursor++;
            }
        } else {
            cursor++;
        }
    }

    return VIGIL_STATUS_OK;
}


/* ── Render helpers ──────────────────────────────────────────────── */

static void render_func_sig(doc_buf_t *b, const vigil_doc_symbol_t *sym) {
    size_t i;
    buf_append(b, sym->name, sym->name_length);
    buf_append_char(b, '(');
    for (i = 0; i < sym->param_count; i++) {
        if (i > 0) buf_append_cstr(b, ", ");
        buf_append(b, sym->params[i].type_text, sym->params[i].type_length);
        buf_append_char(b, ' ');
        buf_append(b, sym->params[i].name, sym->params[i].name_length);
    }
    buf_append_char(b, ')');
    if (sym->return_text != NULL && sym->return_length > 0) {
        buf_append_cstr(b, " -> ");
        buf_append(b, sym->return_text, sym->return_length);
    }
}

static void render_indent_comment(doc_buf_t *b, const vigil_doc_comment_t *c, int indent) {
    size_t pos = 0;
    if (c->text == NULL || c->length == 0) return;
    while (pos < c->length) {
        size_t line_start = pos;
        while (pos < c->length && c->text[pos] != '\n') pos++;
        buf_append_indent(b, indent);
        buf_append(b, c->text + line_start, pos - line_start);
        buf_append_char(b, '\n');
        if (pos < c->length) pos++; /* skip \n */
    }
}

/* ── Render module view ──────────────────────────────────────────── */

static void render_module_view(doc_buf_t *b, const vigil_doc_module_t *m) {
    size_t i, j;
    int has_section;

    buf_append_cstr(b, "MODULE\n  ");
    buf_append(b, m->name, m->name_length);
    buf_append_char(b, '\n');

    if (m->summary.text != NULL && m->summary.length > 0) {
        buf_append_cstr(b, "\nSUMMARY\n");
        render_indent_comment(b, &m->summary, 2);
    }

    /* Constants */
    has_section = 0;
    for (i = 0; i < m->symbol_count; i++) {
        if (m->symbols[i].kind != VIGIL_DOC_CONSTANT) continue;
        if (!has_section) { buf_append_cstr(b, "\nCONSTANTS\n"); has_section = 1; }
        buf_append_cstr(b, "  ");
        buf_append(b, m->symbols[i].name, m->symbols[i].name_length);
        buf_append_char(b, ' ');
        buf_append(b, m->symbols[i].type_text, m->symbols[i].type_length);
        buf_append_char(b, '\n');
        if (m->symbols[i].comment.text != NULL)
            render_indent_comment(b, &m->symbols[i].comment, 4);
    }

    /* Variables */
    has_section = 0;
    for (i = 0; i < m->symbol_count; i++) {
        if (m->symbols[i].kind != VIGIL_DOC_VARIABLE) continue;
        if (!has_section) { buf_append_cstr(b, "\nVARIABLES\n"); has_section = 1; }
        buf_append_cstr(b, "  ");
        buf_append(b, m->symbols[i].name, m->symbols[i].name_length);
        buf_append_char(b, ' ');
        buf_append(b, m->symbols[i].type_text, m->symbols[i].type_length);
        buf_append_char(b, '\n');
        if (m->symbols[i].comment.text != NULL)
            render_indent_comment(b, &m->symbols[i].comment, 4);
    }

    /* Enums */
    has_section = 0;
    for (i = 0; i < m->symbol_count; i++) {
        const vigil_doc_symbol_t *sym = &m->symbols[i];
        if (sym->kind != VIGIL_DOC_ENUM) continue;
        if (!has_section) { buf_append_cstr(b, "\nENUMS\n"); has_section = 1; }
        buf_append_cstr(b, "  ");
        buf_append(b, sym->name, sym->name_length);
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL) {
            buf_append_char(b, '\n');
            render_indent_comment(b, &sym->comment, 4);
        }
        if (sym->variant_count > 0) {
            buf_append_cstr(b, "\n    Variants\n");
            for (j = 0; j < sym->variant_count; j++) {
                buf_append_cstr(b, "      ");
                buf_append(b, sym->variant_names[j], sym->variant_name_lengths[j]);
                buf_append_char(b, '\n');
            }
        }
    }

    /* Interfaces */
    has_section = 0;
    for (i = 0; i < m->symbol_count; i++) {
        const vigil_doc_symbol_t *sym = &m->symbols[i];
        if (sym->kind != VIGIL_DOC_INTERFACE) continue;
        if (!has_section) { buf_append_cstr(b, "\nINTERFACES\n"); has_section = 1; }
        buf_append_cstr(b, "  ");
        buf_append(b, sym->name, sym->name_length);
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL) {
            buf_append_char(b, '\n');
            render_indent_comment(b, &sym->comment, 4);
        }
        if (sym->iface_method_count > 0) {
            buf_append_cstr(b, "\n    Methods\n");
            for (j = 0; j < sym->iface_method_count; j++) {
                buf_append_cstr(b, "      ");
                render_func_sig(b, &sym->iface_methods[j]);
                buf_append_char(b, '\n');
                if (sym->iface_methods[j].comment.text != NULL)
                    render_indent_comment(b, &sym->iface_methods[j].comment, 8);
            }
        }
    }

    /* Classes */
    has_section = 0;
    for (i = 0; i < m->symbol_count; i++) {
        const vigil_doc_symbol_t *sym = &m->symbols[i];
        if (sym->kind != VIGIL_DOC_CLASS) continue;
        if (!has_section) { buf_append_cstr(b, "\nCLASSES\n"); has_section = 1; }
        buf_append_cstr(b, "  ");
        buf_append(b, sym->name, sym->name_length);
        if (sym->implements_text != NULL) {
            buf_append_cstr(b, " implements ");
            buf_append(b, sym->implements_text, sym->implements_length);
        }
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL) {
            buf_append_char(b, '\n');
            render_indent_comment(b, &sym->comment, 4);
        }
        if (sym->field_count > 0) {
            buf_append_cstr(b, "\n    Fields\n");
            for (j = 0; j < sym->field_count; j++) {
                buf_append_cstr(b, "      ");
                buf_append(b, sym->fields[j].name, sym->fields[j].name_length);
                buf_append_char(b, ' ');
                buf_append(b, sym->fields[j].type_text, sym->fields[j].type_length);
                buf_append_char(b, '\n');
                if (sym->fields[j].comment.text != NULL)
                    render_indent_comment(b, &sym->fields[j].comment, 8);
            }
        }
        if (sym->method_count > 0) {
            buf_append_cstr(b, "\n    Methods\n");
            for (j = 0; j < sym->method_count; j++) {
                buf_append_cstr(b, "      ");
                render_func_sig(b, &sym->methods[j]);
                buf_append_char(b, '\n');
                if (sym->methods[j].comment.text != NULL)
                    render_indent_comment(b, &sym->methods[j].comment, 8);
            }
        }
    }

    /* Functions */
    has_section = 0;
    for (i = 0; i < m->symbol_count; i++) {
        const vigil_doc_symbol_t *sym = &m->symbols[i];
        if (sym->kind != VIGIL_DOC_FUNCTION) continue;
        if (!has_section) { buf_append_cstr(b, "\nFUNCTIONS\n"); has_section = 1; }
        buf_append_cstr(b, "  ");
        render_func_sig(b, sym);
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL)
            render_indent_comment(b, &sym->comment, 4);
    }
}


/* ── Render symbol view ──────────────────────────────────────────── */

static void render_symbol_detail(doc_buf_t *b, const vigil_doc_symbol_t *sym) {
    size_t j;

    switch (sym->kind) {
    case VIGIL_DOC_FUNCTION:
        render_func_sig(b, sym);
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL) {
            buf_append_char(b, '\n');
            render_indent_comment(b, &sym->comment, 0);
        }
        break;

    case VIGIL_DOC_CONSTANT:
    case VIGIL_DOC_VARIABLE:
        buf_append(b, sym->name, sym->name_length);
        buf_append_char(b, ' ');
        buf_append(b, sym->type_text, sym->type_length);
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL) {
            buf_append_char(b, '\n');
            render_indent_comment(b, &sym->comment, 0);
        }
        break;

    case VIGIL_DOC_ENUM:
        buf_append(b, sym->name, sym->name_length);
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL) {
            buf_append_char(b, '\n');
            render_indent_comment(b, &sym->comment, 2);
        }
        if (sym->variant_count > 0) {
            buf_append_cstr(b, "\n  Variants\n");
            for (j = 0; j < sym->variant_count; j++) {
                buf_append_cstr(b, "    ");
                buf_append(b, sym->variant_names[j], sym->variant_name_lengths[j]);
                buf_append_char(b, '\n');
            }
        }
        break;

    case VIGIL_DOC_INTERFACE:
        buf_append(b, sym->name, sym->name_length);
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL) {
            buf_append_char(b, '\n');
            render_indent_comment(b, &sym->comment, 2);
        }
        if (sym->iface_method_count > 0) {
            buf_append_cstr(b, "\n  Methods\n");
            for (j = 0; j < sym->iface_method_count; j++) {
                buf_append_cstr(b, "    ");
                render_func_sig(b, &sym->iface_methods[j]);
                buf_append_char(b, '\n');
                if (sym->iface_methods[j].comment.text != NULL)
                    render_indent_comment(b, &sym->iface_methods[j].comment, 6);
            }
        }
        break;

    case VIGIL_DOC_CLASS:
        buf_append(b, sym->name, sym->name_length);
        if (sym->implements_text != NULL) {
            buf_append_cstr(b, " implements ");
            buf_append(b, sym->implements_text, sym->implements_length);
        }
        buf_append_char(b, '\n');
        if (sym->comment.text != NULL) {
            buf_append_char(b, '\n');
            render_indent_comment(b, &sym->comment, 2);
        }
        if (sym->field_count > 0) {
            buf_append_cstr(b, "\n  Fields\n");
            for (j = 0; j < sym->field_count; j++) {
                buf_append_cstr(b, "    ");
                buf_append(b, sym->fields[j].name, sym->fields[j].name_length);
                buf_append_char(b, ' ');
                buf_append(b, sym->fields[j].type_text, sym->fields[j].type_length);
                buf_append_char(b, '\n');
                if (sym->fields[j].comment.text != NULL)
                    render_indent_comment(b, &sym->fields[j].comment, 6);
            }
        }
        if (sym->method_count > 0) {
            buf_append_cstr(b, "\n  Methods\n");
            for (j = 0; j < sym->method_count; j++) {
                buf_append_cstr(b, "    ");
                render_func_sig(b, &sym->methods[j]);
                buf_append_char(b, '\n');
                if (sym->methods[j].comment.text != NULL)
                    render_indent_comment(b, &sym->methods[j].comment, 6);
            }
        }
        break;
    }
}

static int find_symbol(const vigil_doc_module_t *m, const char *name,
                        const vigil_doc_symbol_t **out) {
    size_t i, name_len = strlen(name);
    for (i = 0; i < m->symbol_count; i++) {
        if (m->symbols[i].name_length == name_len &&
            memcmp(m->symbols[i].name, name, name_len) == 0) {
            *out = &m->symbols[i];
            return 1;
        }
    }
    return 0;
}

static int find_class_member(const vigil_doc_symbol_t *cls, const char *member,
                              size_t member_len, const vigil_doc_symbol_t **out,
                              int *is_method) {
    size_t i;
    for (i = 0; i < cls->field_count; i++) {
        if (cls->fields[i].name_length == member_len &&
            memcmp(cls->fields[i].name, member, member_len) == 0) {
            *out = &cls->fields[i];
            *is_method = 0;
            return 1;
        }
    }
    for (i = 0; i < cls->method_count; i++) {
        if (cls->methods[i].name_length == member_len &&
            memcmp(cls->methods[i].name, member, member_len) == 0) {
            *out = &cls->methods[i];
            *is_method = 1;
            return 1;
        }
    }
    return 0;
}

static int find_iface_method(const vigil_doc_symbol_t *iface, const char *method,
                              size_t method_len, const vigil_doc_symbol_t **out) {
    size_t i;
    for (i = 0; i < iface->iface_method_count; i++) {
        if (iface->iface_methods[i].name_length == method_len &&
            memcmp(iface->iface_methods[i].name, method, method_len) == 0) {
            *out = &iface->iface_methods[i];
            return 1;
        }
    }
    return 0;
}

/* ── vigil_doc_render ─────────────────────────────────────────────── */

vigil_status_t vigil_doc_render(
    const vigil_doc_module_t *module,
    const char *symbol,
    char **out_text,
    size_t *out_length,
    vigil_error_t *error
) {
    doc_buf_t buf;

    if (module == NULL || out_text == NULL) return VIGIL_STATUS_INVALID_ARGUMENT;
    *out_text = NULL;
    if (out_length != NULL) *out_length = 0;

    buf_init(&buf, module->allocator);

    if (symbol == NULL || symbol[0] == '\0') {
        render_module_view(&buf, module);
    } else {
        /* Check for dot notation: Class.member */
        const char *dot = strchr(symbol, '.');
        if (dot != NULL) {
            const char *parent_name = symbol;
            size_t parent_len = (size_t)(dot - symbol);
            const char *member_name = dot + 1;
            size_t member_len = strlen(member_name);
            const vigil_doc_symbol_t *parent_sym;
            const vigil_doc_symbol_t *member_sym;
            int is_method = 0;
            int found = 0;

            /* Find parent. */
            size_t i;
            parent_sym = NULL;
            for (i = 0; i < module->symbol_count; i++) {
                if (module->symbols[i].name_length == parent_len &&
                    memcmp(module->symbols[i].name, parent_name, parent_len) == 0) {
                    parent_sym = &module->symbols[i];
                    break;
                }
            }

            if (parent_sym != NULL && parent_sym->kind == VIGIL_DOC_CLASS) {
                if (find_class_member(parent_sym, member_name, member_len, &member_sym, &is_method)) {
                    buf_append(&buf, parent_name, parent_len);
                    buf_append_char(&buf, '.');
                    if (is_method) {
                        render_func_sig(&buf, member_sym);
                    } else {
                        buf_append(&buf, member_sym->name, member_sym->name_length);
                        buf_append_char(&buf, ' ');
                        buf_append(&buf, member_sym->type_text, member_sym->type_length);
                    }
                    buf_append_char(&buf, '\n');
                    if (member_sym->comment.text != NULL) {
                        buf_append_char(&buf, '\n');
                        render_indent_comment(&buf, &member_sym->comment, 0);
                    }
                    found = 1;
                }
            }
            if (!found && parent_sym != NULL && parent_sym->kind == VIGIL_DOC_INTERFACE) {
                if (find_iface_method(parent_sym, member_name, member_len, &member_sym)) {
                    buf_append(&buf, parent_name, parent_len);
                    buf_append_char(&buf, '.');
                    render_func_sig(&buf, member_sym);
                    buf_append_char(&buf, '\n');
                    if (member_sym->comment.text != NULL) {
                        buf_append_char(&buf, '\n');
                        render_indent_comment(&buf, &member_sym->comment, 0);
                    }
                    found = 1;
                }
            }
            if (!found) {
                buf_free(&buf);
                if (error != NULL) {
                    vigil_error_clear(error);
                    error->type = VIGIL_STATUS_INVALID_ARGUMENT;
                    /* Use a static message. */
                    error->value = "public symbol not found";
                    error->length = 23;
                }
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
        } else {
            const vigil_doc_symbol_t *sym;
            if (find_symbol(module, symbol, &sym)) {
                render_symbol_detail(&buf, sym);
            } else {
                buf_free(&buf);
                if (error != NULL) {
                    vigil_error_clear(error);
                    error->type = VIGIL_STATUS_INVALID_ARGUMENT;
                    error->value = "public symbol not found";
                    error->length = 23;
                }
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
        }
    }

    *out_text = buf.data;
    if (out_length != NULL) *out_length = buf.length;
    return VIGIL_STATUS_OK;
}


/* ── Free helpers ────────────────────────────────────────────────── */

static void free_symbol(const vigil_allocator_t *a, vigil_doc_symbol_t *sym) {
    size_t i;
    doc_free_ptr(a, sym->name);
    doc_free_ptr(a, sym->comment.text);
    doc_free_ptr(a, sym->return_text);
    doc_free_ptr(a, sym->type_text);
    doc_free_ptr(a, sym->implements_text);

    if (sym->params != NULL) {
        for (i = 0; i < sym->param_count; i++) {
            doc_free_ptr(a, sym->params[i].type_text);
            doc_free_ptr(a, sym->params[i].name);
        }
        doc_free_ptr(a, sym->params);
    }

    if (sym->fields != NULL) {
        for (i = 0; i < sym->field_count; i++)
            free_symbol(a, &sym->fields[i]);
        doc_free_ptr(a, sym->fields);
    }

    if (sym->methods != NULL) {
        for (i = 0; i < sym->method_count; i++)
            free_symbol(a, &sym->methods[i]);
        doc_free_ptr(a, sym->methods);
    }

    if (sym->iface_methods != NULL) {
        for (i = 0; i < sym->iface_method_count; i++)
            free_symbol(a, &sym->iface_methods[i]);
        doc_free_ptr(a, sym->iface_methods);
    }

    if (sym->variant_names != NULL) {
        for (i = 0; i < sym->variant_count; i++)
            doc_free_ptr(a, sym->variant_names[i]);
        doc_free_ptr(a, sym->variant_names);
    }
    doc_free_ptr(a, sym->variant_name_lengths);
}

void vigil_doc_module_free(vigil_doc_module_t *module) {
    size_t i;
    const vigil_allocator_t *a;
    if (module == NULL) return;
    a = module->allocator;
    doc_free_ptr(a, module->name);
    doc_free_ptr(a, module->summary.text);
    if (module->symbols != NULL) {
        for (i = 0; i < module->symbol_count; i++)
            free_symbol(a, &module->symbols[i]);
        doc_free_ptr(a, module->symbols);
    }
    memset(module, 0, sizeof(*module));
}
