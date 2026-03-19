#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/toml.h"

/* ── Internal types ──────────────────────────────────────────────── */

typedef struct toml_member
{
    char *key;
    size_t key_length;
    vigil_toml_value_t *value;
} toml_member_t;

struct vigil_toml_value
{
    vigil_toml_type_t type;
    vigil_allocator_t allocator;
    union {
        struct
        {
            char *data;
            size_t length;
        } string;
        int64_t integer;
        double floating;
        int boolean;
        vigil_toml_datetime_t datetime;
        struct
        {
            vigil_toml_value_t **items;
            size_t count;
            size_t capacity;
        } array;
        struct
        {
            toml_member_t *members;
            size_t count;
            size_t capacity;
        } table;
    } as;
};

/* ── Allocator helpers ───────────────────────────────────────────── */

static void *toml_alloc(const vigil_allocator_t *a, size_t size)
{
    return a->allocate(a->user_data, size);
}

static void *toml_realloc(const vigil_allocator_t *a, void *p, size_t size)
{
    return a->reallocate(a->user_data, p, size);
}

static void toml_dealloc(const vigil_allocator_t *a, void *p)
{
    a->deallocate(a->user_data, p);
}

static vigil_allocator_t resolve_alloc(const vigil_allocator_t *allocator)
{
    if (allocator != NULL && vigil_allocator_is_valid(allocator))
    {
        return *allocator;
    }
    return vigil_default_allocator();
}

static vigil_status_t alloc_value(const vigil_allocator_t *allocator, vigil_toml_type_t type, vigil_toml_value_t **out,
                                  vigil_error_t *error)
{
    vigil_allocator_t a = resolve_alloc(allocator);
    vigil_toml_value_t *v = (vigil_toml_value_t *)toml_alloc(&a, sizeof(*v));
    if (v == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "toml: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memset(v, 0, sizeof(*v));
    v->type = type;
    v->allocator = a;
    *out = v;
    return VIGIL_STATUS_OK;
}

/* ── Constructors ────────────────────────────────────────────────── */

vigil_status_t vigil_toml_string_new(const vigil_allocator_t *allocator, const char *value, size_t length,
                                     vigil_toml_value_t **out, vigil_error_t *error)
{
    vigil_toml_value_t *v;
    vigil_status_t s = alloc_value(allocator, VIGIL_TOML_STRING, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    v->as.string.data = (char *)toml_alloc(&v->allocator, length + 1);
    if (v->as.string.data == NULL)
    {
        toml_dealloc(&v->allocator, v);
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "toml: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memcpy(v->as.string.data, value, length);
    v->as.string.data[length] = '\0';
    v->as.string.length = length;
    *out = v;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_toml_integer_new(const vigil_allocator_t *allocator, int64_t value, vigil_toml_value_t **out,
                                      vigil_error_t *error)
{
    vigil_toml_value_t *v;
    vigil_status_t s = alloc_value(allocator, VIGIL_TOML_INTEGER, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    v->as.integer = value;
    *out = v;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_toml_float_new(const vigil_allocator_t *allocator, double value, vigil_toml_value_t **out,
                                    vigil_error_t *error)
{
    vigil_toml_value_t *v;
    vigil_status_t s = alloc_value(allocator, VIGIL_TOML_FLOAT, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    v->as.floating = value;
    *out = v;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_toml_bool_new(const vigil_allocator_t *allocator, int value, vigil_toml_value_t **out,
                                   vigil_error_t *error)
{
    vigil_toml_value_t *v;
    vigil_status_t s = alloc_value(allocator, VIGIL_TOML_BOOL, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    v->as.boolean = value != 0;
    *out = v;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_toml_datetime_new(const vigil_allocator_t *allocator, const vigil_toml_datetime_t *value,
                                       vigil_toml_value_t **out, vigil_error_t *error)
{
    vigil_toml_value_t *v;
    vigil_status_t s = alloc_value(allocator, VIGIL_TOML_DATETIME, &v, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    v->as.datetime = *value;
    *out = v;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_toml_array_new(const vigil_allocator_t *allocator, vigil_toml_value_t **out, vigil_error_t *error)
{
    return alloc_value(allocator, VIGIL_TOML_ARRAY, out, error);
}

vigil_status_t vigil_toml_table_new(const vigil_allocator_t *allocator, vigil_toml_value_t **out, vigil_error_t *error)
{
    return alloc_value(allocator, VIGIL_TOML_TABLE, out, error);
}

/* ── Free ────────────────────────────────────────────────────────── */

void vigil_toml_free(vigil_toml_value_t **value)
{
    vigil_toml_value_t *v;
    size_t i;
    if (value == NULL || *value == NULL)
        return;
    v = *value;
    *value = NULL;
    switch (v->type)
    {
    case VIGIL_TOML_STRING:
        toml_dealloc(&v->allocator, v->as.string.data);
        break;
    case VIGIL_TOML_ARRAY:
        for (i = 0; i < v->as.array.count; i++)
        {
            vigil_toml_free(&v->as.array.items[i]);
        }
        toml_dealloc(&v->allocator, v->as.array.items);
        break;
    case VIGIL_TOML_TABLE:
        for (i = 0; i < v->as.table.count; i++)
        {
            toml_dealloc(&v->allocator, v->as.table.members[i].key);
            vigil_toml_free(&v->as.table.members[i].value);
        }
        toml_dealloc(&v->allocator, v->as.table.members);
        break;
    default:
        break;
    }
    toml_dealloc(&v->allocator, v);
}

/* ── Type / accessors ────────────────────────────────────────────── */

vigil_toml_type_t vigil_toml_type(const vigil_toml_value_t *value)
{
    return value ? value->type : VIGIL_TOML_TABLE;
}

const char *vigil_toml_string_value(const vigil_toml_value_t *v)
{
    return (v && v->type == VIGIL_TOML_STRING) ? v->as.string.data : "";
}

size_t vigil_toml_string_length(const vigil_toml_value_t *v)
{
    return (v && v->type == VIGIL_TOML_STRING) ? v->as.string.length : 0;
}

int64_t vigil_toml_integer_value(const vigil_toml_value_t *v)
{
    return (v && v->type == VIGIL_TOML_INTEGER) ? v->as.integer : 0;
}

double vigil_toml_float_value(const vigil_toml_value_t *v)
{
    return (v && v->type == VIGIL_TOML_FLOAT) ? v->as.floating : 0.0;
}

int vigil_toml_bool_value(const vigil_toml_value_t *v)
{
    return (v && v->type == VIGIL_TOML_BOOL) ? v->as.boolean : 0;
}

const vigil_toml_datetime_t *vigil_toml_datetime_value(const vigil_toml_value_t *v)
{
    return (v && v->type == VIGIL_TOML_DATETIME) ? &v->as.datetime : NULL;
}

/* ── Array ops ───────────────────────────────────────────────────── */

size_t vigil_toml_array_count(const vigil_toml_value_t *a)
{
    return (a && a->type == VIGIL_TOML_ARRAY) ? a->as.array.count : 0;
}

const vigil_toml_value_t *vigil_toml_array_get(const vigil_toml_value_t *a, size_t i)
{
    if (!a || a->type != VIGIL_TOML_ARRAY || i >= a->as.array.count)
        return NULL;
    return a->as.array.items[i];
}

vigil_status_t vigil_toml_array_push(vigil_toml_value_t *a, vigil_toml_value_t *elem, vigil_error_t *error)
{
    if (!a || a->type != VIGIL_TOML_ARRAY)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "toml: not an array");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (a->as.array.count == a->as.array.capacity)
    {
        size_t cap = a->as.array.capacity ? a->as.array.capacity * 2 : 4;
        vigil_toml_value_t **items =
            (vigil_toml_value_t **)toml_realloc(&a->allocator, a->as.array.items, cap * sizeof(*items));
        if (!items)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "toml: allocation failed");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        a->as.array.items = items;
        a->as.array.capacity = cap;
    }
    a->as.array.items[a->as.array.count++] = elem;
    return VIGIL_STATUS_OK;
}

/* ── Table ops ───────────────────────────────────────────────────── */

size_t vigil_toml_table_count(const vigil_toml_value_t *t)
{
    return (t && t->type == VIGIL_TOML_TABLE) ? t->as.table.count : 0;
}

static toml_member_t *table_find(const vigil_toml_value_t *t, const char *key, size_t len)
{
    size_t i;
    if (!t || t->type != VIGIL_TOML_TABLE)
        return NULL;
    for (i = 0; i < t->as.table.count; i++)
    {
        if (t->as.table.members[i].key_length == len && memcmp(t->as.table.members[i].key, key, len) == 0)
        {
            return &t->as.table.members[i];
        }
    }
    return NULL;
}

const vigil_toml_value_t *vigil_toml_table_get(const vigil_toml_value_t *t, const char *key)
{
    toml_member_t *m;
    if (!key)
        return NULL;
    m = table_find(t, key, strlen(key));
    return m ? m->value : NULL;
}

vigil_status_t vigil_toml_table_set(vigil_toml_value_t *t, const char *key, size_t key_length,
                                    vigil_toml_value_t *value, vigil_error_t *error)
{
    toml_member_t *m;
    char *kcopy;
    if (!t || t->type != VIGIL_TOML_TABLE)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "toml: not a table");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    m = table_find(t, key, key_length);
    if (m)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "toml: duplicate key");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (t->as.table.count == t->as.table.capacity)
    {
        size_t cap = t->as.table.capacity ? t->as.table.capacity * 2 : 4;
        toml_member_t *mem = (toml_member_t *)toml_realloc(&t->allocator, t->as.table.members, cap * sizeof(*mem));
        if (!mem)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "toml: allocation failed");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        t->as.table.members = mem;
        t->as.table.capacity = cap;
    }
    kcopy = (char *)toml_alloc(&t->allocator, key_length + 1);
    if (!kcopy)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "toml: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memcpy(kcopy, key, key_length);
    kcopy[key_length] = '\0';
    m = &t->as.table.members[t->as.table.count++];
    m->key = kcopy;
    m->key_length = key_length;
    m->value = value;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_toml_table_remove(vigil_toml_value_t *t, const char *key, vigil_error_t *error)
{
    size_t i, key_len;
    if (!t || t->type != VIGIL_TOML_TABLE || !key)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "toml: invalid argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    key_len = strlen(key);
    for (i = 0; i < t->as.table.count; i++)
    {
        toml_member_t *m = &t->as.table.members[i];
        if (m->key_length == key_len && memcmp(m->key, key, key_len) == 0)
        {
            /* Free the key and value */
            toml_dealloc(&t->allocator, m->key);
            vigil_toml_free(&m->value);
            /* Shift remaining entries */
            if (i + 1 < t->as.table.count)
            {
                memmove(&t->as.table.members[i], &t->as.table.members[i + 1],
                        (t->as.table.count - i - 1) * sizeof(toml_member_t));
            }
            t->as.table.count--;
            return VIGIL_STATUS_OK;
        }
    }
    vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "toml: key not found");
    return VIGIL_STATUS_INVALID_ARGUMENT;
}

vigil_status_t vigil_toml_table_entry(const vigil_toml_value_t *t, size_t index, const char **out_key,
                                      size_t *out_key_length, const vigil_toml_value_t **out_value)
{
    if (!t || t->type != VIGIL_TOML_TABLE || index >= t->as.table.count)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (out_key)
        *out_key = t->as.table.members[index].key;
    if (out_key_length)
        *out_key_length = t->as.table.members[index].key_length;
    if (out_value)
        *out_value = t->as.table.members[index].value;
    return VIGIL_STATUS_OK;
}

const vigil_toml_value_t *vigil_toml_table_get_path(const vigil_toml_value_t *t, const char *dotted_key)
{
    const char *p = dotted_key;
    const char *dot;
    while (t && t->type == VIGIL_TOML_TABLE && *p)
    {
        dot = strchr(p, '.');
        if (dot)
        {
            toml_member_t *m = table_find(t, p, (size_t)(dot - p));
            t = m ? m->value : NULL;
            p = dot + 1;
        }
        else
        {
            return vigil_toml_table_get(t, p);
        }
    }
    return NULL;
}

/* ── Parser internals ────────────────────────────────────────────── */

typedef struct toml_parser
{
    const char *input;
    size_t length;
    size_t pos;
    size_t line;
    size_t col;
    const vigil_allocator_t *allocator;
    /* Scratch buffer for string building. */
    char *buf;
    size_t buf_len;
    size_t buf_cap;
} toml_parser_t;

static void parser_init(toml_parser_t *p, const vigil_allocator_t *alloc, const char *input, size_t length)
{
    memset(p, 0, sizeof(*p));
    p->input = input;
    p->length = length;
    p->line = 1;
    p->col = 1;
    p->allocator = alloc;
}

static void parser_free_buf(toml_parser_t *p)
{
    if (p->buf)
    {
        vigil_allocator_t a = resolve_alloc(p->allocator);
        toml_dealloc(&a, p->buf);
        p->buf = NULL;
        p->buf_len = 0;
        p->buf_cap = 0;
    }
}

static vigil_status_t parser_error(toml_parser_t *p, const char *msg, vigil_error_t *error)
{
    char detail[256];
    (void)p;
    snprintf(detail, sizeof(detail), "toml:%zu:%zu: %s", p->line, p->col, msg);
    vigil_error_set_literal(error, VIGIL_STATUS_SYNTAX_ERROR, detail);
    return VIGIL_STATUS_SYNTAX_ERROR;
}

static int parser_eof(const toml_parser_t *p)
{
    return p->pos >= p->length;
}

static char parser_peek(const toml_parser_t *p)
{
    return parser_eof(p) ? '\0' : p->input[p->pos];
}

static char parser_advance(toml_parser_t *p)
{
    char c;
    if (parser_eof(p))
        return '\0';
    c = p->input[p->pos++];
    if (c == '\n')
    {
        p->line++;
        p->col = 1;
    }
    else
    {
        p->col++;
    }
    return c;
}

static int parser_match(toml_parser_t *p, char c)
{
    if (parser_peek(p) == c)
    {
        parser_advance(p);
        return 1;
    }
    return 0;
}

static void skip_ws(toml_parser_t *p)
{
    while (!parser_eof(p))
    {
        char c = parser_peek(p);
        if (c == ' ' || c == '\t')
            parser_advance(p);
        else
            break;
    }
}

static void skip_ws_and_newlines(toml_parser_t *p)
{
    while (!parser_eof(p))
    {
        char c = parser_peek(p);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            parser_advance(p);
        else if (c == '#')
        {
            while (!parser_eof(p) && parser_peek(p) != '\n')
                parser_advance(p);
        }
        else
            break;
    }
}

static void skip_comment(toml_parser_t *p)
{
    if (parser_peek(p) == '#')
    {
        while (!parser_eof(p) && parser_peek(p) != '\n')
            parser_advance(p);
    }
}

static void skip_to_newline(toml_parser_t *p)
{
    skip_ws(p);
    skip_comment(p);
}

/* ── Scratch buffer ──────────────────────────────────────────────── */

static void buf_reset(toml_parser_t *p)
{
    p->buf_len = 0;
}

static vigil_status_t buf_push(toml_parser_t *p, char c, vigil_error_t *error)
{
    if (p->buf_len == p->buf_cap)
    {
        size_t cap = p->buf_cap ? p->buf_cap * 2 : 64;
        vigil_allocator_t a = resolve_alloc(p->allocator);
        char *nb = (char *)toml_realloc(&a, p->buf, cap);
        if (!nb)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "toml: allocation failed");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        p->buf = nb;
        p->buf_cap = cap;
    }
    p->buf[p->buf_len++] = c;
    return VIGIL_STATUS_OK;
}

static vigil_status_t buf_push_utf8(toml_parser_t *p, uint32_t cp, vigil_error_t *error)
{
    if (cp <= 0x7F)
    {
        return buf_push(p, (char)cp, error);
    }
    else if (cp <= 0x7FF)
    {
        vigil_status_t s = buf_push(p, (char)(0xC0 | (cp >> 6)), error);
        if (s != VIGIL_STATUS_OK)
            return s;
        return buf_push(p, (char)(0x80 | (cp & 0x3F)), error);
    }
    else if (cp <= 0xFFFF)
    {
        vigil_status_t s = buf_push(p, (char)(0xE0 | (cp >> 12)), error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = buf_push(p, (char)(0x80 | ((cp >> 6) & 0x3F)), error);
        if (s != VIGIL_STATUS_OK)
            return s;
        return buf_push(p, (char)(0x80 | (cp & 0x3F)), error);
    }
    else if (cp <= 0x10FFFF)
    {
        vigil_status_t s = buf_push(p, (char)(0xF0 | (cp >> 18)), error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = buf_push(p, (char)(0x80 | ((cp >> 12) & 0x3F)), error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = buf_push(p, (char)(0x80 | ((cp >> 6) & 0x3F)), error);
        if (s != VIGIL_STATUS_OK)
            return s;
        return buf_push(p, (char)(0x80 | (cp & 0x3F)), error);
    }
    return parser_error(p, "invalid unicode codepoint", error);
}

/* ── String parsing ──────────────────────────────────────────────── */

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return -1;
}

static vigil_status_t parse_unicode_escape(toml_parser_t *p, int n, vigil_error_t *error)
{
    uint32_t cp = 0;
    int i;
    for (i = 0; i < n; i++)
    {
        int d = hex_digit(parser_peek(p));
        if (d < 0)
            return parser_error(p, "invalid unicode escape", error);
        cp = (cp << 4) | (uint32_t)d;
        parser_advance(p);
    }
    if (cp >= 0xD800 && cp <= 0xDFFF)
        return parser_error(p, "surrogate codepoint in unicode escape", error);
    return buf_push_utf8(p, cp, error);
}

static vigil_status_t parse_basic_string(toml_parser_t *p, vigil_error_t *error)
{
    vigil_status_t s;
    buf_reset(p);
    /* Opening " already consumed. */
    while (!parser_eof(p))
    {
        char c = parser_peek(p);
        if (c == '"')
        {
            parser_advance(p);
            return VIGIL_STATUS_OK;
        }
        if (c == '\n' || c == '\r')
            return parser_error(p, "newline in basic string", error);
        if (c == '\\')
        {
            parser_advance(p);
            c = parser_advance(p);
            switch (c)
            {
            case 'b':
                s = buf_push(p, '\b', error);
                break;
            case 't':
                s = buf_push(p, '\t', error);
                break;
            case 'n':
                s = buf_push(p, '\n', error);
                break;
            case 'f':
                s = buf_push(p, '\f', error);
                break;
            case 'r':
                s = buf_push(p, '\r', error);
                break;
            case '"':
                s = buf_push(p, '"', error);
                break;
            case '\\':
                s = buf_push(p, '\\', error);
                break;
            case 'u':
                s = parse_unicode_escape(p, 4, error);
                break;
            case 'U':
                s = parse_unicode_escape(p, 8, error);
                break;
            default:
                return parser_error(p, "invalid escape sequence", error);
            }
            if (s != VIGIL_STATUS_OK)
                return s;
        }
        else
        {
            parser_advance(p);
            s = buf_push(p, c, error);
            if (s != VIGIL_STATUS_OK)
                return s;
        }
    }
    return parser_error(p, "unterminated basic string", error);
}

static vigil_status_t parse_ml_basic_string(toml_parser_t *p, vigil_error_t *error)
{
    vigil_status_t s;
    buf_reset(p);
    /* Opening """ already consumed. Skip first newline if present. */
    if (parser_peek(p) == '\r')
        parser_advance(p);
    if (parser_peek(p) == '\n')
        parser_advance(p);
    while (!parser_eof(p))
    {
        char c = parser_peek(p);
        if (c == '"')
        {
            parser_advance(p);
            if (parser_peek(p) == '"')
            {
                parser_advance(p);
                if (parser_peek(p) == '"')
                {
                    parser_advance(p);
                    /* Allow up to two extra quotes before closing. */
                    if (parser_peek(p) == '"')
                    {
                        s = buf_push(p, '"', error);
                        if (s != VIGIL_STATUS_OK)
                            return s;
                        parser_advance(p);
                    }
                    if (parser_peek(p) == '"')
                    {
                        s = buf_push(p, '"', error);
                        if (s != VIGIL_STATUS_OK)
                            return s;
                        parser_advance(p);
                    }
                    return VIGIL_STATUS_OK;
                }
                s = buf_push(p, '"', error);
                if (s != VIGIL_STATUS_OK)
                    return s;
            }
            s = buf_push(p, '"', error);
            if (s != VIGIL_STATUS_OK)
                return s;
            continue;
        }
        if (c == '\\')
        {
            parser_advance(p);
            c = parser_peek(p);
            /* Line-ending backslash: trim whitespace. */
            if (c == '\n' || c == '\r')
            {
                while (!parser_eof(p))
                {
                    c = parser_peek(p);
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                        parser_advance(p);
                    else
                        break;
                }
                continue;
            }
            c = parser_advance(p);
            switch (c)
            {
            case 'b':
                s = buf_push(p, '\b', error);
                break;
            case 't':
                s = buf_push(p, '\t', error);
                break;
            case 'n':
                s = buf_push(p, '\n', error);
                break;
            case 'f':
                s = buf_push(p, '\f', error);
                break;
            case 'r':
                s = buf_push(p, '\r', error);
                break;
            case '"':
                s = buf_push(p, '"', error);
                break;
            case '\\':
                s = buf_push(p, '\\', error);
                break;
            case 'u':
                s = parse_unicode_escape(p, 4, error);
                break;
            case 'U':
                s = parse_unicode_escape(p, 8, error);
                break;
            default:
                return parser_error(p, "invalid escape sequence", error);
            }
            if (s != VIGIL_STATUS_OK)
                return s;
        }
        else
        {
            parser_advance(p);
            s = buf_push(p, c, error);
            if (s != VIGIL_STATUS_OK)
                return s;
        }
    }
    return parser_error(p, "unterminated multiline basic string", error);
}

static vigil_status_t parse_literal_string(toml_parser_t *p, vigil_error_t *error)
{
    buf_reset(p);
    /* Opening ' already consumed. */
    while (!parser_eof(p))
    {
        char c = parser_peek(p);
        if (c == '\'')
        {
            parser_advance(p);
            return VIGIL_STATUS_OK;
        }
        if (c == '\n' || c == '\r')
            return parser_error(p, "newline in literal string", error);
        parser_advance(p);
        if (buf_push(p, c, error) != VIGIL_STATUS_OK)
            return error->type;
    }
    return parser_error(p, "unterminated literal string", error);
}

static vigil_status_t parse_ml_literal_string(toml_parser_t *p, vigil_error_t *error)
{
    vigil_status_t s;
    buf_reset(p);
    /* Opening ''' already consumed. Skip first newline. */
    if (parser_peek(p) == '\r')
        parser_advance(p);
    if (parser_peek(p) == '\n')
        parser_advance(p);
    while (!parser_eof(p))
    {
        char c = parser_peek(p);
        if (c == '\'')
        {
            parser_advance(p);
            if (parser_peek(p) == '\'')
            {
                parser_advance(p);
                if (parser_peek(p) == '\'')
                {
                    parser_advance(p);
                    if (parser_peek(p) == '\'')
                    {
                        s = buf_push(p, '\'', error);
                        if (s != VIGIL_STATUS_OK)
                            return s;
                        parser_advance(p);
                    }
                    if (parser_peek(p) == '\'')
                    {
                        s = buf_push(p, '\'', error);
                        if (s != VIGIL_STATUS_OK)
                            return s;
                        parser_advance(p);
                    }
                    return VIGIL_STATUS_OK;
                }
                s = buf_push(p, '\'', error);
                if (s != VIGIL_STATUS_OK)
                    return s;
            }
            s = buf_push(p, '\'', error);
            if (s != VIGIL_STATUS_OK)
                return s;
            continue;
        }
        parser_advance(p);
        s = buf_push(p, c, error);
        if (s != VIGIL_STATUS_OK)
            return s;
    }
    return parser_error(p, "unterminated multiline literal string", error);
}

/* Parse any string type, result in p->buf / p->buf_len. */
static vigil_status_t parse_string(toml_parser_t *p, vigil_error_t *error)
{
    char c = parser_peek(p);
    if (c == '"')
    {
        parser_advance(p);
        if (parser_peek(p) == '"')
        {
            parser_advance(p);
            if (parser_peek(p) == '"')
            {
                parser_advance(p);
                return parse_ml_basic_string(p, error);
            }
            /* Empty basic string. */
            buf_reset(p);
            return VIGIL_STATUS_OK;
        }
        return parse_basic_string(p, error);
    }
    if (c == '\'')
    {
        parser_advance(p);
        if (parser_peek(p) == '\'')
        {
            parser_advance(p);
            if (parser_peek(p) == '\'')
            {
                parser_advance(p);
                return parse_ml_literal_string(p, error);
            }
            buf_reset(p);
            return VIGIL_STATUS_OK;
        }
        return parse_literal_string(p, error);
    }
    return parser_error(p, "expected string", error);
}

/* ── Key parsing ─────────────────────────────────────────────────── */

static int is_bare_key_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

/* Parse a single key segment into p->buf. */
static vigil_status_t parse_simple_key(toml_parser_t *p, vigil_error_t *error)
{
    char c = parser_peek(p);
    if (c == '"' || c == '\'')
        return parse_string(p, error);
    /* Bare key. */
    buf_reset(p);
    if (!is_bare_key_char(c))
        return parser_error(p, "expected key", error);
    while (is_bare_key_char(parser_peek(p)))
    {
        if (buf_push(p, parser_peek(p), error) != VIGIL_STATUS_OK)
            return error->type;
        parser_advance(p);
    }
    return VIGIL_STATUS_OK;
}

/* Key path: array of (key, key_length) pairs. */
#define MAX_KEY_SEGMENTS 64

typedef struct key_path
{
    char *segments[MAX_KEY_SEGMENTS];
    size_t lengths[MAX_KEY_SEGMENTS];
    size_t count;
} key_path_t;

static void key_path_free(key_path_t *kp, const vigil_allocator_t *alloc)
{
    size_t i;
    vigil_allocator_t a = resolve_alloc(alloc);
    for (i = 0; i < kp->count; i++)
        toml_dealloc(&a, kp->segments[i]);
    kp->count = 0;
}

static vigil_status_t parse_key_path(toml_parser_t *p, key_path_t *kp, vigil_error_t *error)
{
    vigil_allocator_t a = resolve_alloc(p->allocator);
    kp->count = 0;
    for (;;)
    {
        char *seg;
        if (kp->count >= MAX_KEY_SEGMENTS)
            return parser_error(p, "key path too deep", error);
        if (parse_simple_key(p, error) != VIGIL_STATUS_OK)
            return error->type;
        seg = (char *)toml_alloc(&a, p->buf_len + 1);
        if (!seg)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "toml: allocation failed");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        memcpy(seg, p->buf, p->buf_len);
        seg[p->buf_len] = '\0';
        kp->segments[kp->count] = seg;
        kp->lengths[kp->count] = p->buf_len;
        kp->count++;
        skip_ws(p);
        if (!parser_match(p, '.'))
            break;
        skip_ws(p);
    }
    return VIGIL_STATUS_OK;
}

/* ── Number parsing ──────────────────────────────────────────────── */

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static vigil_status_t parse_integer_digits(toml_parser_t *p, int64_t *out, int base, vigil_error_t *error)
{
    int64_t val = 0;
    int count = 0;
    while (!parser_eof(p))
    {
        char c = parser_peek(p);
        int d;
        if (c == '_')
        {
            parser_advance(p);
            continue;
        }
        if (base == 16)
            d = hex_digit(c);
        else if (base == 8)
            d = (c >= '0' && c <= '7') ? c - '0' : -1;
        else if (base == 2)
            d = (c == '0' || c == '1') ? c - '0' : -1;
        else
            d = is_digit(c) ? c - '0' : -1;
        if (d < 0)
            break;
        val = val * base + d;
        count++;
        parser_advance(p);
    }
    if (count == 0)
        return parser_error(p, "expected digit", error);
    *out = val;
    return VIGIL_STATUS_OK;
}

/* Forward declaration. */
static vigil_status_t parse_value(toml_parser_t *p, vigil_toml_value_t **out, vigil_error_t *error);

static vigil_status_t parse_number(toml_parser_t *p, vigil_toml_value_t **out, vigil_error_t *error)
{
    int negative = 0;
    int64_t ival = 0;
    char c;

    c = parser_peek(p);
    if (c == '+' || c == '-')
    {
        negative = (c == '-');
        parser_advance(p);
    }

    c = parser_peek(p);

    /* Special float values. */
    if (c == 'i' || c == 'n')
    {
        size_t start = p->pos;
        if (p->pos + 3 <= p->length && memcmp(p->input + p->pos, "inf", 3) == 0)
        {
            p->pos += 3;
            p->col += 3;
            return vigil_toml_float_new(p->allocator, negative ? -HUGE_VAL : HUGE_VAL, out, error);
        }
        if (p->pos + 3 <= p->length && memcmp(p->input + p->pos, "nan", 3) == 0)
        {
            p->pos += 3;
            p->col += 3;
            double nan_val = NAN;
            if (negative)
                nan_val = -nan_val;
            return vigil_toml_float_new(p->allocator, nan_val, out, error);
        }
        (void)start;
        return parser_error(p, "invalid number", error);
    }

    /* Hex, octal, binary. */
    if (c == '0' && !negative)
    {
        char next = (p->pos + 1 < p->length) ? p->input[p->pos + 1] : '\0';
        if (next == 'x' || next == 'o' || next == 'b')
        {
            int base = (next == 'x') ? 16 : (next == 'o') ? 8 : 2;
            parser_advance(p);
            parser_advance(p);
            if (parse_integer_digits(p, &ival, base, error) != VIGIL_STATUS_OK)
                return error->type;
            return vigil_toml_integer_new(p->allocator, ival, out, error);
        }
    }

    /* Decimal integer or float. */
    if (parse_integer_digits(p, &ival, 10, error) != VIGIL_STATUS_OK)
        return error->type;

    c = parser_peek(p);
    if (c == '.' || c == 'e' || c == 'E')
    {
        /* Float. Rebuild from string for precision. */
        double fval = (double)ival;
        if (c == '.')
        {
            double frac = 0.0, scale = 0.1;
            parser_advance(p);
            if (!is_digit(parser_peek(p)))
                return parser_error(p, "expected digit after decimal point", error);
            while (!parser_eof(p))
            {
                c = parser_peek(p);
                if (c == '_')
                {
                    parser_advance(p);
                    continue;
                }
                if (!is_digit(c))
                    break;
                frac += (c - '0') * scale;
                scale *= 0.1;
                parser_advance(p);
            }
            fval += frac;
            c = parser_peek(p);
        }
        if (c == 'e' || c == 'E')
        {
            int exp_neg = 0;
            int64_t exp_val = 0;
            parser_advance(p);
            c = parser_peek(p);
            if (c == '+' || c == '-')
            {
                exp_neg = (c == '-');
                parser_advance(p);
            }
            if (parse_integer_digits(p, &exp_val, 10, error) != VIGIL_STATUS_OK)
                return error->type;
            if (exp_neg)
                exp_val = -exp_val;
            fval *= pow(10.0, (double)exp_val);
        }
        if (negative)
            fval = -fval;
        return vigil_toml_float_new(p->allocator, fval, out, error);
    }

    if (negative)
        ival = -ival;
    return vigil_toml_integer_new(p->allocator, ival, out, error);
}

/* ── DateTime parsing ────────────────────────────────────────────── */

static int parse_n_digits(toml_parser_t *p, int n)
{
    int val = 0, i;
    for (i = 0; i < n; i++)
    {
        char c = parser_peek(p);
        if (!is_digit(c))
            return -1;
        val = val * 10 + (c - '0');
        parser_advance(p);
    }
    return val;
}

static vigil_status_t parse_datetime_after_year(toml_parser_t *p, int year, vigil_toml_value_t **out,
                                                vigil_error_t *error)
{
    vigil_toml_datetime_t dt;
    memset(&dt, 0, sizeof(dt));
    dt.year = year;
    dt.has_date = 1;

    if (!parser_match(p, '-'))
        return parser_error(p, "expected '-' in date", error);
    dt.month = parse_n_digits(p, 2);
    if (dt.month < 0)
        return parser_error(p, "invalid month", error);
    if (!parser_match(p, '-'))
        return parser_error(p, "expected '-' in date", error);
    dt.day = parse_n_digits(p, 2);
    if (dt.day < 0)
        return parser_error(p, "invalid day", error);

    /* Optional time part. */
    {
        char c = parser_peek(p);
        if (c == 'T' || c == 't' || c == ' ')
        {
            parser_advance(p);
            dt.has_time = 1;
            dt.hour = parse_n_digits(p, 2);
            if (dt.hour < 0)
                return parser_error(p, "invalid hour", error);
            if (!parser_match(p, ':'))
                return parser_error(p, "expected ':' in time", error);
            dt.minute = parse_n_digits(p, 2);
            if (dt.minute < 0)
                return parser_error(p, "invalid minute", error);
            if (!parser_match(p, ':'))
                return parser_error(p, "expected ':' in time", error);
            dt.second = parse_n_digits(p, 2);
            if (dt.second < 0)
                return parser_error(p, "invalid second", error);

            /* Fractional seconds. */
            if (parser_match(p, '.'))
            {
                int frac = 0, digits = 0;
                while (is_digit(parser_peek(p)) && digits < 9)
                {
                    frac = frac * 10 + (parser_peek(p) - '0');
                    parser_advance(p);
                    digits++;
                }
                /* Pad to nanoseconds. */
                while (digits < 9)
                {
                    frac *= 10;
                    digits++;
                }
                dt.nanosecond = frac;
                /* Skip extra precision digits. */
                while (is_digit(parser_peek(p)))
                    parser_advance(p);
            }

            /* Offset. */
            c = parser_peek(p);
            if (c == 'Z' || c == 'z')
            {
                parser_advance(p);
                dt.has_offset = 1;
                dt.offset_minutes = 0;
            }
            else if (c == '+' || c == '-')
            {
                int sign = (c == '-') ? -1 : 1;
                int oh, om;
                parser_advance(p);
                oh = parse_n_digits(p, 2);
                if (oh < 0)
                    return parser_error(p, "invalid offset hour", error);
                if (!parser_match(p, ':'))
                    return parser_error(p, "expected ':' in offset", error);
                om = parse_n_digits(p, 2);
                if (om < 0)
                    return parser_error(p, "invalid offset minute", error);
                dt.has_offset = 1;
                dt.offset_minutes = sign * (oh * 60 + om);
            }
        }
    }

    return vigil_toml_datetime_new(p->allocator, &dt, out, error);
}

/* Parse a local time (HH:MM:SS[.frac]). */
static vigil_status_t parse_local_time(toml_parser_t *p, int hour, vigil_toml_value_t **out, vigil_error_t *error)
{
    vigil_toml_datetime_t dt;
    memset(&dt, 0, sizeof(dt));
    dt.has_time = 1;
    dt.hour = hour;
    if (!parser_match(p, ':'))
        return parser_error(p, "expected ':' in time", error);
    dt.minute = parse_n_digits(p, 2);
    if (dt.minute < 0)
        return parser_error(p, "invalid minute", error);
    if (!parser_match(p, ':'))
        return parser_error(p, "expected ':' in time", error);
    dt.second = parse_n_digits(p, 2);
    if (dt.second < 0)
        return parser_error(p, "invalid second", error);
    if (parser_match(p, '.'))
    {
        int frac = 0, digits = 0;
        while (is_digit(parser_peek(p)) && digits < 9)
        {
            frac = frac * 10 + (parser_peek(p) - '0');
            parser_advance(p);
            digits++;
        }
        while (digits < 9)
        {
            frac *= 10;
            digits++;
        }
        dt.nanosecond = frac;
        while (is_digit(parser_peek(p)))
            parser_advance(p);
    }
    return vigil_toml_datetime_new(p->allocator, &dt, out, error);
}

/* ── Value parsing ───────────────────────────────────────────────── */

static vigil_status_t parse_inline_table(toml_parser_t *p, vigil_toml_value_t **out, vigil_error_t *error)
{
    vigil_toml_value_t *tbl = NULL;
    vigil_status_t s = vigil_toml_table_new(p->allocator, &tbl, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    /* Opening { already consumed. */
    skip_ws(p);
    if (parser_peek(p) == '}')
    {
        parser_advance(p);
        *out = tbl;
        return VIGIL_STATUS_OK;
    }
    for (;;)
    {
        key_path_t kp;
        vigil_toml_value_t *val = NULL;
        vigil_toml_value_t *target = tbl;
        size_t i;

        memset(&kp, 0, sizeof(kp));
        skip_ws(p);
        s = parse_key_path(p, &kp, error);
        if (s != VIGIL_STATUS_OK)
        {
            key_path_free(&kp, p->allocator);
            vigil_toml_free(&tbl);
            return s;
        }
        skip_ws(p);
        if (!parser_match(p, '='))
        {
            key_path_free(&kp, p->allocator);
            vigil_toml_free(&tbl);
            return parser_error(p, "expected '=' in inline table", error);
        }
        skip_ws(p);
        /* Navigate/create intermediate tables for dotted keys. */
        for (i = 0; i + 1 < kp.count; i++)
        {
            const vigil_toml_value_t *existing = vigil_toml_table_get(target, kp.segments[i]);
            if (existing && vigil_toml_type(existing) == VIGIL_TOML_TABLE)
            {
                target = (vigil_toml_value_t *)existing;
            }
            else if (!existing)
            {
                vigil_toml_value_t *sub = NULL;
                s = vigil_toml_table_new(p->allocator, &sub, error);
                if (s != VIGIL_STATUS_OK)
                {
                    key_path_free(&kp, p->allocator);
                    vigil_toml_free(&tbl);
                    return s;
                }
                s = vigil_toml_table_set(target, kp.segments[i], kp.lengths[i], sub, error);
                if (s != VIGIL_STATUS_OK)
                {
                    vigil_toml_free(&sub);
                    key_path_free(&kp, p->allocator);
                    vigil_toml_free(&tbl);
                    return s;
                }
                target = sub;
            }
            else
            {
                key_path_free(&kp, p->allocator);
                vigil_toml_free(&tbl);
                return parser_error(p, "key conflict in inline table", error);
            }
        }
        s = parse_value(p, &val, error);
        if (s != VIGIL_STATUS_OK)
        {
            key_path_free(&kp, p->allocator);
            vigil_toml_free(&tbl);
            return s;
        }
        s = vigil_toml_table_set(target, kp.segments[kp.count - 1], kp.lengths[kp.count - 1], val, error);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_toml_free(&val);
            key_path_free(&kp, p->allocator);
            vigil_toml_free(&tbl);
            return s;
        }
        key_path_free(&kp, p->allocator);
        skip_ws(p);
        if (parser_match(p, '}'))
        {
            *out = tbl;
            return VIGIL_STATUS_OK;
        }
        if (!parser_match(p, ','))
        {
            vigil_toml_free(&tbl);
            return parser_error(p, "expected ',' or '}' in inline table", error);
        }
    }
}

static vigil_status_t parse_array_value(toml_parser_t *p, vigil_toml_value_t **out, vigil_error_t *error)
{
    vigil_toml_value_t *arr = NULL;
    vigil_status_t s = vigil_toml_array_new(p->allocator, &arr, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    /* Opening [ already consumed. */
    skip_ws_and_newlines(p);
    if (parser_peek(p) == ']')
    {
        parser_advance(p);
        *out = arr;
        return VIGIL_STATUS_OK;
    }
    for (;;)
    {
        vigil_toml_value_t *elem = NULL;
        skip_ws_and_newlines(p);
        s = parse_value(p, &elem, error);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_toml_free(&arr);
            return s;
        }
        s = vigil_toml_array_push(arr, elem, error);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_toml_free(&elem);
            vigil_toml_free(&arr);
            return s;
        }
        skip_ws_and_newlines(p);
        if (parser_match(p, ','))
        {
            skip_ws_and_newlines(p);
            if (parser_peek(p) == ']')
            {
                parser_advance(p);
                *out = arr;
                return VIGIL_STATUS_OK;
            }
            continue;
        }
        if (parser_match(p, ']'))
        {
            *out = arr;
            return VIGIL_STATUS_OK;
        }
        vigil_toml_free(&arr);
        return parser_error(p, "expected ',' or ']' in array", error);
    }
}

static vigil_status_t parse_value(toml_parser_t *p, vigil_toml_value_t **out, vigil_error_t *error)
{
    char c = parser_peek(p);

    /* String. */
    if (c == '"' || c == '\'')
    {
        vigil_status_t s = parse_string(p, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        return vigil_toml_string_new(p->allocator, p->buf, p->buf_len, out, error);
    }

    /* Bool. */
    if (c == 't' && p->pos + 4 <= p->length && memcmp(p->input + p->pos, "true", 4) == 0)
    {
        char after = (p->pos + 4 < p->length) ? p->input[p->pos + 4] : '\0';
        if (!is_bare_key_char(after))
        {
            p->pos += 4;
            p->col += 4;
            return vigil_toml_bool_new(p->allocator, 1, out, error);
        }
    }
    if (c == 'f' && p->pos + 5 <= p->length && memcmp(p->input + p->pos, "false", 5) == 0)
    {
        char after = (p->pos + 5 < p->length) ? p->input[p->pos + 5] : '\0';
        if (!is_bare_key_char(after))
        {
            p->pos += 5;
            p->col += 5;
            return vigil_toml_bool_new(p->allocator, 0, out, error);
        }
    }

    /* Inline table. */
    if (c == '{')
    {
        parser_advance(p);
        return parse_inline_table(p, out, error);
    }

    /* Array. */
    if (c == '[')
    {
        parser_advance(p);
        return parse_array_value(p, out, error);
    }

    /* Number, datetime, or inf/nan. */
    if (is_digit(c) || c == '+' || c == '-' || c == 'i' || c == 'n')
    {
        /*
         * Disambiguate datetime vs number.
         * If we see 4 digits followed by '-', it's a date.
         * If we see 2 digits followed by ':', it's a local time.
         */
        if (is_digit(c))
        {
            size_t saved_pos = p->pos;
            size_t saved_line = p->line;
            size_t saved_col = p->col;
            int d1 = parse_n_digits(p, 4);
            if (d1 >= 0 && parser_peek(p) == '-')
            {
                /* Looks like a date: YYYY-... */
                return parse_datetime_after_year(p, d1, out, error);
            }
            /* Restore and check for local time: HH:... */
            p->pos = saved_pos;
            p->line = saved_line;
            p->col = saved_col;
            d1 = parse_n_digits(p, 2);
            if (d1 >= 0 && parser_peek(p) == ':')
            {
                return parse_local_time(p, d1, out, error);
            }
            /* Restore and parse as number. */
            p->pos = saved_pos;
            p->line = saved_line;
            p->col = saved_col;
        }
        return parse_number(p, out, error);
    }

    return parser_error(p, "unexpected character", error);
}

/* ── Document parser ─────────────────────────────────────────────── */

/*
 * Navigate a key path in the root table, creating implicit tables as needed.
 * Returns the target table for the final key segment.
 */
static vigil_toml_value_t *navigate_to_table(vigil_toml_value_t *root, key_path_t *kp,
                                             const vigil_allocator_t *allocator, vigil_error_t *error)
{
    vigil_toml_value_t *cur = root;
    size_t i;
    for (i = 0; i < kp->count; i++)
    {
        const vigil_toml_value_t *existing = vigil_toml_table_get(cur, kp->segments[i]);
        if (existing)
        {
            if (vigil_toml_type(existing) == VIGIL_TOML_TABLE)
            {
                cur = (vigil_toml_value_t *)existing;
            }
            else if (vigil_toml_type(existing) == VIGIL_TOML_ARRAY)
            {
                /* Array of tables: navigate to last element. */
                size_t cnt = vigil_toml_array_count(existing);
                if (cnt == 0)
                {
                    return NULL;
                }
                cur = (vigil_toml_value_t *)vigil_toml_array_get(existing, cnt - 1);
                if (!cur || vigil_toml_type(cur) != VIGIL_TOML_TABLE)
                    return NULL;
            }
            else
            {
                return NULL;
            }
        }
        else
        {
            vigil_toml_value_t *sub = NULL;
            if (vigil_toml_table_new(allocator, &sub, error) != VIGIL_STATUS_OK)
                return NULL;
            if (vigil_toml_table_set(cur, kp->segments[i], kp->lengths[i], sub, error) != VIGIL_STATUS_OK)
            {
                vigil_toml_free(&sub);
                return NULL;
            }
            cur = sub;
        }
    }
    return cur;
}

vigil_status_t vigil_toml_parse(const vigil_allocator_t *allocator, const char *input, size_t length,
                                vigil_toml_value_t **out, vigil_error_t *error)
{
    toml_parser_t p;
    vigil_toml_value_t *root = NULL;
    vigil_toml_value_t *current_table = NULL;
    vigil_status_t s;

    if (!out)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "toml: out is NULL");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out = NULL;

    parser_init(&p, allocator, input, length);
    s = vigil_toml_table_new(allocator, &root, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    current_table = root;

    while (!parser_eof(&p))
    {
        skip_ws_and_newlines(&p);
        if (parser_eof(&p))
            break;

        char c = parser_peek(&p);

        /* Table header or array-of-tables. */
        if (c == '[')
        {
            key_path_t kp;
            int is_array_table = 0;
            memset(&kp, 0, sizeof(kp));

            parser_advance(&p);
            if (parser_peek(&p) == '[')
            {
                parser_advance(&p);
                is_array_table = 1;
            }
            skip_ws(&p);
            s = parse_key_path(&p, &kp, error);
            if (s != VIGIL_STATUS_OK)
            {
                key_path_free(&kp, allocator);
                vigil_toml_free(&root);
                parser_free_buf(&p);
                return s;
            }
            skip_ws(&p);
            if (!parser_match(&p, ']') || (is_array_table && !parser_match(&p, ']')))
            {
                key_path_free(&kp, allocator);
                vigil_toml_free(&root);
                parser_free_buf(&p);
                return parser_error(&p, "expected ']' after table header", error);
            }
            skip_to_newline(&p);

            if (is_array_table)
            {
                /* [[array.of.tables]] */
                key_path_t parent_kp;
                vigil_toml_value_t *parent;
                const char *last_key;
                size_t last_len;
                const vigil_toml_value_t *existing;
                vigil_toml_value_t *new_table = NULL;

                parent_kp.count = kp.count - 1;
                memcpy(parent_kp.segments, kp.segments, parent_kp.count * sizeof(char *));
                memcpy(parent_kp.lengths, kp.lengths, parent_kp.count * sizeof(size_t));

                parent = navigate_to_table(root, &parent_kp, allocator, error);
                if (!parent)
                {
                    key_path_free(&kp, allocator);
                    vigil_toml_free(&root);
                    parser_free_buf(&p);
                    return parser_error(&p, "cannot navigate to array-of-tables parent", error);
                }

                last_key = kp.segments[kp.count - 1];
                last_len = kp.lengths[kp.count - 1];
                existing = vigil_toml_table_get(parent, last_key);

                if (!existing)
                {
                    /* Create the array. */
                    vigil_toml_value_t *arr = NULL;
                    s = vigil_toml_array_new(allocator, &arr, error);
                    if (s != VIGIL_STATUS_OK)
                    {
                        key_path_free(&kp, allocator);
                        vigil_toml_free(&root);
                        parser_free_buf(&p);
                        return s;
                    }
                    s = vigil_toml_table_set(parent, last_key, last_len, arr, error);
                    if (s != VIGIL_STATUS_OK)
                    {
                        vigil_toml_free(&arr);
                        key_path_free(&kp, allocator);
                        vigil_toml_free(&root);
                        parser_free_buf(&p);
                        return s;
                    }
                    existing = arr;
                }

                if (vigil_toml_type(existing) != VIGIL_TOML_ARRAY)
                {
                    key_path_free(&kp, allocator);
                    vigil_toml_free(&root);
                    parser_free_buf(&p);
                    return parser_error(&p, "expected array for [[...]]", error);
                }

                s = vigil_toml_table_new(allocator, &new_table, error);
                if (s != VIGIL_STATUS_OK)
                {
                    key_path_free(&kp, allocator);
                    vigil_toml_free(&root);
                    parser_free_buf(&p);
                    return s;
                }
                s = vigil_toml_array_push((vigil_toml_value_t *)existing, new_table, error);
                if (s != VIGIL_STATUS_OK)
                {
                    vigil_toml_free(&new_table);
                    key_path_free(&kp, allocator);
                    vigil_toml_free(&root);
                    parser_free_buf(&p);
                    return s;
                }
                current_table = new_table;
            }
            else
            {
                /* [table] */
                current_table = navigate_to_table(root, &kp, allocator, error);
                if (!current_table)
                {
                    key_path_free(&kp, allocator);
                    vigil_toml_free(&root);
                    parser_free_buf(&p);
                    return parser_error(&p, "cannot create table (key conflict)", error);
                }
            }
            key_path_free(&kp, allocator);
            continue;
        }

        /* Key = value. */
        {
            key_path_t kp;
            vigil_toml_value_t *val = NULL;
            vigil_toml_value_t *target;
            size_t i;

            memset(&kp, 0, sizeof(kp));
            s = parse_key_path(&p, &kp, error);
            if (s != VIGIL_STATUS_OK)
            {
                key_path_free(&kp, allocator);
                vigil_toml_free(&root);
                parser_free_buf(&p);
                return s;
            }
            skip_ws(&p);
            if (!parser_match(&p, '='))
            {
                key_path_free(&kp, allocator);
                vigil_toml_free(&root);
                parser_free_buf(&p);
                return parser_error(&p, "expected '='", error);
            }
            skip_ws(&p);
            s = parse_value(&p, &val, error);
            if (s != VIGIL_STATUS_OK)
            {
                key_path_free(&kp, allocator);
                vigil_toml_free(&root);
                parser_free_buf(&p);
                return s;
            }
            skip_to_newline(&p);

            /* Navigate dotted key path. */
            target = current_table;
            for (i = 0; i + 1 < kp.count; i++)
            {
                const vigil_toml_value_t *existing = vigil_toml_table_get(target, kp.segments[i]);
                if (existing && vigil_toml_type(existing) == VIGIL_TOML_TABLE)
                {
                    target = (vigil_toml_value_t *)existing;
                }
                else if (!existing)
                {
                    vigil_toml_value_t *sub = NULL;
                    s = vigil_toml_table_new(allocator, &sub, error);
                    if (s != VIGIL_STATUS_OK)
                    {
                        vigil_toml_free(&val);
                        key_path_free(&kp, allocator);
                        vigil_toml_free(&root);
                        parser_free_buf(&p);
                        return s;
                    }
                    s = vigil_toml_table_set(target, kp.segments[i], kp.lengths[i], sub, error);
                    if (s != VIGIL_STATUS_OK)
                    {
                        vigil_toml_free(&sub);
                        vigil_toml_free(&val);
                        key_path_free(&kp, allocator);
                        vigil_toml_free(&root);
                        parser_free_buf(&p);
                        return s;
                    }
                    target = sub;
                }
                else
                {
                    vigil_toml_free(&val);
                    key_path_free(&kp, allocator);
                    vigil_toml_free(&root);
                    parser_free_buf(&p);
                    return parser_error(&p, "key conflict (not a table)", error);
                }
            }

            s = vigil_toml_table_set(target, kp.segments[kp.count - 1], kp.lengths[kp.count - 1], val, error);
            if (s != VIGIL_STATUS_OK)
            {
                vigil_toml_free(&val);
                key_path_free(&kp, allocator);
                vigil_toml_free(&root);
                parser_free_buf(&p);
                return s;
            }
            key_path_free(&kp, allocator);
        }
    }

    parser_free_buf(&p);
    *out = root;
    return VIGIL_STATUS_OK;
}

/* ── Emitter ─────────────────────────────────────────────────────── */

typedef struct toml_emitter
{
    char *buf;
    size_t len;
    size_t cap;
    vigil_allocator_t allocator;
} toml_emitter_t;

static void emit_init(toml_emitter_t *e, const vigil_allocator_t *alloc)
{
    memset(e, 0, sizeof(*e));
    e->allocator = resolve_alloc(alloc);
}

static vigil_status_t emit_grow(toml_emitter_t *e, size_t need, vigil_error_t *error)
{
    if (e->len + need <= e->cap)
        return VIGIL_STATUS_OK;
    {
        size_t cap = e->cap ? e->cap : 128;
        while (cap < e->len + need)
            cap *= 2;
        {
            char *nb = (char *)toml_realloc(&e->allocator, e->buf, cap);
            if (!nb)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "toml: allocation failed");
                return VIGIL_STATUS_OUT_OF_MEMORY;
            }
            e->buf = nb;
            e->cap = cap;
        }
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_str(toml_emitter_t *e, const char *s, size_t len, vigil_error_t *error)
{
    vigil_status_t st = emit_grow(e, len, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    memcpy(e->buf + e->len, s, len);
    e->len += len;
    return VIGIL_STATUS_OK;
}

static vigil_status_t emit_cstr(toml_emitter_t *e, const char *s, vigil_error_t *error)
{
    return emit_str(e, s, strlen(s), error);
}

static vigil_status_t emit_char(toml_emitter_t *e, char c, vigil_error_t *error)
{
    return emit_str(e, &c, 1, error);
}

static int needs_quoting(const char *key, size_t len)
{
    size_t i;
    if (len == 0)
        return 1;
    for (i = 0; i < len; i++)
    {
        if (!is_bare_key_char(key[i]))
            return 1;
    }
    return 0;
}

static vigil_status_t emit_key(toml_emitter_t *e, const char *key, size_t len, vigil_error_t *error)
{
    if (needs_quoting(key, len))
    {
        vigil_status_t s = emit_char(e, '"', error);
        if (s != VIGIL_STATUS_OK)
            return s;
        /* Escape special chars. */
        {
            size_t i;
            for (i = 0; i < len; i++)
            {
                char c = key[i];
                if (c == '"')
                {
                    s = emit_cstr(e, "\\\"", error);
                }
                else if (c == '\\')
                {
                    s = emit_cstr(e, "\\\\", error);
                }
                else if (c == '\n')
                {
                    s = emit_cstr(e, "\\n", error);
                }
                else if (c == '\t')
                {
                    s = emit_cstr(e, "\\t", error);
                }
                else
                {
                    s = emit_char(e, c, error);
                }
                if (s != VIGIL_STATUS_OK)
                    return s;
            }
        }
        return emit_char(e, '"', error);
    }
    return emit_str(e, key, len, error);
}

static vigil_status_t emit_quoted_string(toml_emitter_t *e, const char *s, size_t len, vigil_error_t *error)
{
    vigil_status_t st;
    size_t i;
    st = emit_char(e, '"', error);
    if (st != VIGIL_STATUS_OK)
        return st;
    for (i = 0; i < len; i++)
    {
        char c = s[i];
        if (c == '"')
            st = emit_cstr(e, "\\\"", error);
        else if (c == '\\')
            st = emit_cstr(e, "\\\\", error);
        else if (c == '\b')
            st = emit_cstr(e, "\\b", error);
        else if (c == '\f')
            st = emit_cstr(e, "\\f", error);
        else if (c == '\n')
            st = emit_cstr(e, "\\n", error);
        else if (c == '\r')
            st = emit_cstr(e, "\\r", error);
        else if (c == '\t')
            st = emit_cstr(e, "\\t", error);
        else
            st = emit_char(e, c, error);
        if (st != VIGIL_STATUS_OK)
            return st;
    }
    return emit_char(e, '"', error);
}

/* Forward declare. */
static vigil_status_t emit_value(toml_emitter_t *e, const vigil_toml_value_t *v, vigil_error_t *error);

static vigil_status_t emit_inline_table(toml_emitter_t *e, const vigil_toml_value_t *t, vigil_error_t *error)
{
    size_t i, count = vigil_toml_table_count(t);
    vigil_status_t s = emit_cstr(e, "{ ", error);
    if (s != VIGIL_STATUS_OK)
        return s;
    for (i = 0; i < count; i++)
    {
        const char *key;
        size_t klen;
        const vigil_toml_value_t *val;
        vigil_toml_table_entry(t, i, &key, &klen, &val);
        if (i > 0)
        {
            s = emit_cstr(e, ", ", error);
            if (s != VIGIL_STATUS_OK)
                return s;
        }
        s = emit_key(e, key, klen, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = emit_cstr(e, " = ", error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = emit_value(e, val, error);
        if (s != VIGIL_STATUS_OK)
            return s;
    }
    return emit_cstr(e, " }", error);
}

static vigil_status_t emit_value(toml_emitter_t *e, const vigil_toml_value_t *v, vigil_error_t *error)
{
    char tmp[64];
    switch (vigil_toml_type(v))
    {
    case VIGIL_TOML_STRING:
        return emit_quoted_string(e, vigil_toml_string_value(v), vigil_toml_string_length(v), error);
    case VIGIL_TOML_INTEGER:
        snprintf(tmp, sizeof(tmp), "%" PRId64, vigil_toml_integer_value(v));
        return emit_cstr(e, tmp, error);
    case VIGIL_TOML_FLOAT: {
        double f = vigil_toml_float_value(v);
        if (f != f)
            return emit_cstr(e, "nan", error);
        if (f == HUGE_VAL)
            return emit_cstr(e, "inf", error);
        if (f == -HUGE_VAL)
            return emit_cstr(e, "-inf", error);
        snprintf(tmp, sizeof(tmp), "%.17g", f);
        /* Ensure there's a decimal point. */
        if (!strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'E'))
        {
            size_t l = strlen(tmp);
            tmp[l] = '.';
            tmp[l + 1] = '0';
            tmp[l + 2] = '\0';
        }
        return emit_cstr(e, tmp, error);
    }
    case VIGIL_TOML_BOOL:
        return emit_cstr(e, vigil_toml_bool_value(v) ? "true" : "false", error);
    case VIGIL_TOML_DATETIME: {
        const vigil_toml_datetime_t *dt = vigil_toml_datetime_value(v);
        if (dt->has_date)
        {
            snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d", dt->year, dt->month, dt->day);
            {
                vigil_status_t s = emit_cstr(e, tmp, error);
                if (s != VIGIL_STATUS_OK)
                    return s;
            }
            if (dt->has_time)
            {
                vigil_status_t s = emit_char(e, 'T', error);
                if (s != VIGIL_STATUS_OK)
                    return s;
            }
        }
        if (dt->has_time)
        {
            snprintf(tmp, sizeof(tmp), "%02d:%02d:%02d", dt->hour, dt->minute, dt->second);
            {
                vigil_status_t s = emit_cstr(e, tmp, error);
                if (s != VIGIL_STATUS_OK)
                    return s;
            }
            if (dt->nanosecond)
            {
                snprintf(tmp, sizeof(tmp), ".%09d", dt->nanosecond);
                /* Trim trailing zeros. */
                {
                    size_t l = strlen(tmp);
                    while (l > 2 && tmp[l - 1] == '0')
                        l--;
                    tmp[l] = '\0';
                }
                {
                    vigil_status_t s = emit_cstr(e, tmp, error);
                    if (s != VIGIL_STATUS_OK)
                        return s;
                }
            }
            if (dt->has_offset)
            {
                if (dt->offset_minutes == 0)
                {
                    vigil_status_t s = emit_char(e, 'Z', error);
                    if (s != VIGIL_STATUS_OK)
                        return s;
                }
                else
                {
                    int off = dt->offset_minutes;
                    char sign = off < 0 ? '-' : '+';
                    if (off < 0)
                        off = -off;
                    snprintf(tmp, sizeof(tmp), "%c%02d:%02d", sign, off / 60, off % 60);
                    {
                        vigil_status_t s = emit_cstr(e, tmp, error);
                        if (s != VIGIL_STATUS_OK)
                            return s;
                    }
                }
            }
        }
        return VIGIL_STATUS_OK;
    }
    case VIGIL_TOML_ARRAY: {
        size_t i, count = vigil_toml_array_count(v);
        vigil_status_t s = emit_char(e, '[', error);
        if (s != VIGIL_STATUS_OK)
            return s;
        for (i = 0; i < count; i++)
        {
            if (i > 0)
            {
                s = emit_cstr(e, ", ", error);
                if (s != VIGIL_STATUS_OK)
                    return s;
            }
            s = emit_value(e, vigil_toml_array_get(v, i), error);
            if (s != VIGIL_STATUS_OK)
                return s;
        }
        return emit_char(e, ']', error);
    }
    case VIGIL_TOML_TABLE:
        return emit_inline_table(e, v, error);
    }
    return VIGIL_STATUS_OK;
}

/* Check if a table value should be emitted as a [section] rather than inline. */
static int is_section_table(const vigil_toml_value_t *v)
{
    size_t i, count;
    if (vigil_toml_type(v) != VIGIL_TOML_TABLE)
        return 0;
    count = vigil_toml_table_count(v);
    for (i = 0; i < count; i++)
    {
        const vigil_toml_value_t *child;
        vigil_toml_table_entry(v, i, NULL, NULL, &child);
        if (vigil_toml_type(child) == VIGIL_TOML_TABLE || vigil_toml_type(child) == VIGIL_TOML_ARRAY)
            return 1;
    }
    /* Emit small tables inline, larger ones as sections. */
    return count > 3;
}

static int is_array_of_tables(const vigil_toml_value_t *v)
{
    size_t i, count;
    if (vigil_toml_type(v) != VIGIL_TOML_ARRAY)
        return 0;
    count = vigil_toml_array_count(v);
    if (count == 0)
        return 0;
    for (i = 0; i < count; i++)
    {
        if (vigil_toml_type(vigil_toml_array_get(v, i)) != VIGIL_TOML_TABLE)
            return 0;
    }
    return 1;
}

static vigil_status_t emit_table_body(toml_emitter_t *e, const vigil_toml_value_t *t, const char *prefix,
                                      size_t prefix_len, vigil_error_t *error);

static vigil_status_t emit_table_body(toml_emitter_t *e, const vigil_toml_value_t *t, const char *prefix,
                                      size_t prefix_len, vigil_error_t *error)
{
    size_t i, count = vigil_toml_table_count(t);
    vigil_status_t s;

    /* First pass: simple key/value pairs. */
    for (i = 0; i < count; i++)
    {
        const char *key;
        size_t klen;
        const vigil_toml_value_t *val;
        vigil_toml_table_entry(t, i, &key, &klen, &val);
        if (is_section_table(val) || is_array_of_tables(val))
            continue;
        s = emit_key(e, key, klen, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = emit_cstr(e, " = ", error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = emit_value(e, val, error);
        if (s != VIGIL_STATUS_OK)
            return s;
        s = emit_char(e, '\n', error);
        if (s != VIGIL_STATUS_OK)
            return s;
    }

    /* Second pass: sub-tables and arrays of tables. */
    for (i = 0; i < count; i++)
    {
        const char *key;
        size_t klen;
        const vigil_toml_value_t *val;
        vigil_toml_table_entry(t, i, &key, &klen, &val);

        if (is_section_table(val))
        {
            /* Build full key for section header. */
            char section_key[512];
            size_t sk_len = 0;
            if (prefix_len > 0)
            {
                memcpy(section_key, prefix, prefix_len);
                sk_len = prefix_len;
                section_key[sk_len++] = '.';
            }
            /* Use bare or quoted key. */
            if (needs_quoting(key, klen))
            {
                section_key[sk_len++] = '"';
                memcpy(section_key + sk_len, key, klen);
                sk_len += klen;
                section_key[sk_len++] = '"';
            }
            else
            {
                memcpy(section_key + sk_len, key, klen);
                sk_len += klen;
            }
            section_key[sk_len] = '\0';

            s = emit_cstr(e, "\n[", error);
            if (s != VIGIL_STATUS_OK)
                return s;
            s = emit_str(e, section_key, sk_len, error);
            if (s != VIGIL_STATUS_OK)
                return s;
            s = emit_cstr(e, "]\n", error);
            if (s != VIGIL_STATUS_OK)
                return s;
            s = emit_table_body(e, val, section_key, sk_len, error);
            if (s != VIGIL_STATUS_OK)
                return s;
        }
        else if (is_array_of_tables(val))
        {
            size_t j, arr_count = vigil_toml_array_count(val);
            char section_key[512];
            size_t sk_len = 0;
            if (prefix_len > 0)
            {
                memcpy(section_key, prefix, prefix_len);
                sk_len = prefix_len;
                section_key[sk_len++] = '.';
            }
            if (needs_quoting(key, klen))
            {
                section_key[sk_len++] = '"';
                memcpy(section_key + sk_len, key, klen);
                sk_len += klen;
                section_key[sk_len++] = '"';
            }
            else
            {
                memcpy(section_key + sk_len, key, klen);
                sk_len += klen;
            }
            section_key[sk_len] = '\0';

            for (j = 0; j < arr_count; j++)
            {
                const vigil_toml_value_t *elem = vigil_toml_array_get(val, j);
                s = emit_cstr(e, "\n[[", error);
                if (s != VIGIL_STATUS_OK)
                    return s;
                s = emit_str(e, section_key, sk_len, error);
                if (s != VIGIL_STATUS_OK)
                    return s;
                s = emit_cstr(e, "]]\n", error);
                if (s != VIGIL_STATUS_OK)
                    return s;
                s = emit_table_body(e, elem, section_key, sk_len, error);
                if (s != VIGIL_STATUS_OK)
                    return s;
            }
        }
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_toml_emit(const vigil_toml_value_t *value, char **out_string, size_t *out_length,
                               vigil_error_t *error)
{
    toml_emitter_t e;
    vigil_status_t s;

    if (!value || vigil_toml_type(value) != VIGIL_TOML_TABLE)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "toml: root must be a table");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    emit_init(&e, &value->allocator);
    s = emit_table_body(&e, value, "", 0, error);
    if (s != VIGIL_STATUS_OK)
    {
        toml_dealloc(&e.allocator, e.buf);
        return s;
    }

    /* Null-terminate. */
    s = emit_char(&e, '\0', error);
    if (s != VIGIL_STATUS_OK)
    {
        toml_dealloc(&e.allocator, e.buf);
        return s;
    }
    e.len--; /* Don't count null in length. */

    *out_string = e.buf;
    if (out_length)
        *out_length = e.len;
    return VIGIL_STATUS_OK;
}
