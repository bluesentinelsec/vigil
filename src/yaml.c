/* VIGIL YAML parsing library.
 *
 * Parses a subset of YAML 1.2:
 * - Scalars: strings, integers, floats, booleans, null
 * - Block mappings (key: value)
 * - Block sequences (- item)
 * - Comments (# ...)
 * - Quoted strings (single and double)
 * - Block scalars (| literal, > folded)
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/yaml.h"

/* ── Parser state ────────────────────────────────────────────────── */

typedef struct
{
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
    vigil_allocator_t alloc;
    vigil_error_t *error;
} yaml_parser_t;

/* ── Helpers ─────────────────────────────────────────────────────── */

static void *yaml_alloc(yaml_parser_t *p, size_t size)
{
    return p->alloc.allocate(p->alloc.user_data, size);
}

static void yaml_dealloc(yaml_parser_t *p, void *ptr)
{
    p->alloc.deallocate(p->alloc.user_data, ptr);
}

static char peek(yaml_parser_t *p)
{
    return p->pos < p->len ? p->src[p->pos] : '\0';
}

static char peek_at(yaml_parser_t *p, size_t offset)
{
    return (p->pos + offset) < p->len ? p->src[p->pos + offset] : '\0';
}

static void advance(yaml_parser_t *p)
{
    if (p->pos < p->len)
    {
        if (p->src[p->pos] == '\n')
        {
            p->line++;
            p->col = 1;
        }
        else
        {
            p->col++;
        }
        p->pos++;
    }
}

static void skip_spaces(yaml_parser_t *p)
{
    while (peek(p) == ' ' || peek(p) == '\t')
        advance(p);
}

static void skip_to_eol(yaml_parser_t *p)
{
    while (peek(p) && peek(p) != '\n')
        advance(p);
}

static void skip_comment(yaml_parser_t *p)
{
    if (peek(p) == '#')
        skip_to_eol(p);
}

static void skip_blank_lines(yaml_parser_t *p)
{
    while (p->pos < p->len)
    {
        size_t start = p->pos;
        skip_spaces(p);
        skip_comment(p);
        if (peek(p) == '\n')
        {
            advance(p);
        }
        else
        {
            /* Not a blank line - rewind to start */
            p->pos = start;
            break;
        }
    }
}

static size_t measure_indent(yaml_parser_t *p)
{
    size_t count = 0;
    while (peek_at(p, count) == ' ')
        count++;
    return count;
}

static void set_error(yaml_parser_t *p, const char *msg)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "yaml: line %zu: %s", p->line, msg);
    vigil_error_set_literal(p->error, VIGIL_STATUS_SYNTAX_ERROR, buf);
}

/* ── Forward declarations ────────────────────────────────────────── */

static vigil_status_t parse_value(yaml_parser_t *p, size_t min_indent, vigil_json_value_t **out);

/* ── Scalar parsing ──────────────────────────────────────────────── */

static int is_scalar_char(char c)
{
    return c && c != '\n' && c != '#' && c != ':';
}

static vigil_status_t parse_quoted_string(yaml_parser_t *p, char quote, vigil_json_value_t **out)
{
    advance(p); /* skip opening quote */
    size_t start = p->pos;

    /* First pass: count length with escapes */
    size_t len = 0;
    while (peek(p) && peek(p) != quote)
    {
        if (peek(p) == '\\' && quote == '"')
        {
            advance(p);
            if (peek(p))
            {
                advance(p);
                len++;
            }
        }
        else
        {
            advance(p);
            len++;
        }
    }

    /* Allocate and copy with escape processing */
    char *buf = (char *)yaml_alloc(p, len + 1);
    if (!buf)
    {
        set_error(p, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    p->pos = start;
    size_t i = 0;
    while (peek(p) && peek(p) != quote)
    {
        if (peek(p) == '\\' && quote == '"')
        {
            advance(p);
            char c = peek(p);
            if (c)
            {
                switch (c)
                {
                case 'n':
                    buf[i++] = '\n';
                    break;
                case 't':
                    buf[i++] = '\t';
                    break;
                case 'r':
                    buf[i++] = '\r';
                    break;
                case '\\':
                    buf[i++] = '\\';
                    break;
                case '"':
                    buf[i++] = '"';
                    break;
                default:
                    buf[i++] = c;
                    break;
                }
                advance(p);
            }
        }
        else
        {
            buf[i++] = peek(p);
            advance(p);
        }
    }
    buf[i] = '\0';

    if (peek(p) == quote)
        advance(p);

    vigil_status_t s = vigil_json_string_new(&p->alloc, buf, i, out, p->error);
    yaml_dealloc(p, buf);
    return s;
}

/* ── Buffer helper for block scalar ───────────────────────────────── */

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
} yaml_buf_t;

static vigil_status_t yaml_buf_push(yaml_buf_t *b, yaml_parser_t *p, char ch)
{
    if (b->len + 1 >= b->cap)
    {
        b->cap *= 2;
        char *nb = (char *)p->alloc.reallocate(p->alloc.user_data, b->data, b->cap);
        if (!nb)
        {
            yaml_dealloc(p, b->data);
            b->data = NULL;
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        b->data = nb;
    }
    b->data[b->len++] = ch;
    return VIGIL_STATUS_OK;
}

/* Copy one content line into buf, appending the appropriate line ending. */
static int block_scalar_copy_line(yaml_parser_t *p, yaml_buf_t *buf, size_t block_indent, char style)
{
    for (size_t i = 0; i < block_indent && peek(p) == ' '; i++)
        advance(p);
    while (peek(p) && peek(p) != '\n')
    {
        if (yaml_buf_push(buf, p, peek(p)) != VIGIL_STATUS_OK)
            return -1;
        advance(p);
    }
    if (peek(p) == '\n')
    {
        if (yaml_buf_push(buf, p, style == '|' ? '\n' : ' ') != VIGIL_STATUS_OK)
            return -1;
        advance(p);
    }
    return 1;
}

/* Process one line of a block scalar. Returns 1=consumed, 0=end, -1=OOM. */
static int block_scalar_line(yaml_parser_t *p, yaml_buf_t *buf, size_t block_indent, char style)
{
    size_t line_indent = measure_indent(p);

    /* Check for blank line */
    size_t check = p->pos + line_indent;
    if (check < p->len && (p->src[check] == '\n' || p->src[check] == '\0'))
    {
        while (peek(p) == ' ')
            advance(p);
        if (peek(p) == '\n')
        {
            if (yaml_buf_push(buf, p, '\n') != VIGIL_STATUS_OK)
                return -1;
            advance(p);
            return 1;
        }
    }

    if (line_indent < block_indent)
        return 0;

    return block_scalar_copy_line(p, buf, block_indent, style);
}

static vigil_status_t parse_block_scalar(yaml_parser_t *p, char style, vigil_json_value_t **out)
{
    advance(p); /* skip | or > */
    skip_spaces(p);
    skip_comment(p);
    if (peek(p) == '\n')
        advance(p);

    /* Determine block indent from first non-empty line */
    skip_blank_lines(p);
    size_t block_indent = measure_indent(p);
    if (block_indent == 0)
        return vigil_json_string_new(&p->alloc, "", 0, out, p->error);

    yaml_buf_t buf;
    buf.data = (char *)yaml_alloc(p, 256);
    if (!buf.data)
    {
        set_error(p, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    buf.len = 0;
    buf.cap = 256;

    while (p->pos < p->len)
    {
        int rc = block_scalar_line(p, &buf, block_indent, style);
        if (rc < 0)
        {
            yaml_dealloc(p, buf.data);
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        if (rc == 0)
            break;
    }

    /* Trim trailing whitespace */
    if (style == '>')
    {
        while (buf.len > 0 && (buf.data[buf.len - 1] == ' ' || buf.data[buf.len - 1] == '\n'))
            buf.len--;
    }
    else
    {
        while (buf.len > 1 && buf.data[buf.len - 1] == '\n' && buf.data[buf.len - 2] == '\n')
            buf.len--;
    }

    buf.data[buf.len] = '\0';
    vigil_status_t s = vigil_json_string_new(&p->alloc, buf.data, buf.len, out, p->error);
    yaml_dealloc(p, buf.data);
    return s;
}

static vigil_status_t parse_plain_scalar(yaml_parser_t *p, vigil_json_value_t **out)
{
    size_t start = p->pos;

    /* Find end of scalar */
    while (is_scalar_char(peek(p)))
    {
        /* Stop at ': ' which starts a mapping value */
        if (peek(p) == ':' && (peek_at(p, 1) == ' ' || peek_at(p, 1) == '\n' || peek_at(p, 1) == '\0'))
            break;
        advance(p);
    }

    /* Trim trailing spaces */
    size_t end = p->pos;
    while (end > start && p->src[end - 1] == ' ')
        end--;

    size_t len = end - start;
    const char *str = p->src + start;

    /* Check for special values */
    if (len == 4 && strncmp(str, "true", 4) == 0)
    {
        return vigil_json_bool_new(&p->alloc, 1, out, p->error);
    }
    if (len == 5 && strncmp(str, "false", 5) == 0)
    {
        return vigil_json_bool_new(&p->alloc, 0, out, p->error);
    }
    if (len == 4 && strncmp(str, "null", 4) == 0)
    {
        return vigil_json_null_new(&p->alloc, out, p->error);
    }
    if (len == 1 && str[0] == '~')
    {
        return vigil_json_null_new(&p->alloc, out, p->error);
    }

    /* Try to parse as number */
    char *numstr = (char *)yaml_alloc(p, len + 1);
    if (!numstr)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    memcpy(numstr, str, len);
    numstr[len] = '\0';

    char *endptr;
    double num = strtod(numstr, &endptr);
    if (endptr == numstr + len && len > 0)
    {
        yaml_dealloc(p, numstr);
        return vigil_json_number_new(&p->alloc, num, out, p->error);
    }
    yaml_dealloc(p, numstr);

    /* Return as string */
    return vigil_json_string_new(&p->alloc, str, len, out, p->error);
}

/* Parse an inline scalar value (quoted string, block scalar, or plain). */
static vigil_status_t yaml_parse_inline_value(yaml_parser_t *p, vigil_json_value_t **out)
{
    char c = peek(p);
    if (c == '"' || c == '\'')
        return parse_quoted_string(p, c, out);
    if (c == '|' || c == '>')
        return parse_block_scalar(p, c, out);
    return parse_plain_scalar(p, out);
}

/* Parse a value that is either on the next line (indented) or inline. */
static vigil_status_t yaml_parse_next_value(yaml_parser_t *p, size_t parent_indent, vigil_json_value_t **item)
{
    skip_spaces(p);
    skip_comment(p);

    if (peek(p) == '\n' || peek(p) == '\0')
    {
        if (peek(p) == '\n')
            advance(p);
        skip_blank_lines(p);
        size_t child_indent = measure_indent(p);
        if (child_indent > parent_indent)
            return parse_value(p, child_indent, item);
        return vigil_json_null_new(&p->alloc, item, p->error);
    }

    vigil_status_t s = yaml_parse_inline_value(p, item);
    if (s != VIGIL_STATUS_OK)
        return s;
    skip_spaces(p);
    skip_comment(p);
    if (peek(p) == '\n')
        advance(p);
    return VIGIL_STATUS_OK;
}

static vigil_status_t parse_sequence(yaml_parser_t *p, size_t seq_indent, vigil_json_value_t **out)
{
    vigil_status_t s = vigil_json_array_new(&p->alloc, out, p->error);
    if (s != VIGIL_STATUS_OK)
        return s;

    while (p->pos < p->len)
    {
        skip_blank_lines(p);

        size_t indent = measure_indent(p);
        if (indent != seq_indent)
            break;

        /* Skip indent */
        for (size_t i = 0; i < indent; i++)
            advance(p);

        if (peek(p) != '-')
            break;
        advance(p); /* skip '-' */

        if (peek(p) != ' ' && peek(p) != '\n')
            break;
        if (peek(p) == ' ')
            advance(p);

        vigil_json_value_t *item = NULL;
        s = yaml_parse_next_value(p, seq_indent, &item);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(out);
            return s;
        }

        s = vigil_json_array_push(*out, item, p->error);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(&item);
            vigil_json_free(out);
            return s;
        }
    }

    return VIGIL_STATUS_OK;
}

/* ── Mapping parsing ─────────────────────────────────────────────── */

static vigil_status_t parse_mapping(yaml_parser_t *p, size_t map_indent, vigil_json_value_t **out)
{
    vigil_status_t s = vigil_json_object_new(&p->alloc, out, p->error);
    if (s != VIGIL_STATUS_OK)
        return s;

    while (p->pos < p->len)
    {
        skip_blank_lines(p);

        size_t indent = measure_indent(p);
        if (indent != map_indent)
            break;

        /* Skip indent */
        for (size_t i = 0; i < indent; i++)
            advance(p);

        /* Check for sequence start */
        if (peek(p) == '-')
            break;

        /* Parse key */
        size_t key_start = p->pos;
        while (peek(p) && peek(p) != ':' && peek(p) != '\n')
            advance(p);
        size_t key_end = p->pos;

        /* Trim trailing spaces from key */
        while (key_end > key_start && p->src[key_end - 1] == ' ')
            key_end--;

        if (peek(p) != ':')
        {
            set_error(p, "expected ':'");
            vigil_json_free(out);
            return VIGIL_STATUS_SYNTAX_ERROR;
        }
        advance(p); /* skip ':' */

        vigil_json_value_t *value = NULL;
        s = yaml_parse_next_value(p, map_indent, &value);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(out);
            return s;
        }

        s = vigil_json_object_set(*out, p->src + key_start, key_end - key_start, value, p->error);
        if (s != VIGIL_STATUS_OK)
        {
            vigil_json_free(&value);
            vigil_json_free(out);
            return s;
        }
    }

    return VIGIL_STATUS_OK;
}

/* ── Main value parser ───────────────────────────────────────────── */

static vigil_status_t parse_value(yaml_parser_t *p, size_t min_indent, vigil_json_value_t **out)
{
    skip_blank_lines(p);

    size_t indent = measure_indent(p);
    if (indent < min_indent && p->pos < p->len)
    {
        /* Dedented - return null */
        return vigil_json_null_new(&p->alloc, out, p->error);
    }

    /* Skip to content */
    for (size_t i = 0; i < indent; i++)
        advance(p);

    char c = peek(p);

    /* Block scalar */
    if (c == '|' || c == '>')
    {
        return parse_block_scalar(p, c, out);
    }

    /* Quoted string */
    if (c == '"' || c == '\'')
    {
        return parse_quoted_string(p, c, out);
    }

    /* Sequence */
    if (c == '-' && (peek_at(p, 1) == ' ' || peek_at(p, 1) == '\n'))
    {
        /* Rewind to start of line for sequence parsing */
        p->pos -= indent;
        p->col -= indent;
        return parse_sequence(p, indent, out);
    }

    /* Check if this is a mapping (look for key: pattern) */
    size_t scan = p->pos;
    while (scan < p->len && p->src[scan] != '\n' && p->src[scan] != ':')
        scan++;
    if (scan < p->len && p->src[scan] == ':' &&
        (scan + 1 >= p->len || p->src[scan + 1] == ' ' || p->src[scan + 1] == '\n'))
    {
        /* Rewind to start of line for mapping parsing */
        p->pos -= indent;
        p->col -= indent;
        return parse_mapping(p, indent, out);
    }

    /* Plain scalar */
    return parse_plain_scalar(p, out);
}

/* ── Public API ──────────────────────────────────────────────────── */

vigil_status_t vigil_yaml_parse(const char *yaml, size_t length, const vigil_allocator_t *allocator,
                                vigil_json_value_t **out, vigil_error_t *error)
{
    if (!yaml || !out)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "yaml: invalid argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    yaml_parser_t p = {.src = yaml,
                       .len = length,
                       .pos = 0,
                       .line = 1,
                       .col = 1,
                       .alloc = allocator ? *allocator : vigil_default_allocator(),
                       .error = error};

    *out = NULL;
    return parse_value(&p, 0, out);
}
