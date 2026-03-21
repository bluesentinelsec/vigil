/* VIGIL regex engine - Thompson NFA implementation
 *
 * This implements RE2-style regex with linear time guarantees.
 * Based on Russ Cox's articles on regular expression matching.
 */
#include "regex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── NFA Node Types ─────────────────────────────────────────── */

typedef enum
{
    NFA_LITERAL,          /* Match single byte */
    NFA_ANY,              /* Match any byte (.) */
    NFA_CLASS,            /* Character class [abc] */
    NFA_CLASS_NEG,        /* Negated class [^abc] */
    NFA_SPLIT,            /* Branch: try out1, then out2 */
    NFA_JUMP,             /* Unconditional jump to out1 */
    NFA_SAVE,             /* Save position for capture group */
    NFA_MATCH,            /* Accept state */
    NFA_ANCHOR_START,     /* ^ anchor */
    NFA_ANCHOR_END,       /* $ anchor */
    NFA_WORD_BOUNDARY,    /* \b */
    NFA_NOT_WORD_BOUNDARY /* \B */
} nfa_type_t;

/* Character class bitmap (256 bits = 32 bytes) */
typedef struct
{
    uint8_t bits[32];
} char_class_t;

static void class_set(char_class_t *c, uint8_t ch)
{
    c->bits[ch >> 3] |= (1U << (ch & 7));
}

static bool class_test(const char_class_t *c, uint8_t ch)
{
    return (c->bits[ch >> 3] & (1U << (ch & 7))) != 0;
}

/* NFA state node */
typedef struct nfa_state
{
    nfa_type_t type;
    union {
        uint8_t literal;      /* NFA_LITERAL */
        char_class_t *cclass; /* NFA_CLASS, NFA_CLASS_NEG */
        size_t save_slot;     /* NFA_SAVE: slot number */
    } data;
    struct nfa_state *out1; /* Primary transition */
    struct nfa_state *out2; /* Secondary (for SPLIT) */
    size_t id;              /* State ID for simulation */
} nfa_state_t;

/* Compiled regex structure */
struct vigil_regex
{
    nfa_state_t *start;
    nfa_state_t *states; /* Array of all states */
    size_t state_count;
    size_t state_capacity;
    char_class_t *classes; /* Array of character classes */
    size_t class_count;
    size_t class_capacity;
    size_t group_count; /* Number of capture groups */
};

/* ── Parser State ───────────────────────────────────────────── */

typedef struct
{
    const char *pattern;
    size_t length;
    size_t pos;
    char *error_buf;
    size_t error_buf_size;
    vigil_regex_t *re;
    size_t group_count;
} parser_t;

/* Fragment: partial NFA with dangling arrows */
typedef struct
{
    nfa_state_t *start;
    nfa_state_t ***patch_list; /* Array of pointers to out fields to patch */
    size_t patch_count;
    size_t patch_capacity;
} fragment_t;

/* ── Memory Management ──────────────────────────────────────── */

static nfa_state_t *alloc_state(vigil_regex_t *re, nfa_type_t type)
{
    if (re->state_count >= re->state_capacity)
    {
        size_t new_cap = re->state_capacity == 0 ? 64 : re->state_capacity * 2;
        nfa_state_t *new_states = realloc(re->states, new_cap * sizeof(nfa_state_t));
        if (!new_states)
            return NULL;
        re->states = new_states;
        re->state_capacity = new_cap;
    }
    nfa_state_t *s = &re->states[re->state_count];
    memset(s, 0, sizeof(*s));
    s->type = type;
    s->id = re->state_count;
    re->state_count++;
    return s;
}

static char_class_t *alloc_class(vigil_regex_t *re)
{
    if (re->class_count >= re->class_capacity)
    {
        size_t new_cap = re->class_capacity == 0 ? 16 : re->class_capacity * 2;
        char_class_t *new_classes = realloc(re->classes, new_cap * sizeof(char_class_t));
        if (!new_classes)
            return NULL;
        re->classes = new_classes;
        re->class_capacity = new_cap;
    }
    char_class_t *c = &re->classes[re->class_count++];
    memset(c, 0, sizeof(*c));
    return c;
}

static void fragment_init(fragment_t *f)
{
    f->start = NULL;
    f->patch_list = NULL;
    f->patch_count = 0;
    f->patch_capacity = 0;
}

static void fragment_free(fragment_t *f)
{
    free(f->patch_list);
    f->patch_list = NULL;
    f->patch_count = 0;
    f->patch_capacity = 0;
}

static bool fragment_add_patch(fragment_t *f, nfa_state_t **ptr)
{
    if (f->patch_count >= f->patch_capacity)
    {
        size_t new_cap = f->patch_capacity == 0 ? 8 : f->patch_capacity * 2;
        nfa_state_t ***new_list = realloc(f->patch_list, new_cap * sizeof(nfa_state_t **));
        if (!new_list)
            return false;
        f->patch_list = new_list;
        f->patch_capacity = new_cap;
    }
    f->patch_list[f->patch_count++] = ptr;
    return true;
}

static void fragment_patch(fragment_t *f, nfa_state_t *target)
{
    for (size_t i = 0; i < f->patch_count; i++)
    {
        *f->patch_list[i] = target;
    }
}

static bool fragment_append(fragment_t *dst, fragment_t *src)
{
    for (size_t i = 0; i < src->patch_count; i++)
    {
        if (!fragment_add_patch(dst, src->patch_list[i]))
            return false;
    }
    return true;
}

/* ── Parser Helpers ─────────────────────────────────────────── */

static void parser_error(parser_t *p, const char *msg)
{
    if (p->error_buf && p->error_buf_size > 0)
    {
        snprintf(p->error_buf, p->error_buf_size, "%s at position %zu", msg, p->pos);
    }
}

static bool parser_eof(parser_t *p)
{
    return p->pos >= p->length;
}

static char parser_peek(parser_t *p)
{
    if (parser_eof(p))
        return '\0';
    return p->pattern[p->pos];
}

static char parser_advance(parser_t *p)
{
    if (parser_eof(p))
        return '\0';
    return p->pattern[p->pos++];
}

static bool parser_match(parser_t *p, char c)
{
    if (parser_peek(p) == c)
    {
        p->pos++;
        return true;
    }
    return false;
}

/* ── Character Class Parsing ────────────────────────────────── */

static void class_add_word(char_class_t *c)
{
    for (int i = 'a'; i <= 'z'; i++)
        class_set(c, (uint8_t)i);
    for (int i = 'A'; i <= 'Z'; i++)
        class_set(c, (uint8_t)i);
    for (int i = '0'; i <= '9'; i++)
        class_set(c, (uint8_t)i);
    class_set(c, '_');
}

static void class_add_digit(char_class_t *c)
{
    for (int i = '0'; i <= '9'; i++)
        class_set(c, (uint8_t)i);
}

static void class_add_space(char_class_t *c)
{
    class_set(c, ' ');
    class_set(c, '\t');
    class_set(c, '\n');
    class_set(c, '\r');
    class_set(c, '\f');
    class_set(c, '\v');
}

static bool parse_escape_into_class(parser_t *p, char_class_t *c)
{
    char ch = parser_advance(p);
    switch (ch)
    {
    case 'd':
        class_add_digit(c);
        return true;
    case 'D':
        for (int i = 0; i < 256; i++)
            class_set(c, (uint8_t)i);
        for (int i = '0'; i <= '9'; i++)
            c->bits[i >> 3] &= ~(1U << (i & 7));
        return true;
    case 'w':
        class_add_word(c);
        return true;
    case 'W':
        for (int i = 0; i < 256; i++)
            class_set(c, (uint8_t)i);
        for (int i = 'a'; i <= 'z'; i++)
            c->bits[i >> 3] &= ~(1U << (i & 7));
        for (int i = 'A'; i <= 'Z'; i++)
            c->bits[i >> 3] &= ~(1U << (i & 7));
        for (int i = '0'; i <= '9'; i++)
            c->bits[i >> 3] &= ~(1U << (i & 7));
        c->bits['_' >> 3] &= ~(1U << ('_' & 7));
        return true;
    case 's':
        class_add_space(c);
        return true;
    case 'S':
        for (int i = 0; i < 256; i++)
            class_set(c, (uint8_t)i);
        c->bits[' ' >> 3] &= ~(1U << (' ' & 7));
        c->bits['\t' >> 3] &= ~(1U << ('\t' & 7));
        c->bits['\n' >> 3] &= ~(1U << ('\n' & 7));
        c->bits['\r' >> 3] &= ~(1U << ('\r' & 7));
        c->bits['\f' >> 3] &= ~(1U << ('\f' & 7));
        c->bits['\v' >> 3] &= ~(1U << ('\v' & 7));
        return true;
    case 'n':
        class_set(c, '\n');
        return true;
    case 'r':
        class_set(c, '\r');
        return true;
    case 't':
        class_set(c, '\t');
        return true;
    case 'f':
        class_set(c, '\f');
        return true;
    case 'v':
        class_set(c, '\v');
        return true;
    case '\\':
    case '.':
    case '*':
    case '+':
    case '?':
    case '[':
    case ']':
    case '(':
    case ')':
    case '{':
    case '}':
    case '|':
    case '^':
    case '$':
    case '-':
        class_set(c, (uint8_t)ch);
        return true;
    default:
        parser_error(p, "invalid escape sequence");
        return false;
    }
}

static bool parse_char_class(parser_t *p, fragment_t *out)
{
    bool negated = parser_match(p, '^');
    char_class_t *c = alloc_class(p->re);
    if (!c)
    {
        parser_error(p, "out of memory");
        return false;
    }

    bool first = true;
    while (!parser_eof(p) && (first || parser_peek(p) != ']'))
    {
        first = false;
        char ch = parser_advance(p);
        if (ch == '\\')
        {
            if (!parse_escape_into_class(p, c))
                return false;
        }
        else if (parser_peek(p) == '-' && p->pos + 1 < p->length && p->pattern[p->pos + 1] != ']')
        {
            parser_advance(p); /* consume '-' */
            char end = parser_advance(p);
            if (end == '\\')
            {
                parser_error(p, "escape in range end not supported");
                return false;
            }
            for (int i = (uint8_t)ch; i <= (uint8_t)end; i++)
            {
                class_set(c, (uint8_t)i);
            }
        }
        else
        {
            class_set(c, (uint8_t)ch);
        }
    }

    if (!parser_match(p, ']'))
    {
        parser_error(p, "unclosed character class");
        return false;
    }

    nfa_state_t *s = alloc_state(p->re, negated ? NFA_CLASS_NEG : NFA_CLASS);
    if (!s)
    {
        parser_error(p, "out of memory");
        return false;
    }
    s->data.cclass = c;

    fragment_init(out);
    out->start = s;
    fragment_add_patch(out, &s->out1);
    return true;
}

/* ── Atom Parsing ───────────────────────────────────────────── */

static bool parse_escape(parser_t *p, fragment_t *out)
{
    char ch = parser_advance(p);
    nfa_state_t *s;

    switch (ch)
    {
    case 'd':
    case 'D':
    case 'w':
    case 'W':
    case 's':
    case 'S': {
        char_class_t *c = alloc_class(p->re);
        if (!c)
        {
            parser_error(p, "out of memory");
            return false;
        }
        p->pos--; /* back up to re-parse */
        if (!parse_escape_into_class(p, c))
            return false;
        s = alloc_state(p->re, NFA_CLASS);
        if (!s)
        {
            parser_error(p, "out of memory");
            return false;
        }
        s->data.cclass = c;
        break;
    }
    case 'b':
        s = alloc_state(p->re, NFA_WORD_BOUNDARY);
        if (!s)
        {
            parser_error(p, "out of memory");
            return false;
        }
        break;
    case 'B':
        s = alloc_state(p->re, NFA_NOT_WORD_BOUNDARY);
        if (!s)
        {
            parser_error(p, "out of memory");
            return false;
        }
        break;
    case 'n':
        ch = '\n';
        goto literal;
    case 'r':
        ch = '\r';
        goto literal;
    case 't':
        ch = '\t';
        goto literal;
    case 'f':
        ch = '\f';
        goto literal;
    case 'v':
        ch = '\v';
        goto literal;
    case '\\':
    case '.':
    case '*':
    case '+':
    case '?':
    case '[':
    case ']':
    case '(':
    case ')':
    case '{':
    case '}':
    case '|':
    case '^':
    case '$':
    literal:
        s = alloc_state(p->re, NFA_LITERAL);
        if (!s)
        {
            parser_error(p, "out of memory");
            return false;
        }
        s->data.literal = (uint8_t)ch;
        break;
    default:
        parser_error(p, "invalid escape sequence");
        return false;
    }

    fragment_init(out);
    out->start = s;
    fragment_add_patch(out, &s->out1);
    return true;
}

static bool parse_atom(parser_t *p, fragment_t *out);
static bool parse_alternation(parser_t *p, fragment_t *out);

static bool parse_group(parser_t *p, fragment_t *out)
{
    bool capturing = true;
    size_t group_num = 0;

    if (parser_match(p, '?'))
    {
        if (parser_match(p, ':'))
        {
            capturing = false;
        }
        else
        {
            parser_error(p, "invalid group modifier");
            return false;
        }
    }

    if (capturing)
    {
        if (p->group_count >= VIGIL_REGEX_MAX_GROUPS)
        {
            parser_error(p, "too many capture groups");
            return false;
        }
        group_num = p->group_count++;
    }

    fragment_t inner;
    if (!parse_alternation(p, &inner))
        return false;

    if (!parser_match(p, ')'))
    {
        fragment_free(&inner);
        parser_error(p, "unclosed group");
        return false;
    }

    if (capturing)
    {
        /* Wrap with SAVE states */
        nfa_state_t *save_start = alloc_state(p->re, NFA_SAVE);
        nfa_state_t *save_end = alloc_state(p->re, NFA_SAVE);
        if (!save_start || !save_end)
        {
            fragment_free(&inner);
            parser_error(p, "out of memory");
            return false;
        }
        save_start->data.save_slot = group_num * 2;
        save_end->data.save_slot = group_num * 2 + 1;

        save_start->out1 = inner.start;
        fragment_patch(&inner, save_end);

        fragment_init(out);
        out->start = save_start;
        fragment_add_patch(out, &save_end->out1);
        fragment_free(&inner);
    }
    else
    {
        *out = inner;
    }
    return true;
}

static bool parse_atom(parser_t *p, fragment_t *out)
{
    char ch = parser_peek(p);

    if (ch == '\\')
    {
        parser_advance(p);
        return parse_escape(p, out);
    }
    if (ch == '[')
    {
        parser_advance(p);
        return parse_char_class(p, out);
    }
    if (ch == '(')
    {
        parser_advance(p);
        return parse_group(p, out);
    }
    if (ch == '.')
    {
        parser_advance(p);
        nfa_state_t *s = alloc_state(p->re, NFA_ANY);
        if (!s)
        {
            parser_error(p, "out of memory");
            return false;
        }
        fragment_init(out);
        out->start = s;
        fragment_add_patch(out, &s->out1);
        return true;
    }
    if (ch == '^')
    {
        parser_advance(p);
        nfa_state_t *s = alloc_state(p->re, NFA_ANCHOR_START);
        if (!s)
        {
            parser_error(p, "out of memory");
            return false;
        }
        fragment_init(out);
        out->start = s;
        fragment_add_patch(out, &s->out1);
        return true;
    }
    if (ch == '$')
    {
        parser_advance(p);
        nfa_state_t *s = alloc_state(p->re, NFA_ANCHOR_END);
        if (!s)
        {
            parser_error(p, "out of memory");
            return false;
        }
        fragment_init(out);
        out->start = s;
        fragment_add_patch(out, &s->out1);
        return true;
    }

    /* Metacharacters that shouldn't appear as atoms */
    if (ch == '*' || ch == '+' || ch == '?' || ch == '|' || ch == ')' || ch == ']' || ch == '}' || ch == '\0')
    {
        return false; /* Not an atom */
    }

    /* Literal character */
    parser_advance(p);
    nfa_state_t *s = alloc_state(p->re, NFA_LITERAL);
    if (!s)
    {
        parser_error(p, "out of memory");
        return false;
    }
    s->data.literal = (uint8_t)ch;
    fragment_init(out);
    out->start = s;
    fragment_add_patch(out, &s->out1);
    return true;
}

/* ── Quantifier Parsing ─────────────────────────────────────── */

typedef struct
{
    size_t min;
    size_t max;
    bool has_max;
    bool greedy;
} quantifier_spec_t;

static bool build_empty_quantifier_fragment(parser_t *p, fragment_t *out)
{
    nfa_state_t *jump = alloc_state(p->re, NFA_JUMP);
    if (!jump)
    {
        parser_error(p, "out of memory");
        return false;
    }

    fragment_init(out);
    out->start = jump;
    fragment_add_patch(out, &jump->out1);
    return true;
}

static bool build_loop_quantifier(parser_t *p, fragment_t *atom, fragment_t *out, bool greedy, bool allow_empty)
{
    nfa_state_t *split = alloc_state(p->re, NFA_SPLIT);
    nfa_state_t **exit_patch;

    if (!split)
    {
        parser_error(p, "out of memory");
        return false;
    }

    if (greedy)
        split->out1 = atom->start;
    else
        split->out2 = atom->start;

    exit_patch = greedy ? &split->out2 : &split->out1;
    fragment_patch(atom, split);
    fragment_init(out);
    out->start = allow_empty ? split : atom->start;
    fragment_add_patch(out, exit_patch);
    fragment_free(atom);
    return true;
}

static bool build_optional_quantifier(parser_t *p, fragment_t *atom, fragment_t *out, bool greedy)
{
    nfa_state_t *split = alloc_state(p->re, NFA_SPLIT);
    nfa_state_t **skip_patch;

    if (!split)
    {
        parser_error(p, "out of memory");
        return false;
    }

    if (greedy)
        split->out1 = atom->start;
    else
        split->out2 = atom->start;

    skip_patch = greedy ? &split->out2 : &split->out1;
    fragment_init(out);
    out->start = split;
    fragment_add_patch(out, skip_patch);
    fragment_append(out, atom);
    fragment_free(atom);
    return true;
}

static bool parse_brace_quantifier_spec(parser_t *p, quantifier_spec_t *spec)
{
    spec->min = 0U;
    spec->max = 0U;
    spec->has_max = false;
    spec->greedy = true;

    while (isdigit(parser_peek(p)))
        spec->min = spec->min * 10U + (size_t)(parser_advance(p) - '0');

    if (parser_match(p, ','))
    {
        if (isdigit(parser_peek(p)))
        {
            spec->has_max = true;
            while (isdigit(parser_peek(p)))
                spec->max = spec->max * 10U + (size_t)(parser_advance(p) - '0');
        }
    }
    else
    {
        spec->has_max = true;
        spec->max = spec->min;
    }

    if (!parser_match(p, '}'))
    {
        parser_error(p, "invalid quantifier");
        return false;
    }

    if (parser_match(p, '?'))
        spec->greedy = false;
    return true;
}

static bool validate_brace_quantifier_spec(parser_t *p, const quantifier_spec_t *spec)
{
    if (spec->min > 100U || (spec->has_max && spec->max > 100U))
    {
        parser_error(p, "quantifier too large");
        return false;
    }
    if (spec->has_max && spec->max > 10U)
    {
        parser_error(p, "bounded quantifier max > 10 not yet supported");
        return false;
    }
    return true;
}

static bool build_exact_brace_quantifier(parser_t *p, fragment_t *atom, fragment_t *out, size_t count)
{
    if (count == 0U)
    {
        fragment_free(atom);
        return build_empty_quantifier_fragment(p, out);
    }
    if (count == 1U)
    {
        *out = *atom;
        return true;
    }

    parser_error(p, "exact repetition > 1 not yet supported");
    return false;
}

static bool build_unbounded_brace_quantifier(parser_t *p, fragment_t *atom, fragment_t *out,
                                             const quantifier_spec_t *spec)
{
    if (spec->min == 0U)
        return build_loop_quantifier(p, atom, out, spec->greedy, true);
    if (spec->min == 1U)
        return build_loop_quantifier(p, atom, out, spec->greedy, false);

    parser_error(p, "{n,} with n > 1 not yet supported");
    return false;
}

static bool build_brace_quantifier(parser_t *p, fragment_t *atom, fragment_t *out)
{
    quantifier_spec_t spec;

    if (!parse_brace_quantifier_spec(p, &spec))
        return false;
    if (!validate_brace_quantifier_spec(p, &spec))
        return false;
    if (spec.has_max && spec.min == spec.max)
        return build_exact_brace_quantifier(p, atom, out, spec.min);
    if (!spec.has_max)
        return build_unbounded_brace_quantifier(p, atom, out, &spec);

    parser_error(p, "complex bounded quantifiers not yet supported");
    return false;
}

static bool parse_quantifier(parser_t *p, fragment_t *atom, fragment_t *out)
{
    char ch = parser_peek(p);
    bool greedy;

    if (ch == '*')
    {
        parser_advance(p);
        greedy = !parser_match(p, '?');
        return build_loop_quantifier(p, atom, out, greedy, true);
    }

    if (ch == '+')
    {
        parser_advance(p);
        greedy = !parser_match(p, '?');
        return build_loop_quantifier(p, atom, out, greedy, false);
    }

    if (ch == '?')
    {
        parser_advance(p);
        greedy = !parser_match(p, '?');
        return build_optional_quantifier(p, atom, out, greedy);
    }

    if (ch == '{')
    {
        parser_advance(p);
        return build_brace_quantifier(p, atom, out);
    }

    /* No quantifier - return atom as-is */
    *out = *atom;
    return true;
}

/* ── Expression Parsing ─────────────────────────────────────── */

static bool parse_concatenation(parser_t *p, fragment_t *out)
{
    fragment_t result;
    fragment_init(&result);

    while (!parser_eof(p))
    {
        char ch = parser_peek(p);
        if (ch == '|' || ch == ')')
            break;

        fragment_t atom;
        if (!parse_atom(p, &atom))
        {
            if (result.start == NULL)
            {
                /* Empty concatenation - create jump state */
                nfa_state_t *jump = alloc_state(p->re, NFA_JUMP);
                if (!jump)
                {
                    parser_error(p, "out of memory");
                    return false;
                }
                fragment_init(out);
                out->start = jump;
                fragment_add_patch(out, &jump->out1);
                return true;
            }
            break;
        }

        fragment_t quantified;
        if (!parse_quantifier(p, &atom, &quantified))
        {
            fragment_free(&atom);
            fragment_free(&result);
            return false;
        }

        if (result.start == NULL)
        {
            result = quantified;
        }
        else
        {
            fragment_patch(&result, quantified.start);
            fragment_free(&result);
            result.start = result.start; /* keep start */
            result.patch_list = quantified.patch_list;
            result.patch_count = quantified.patch_count;
            result.patch_capacity = quantified.patch_capacity;
            /* Transfer ownership of patch list */
            quantified.patch_list = NULL;
        }
    }

    if (result.start == NULL)
    {
        /* Empty - create jump state */
        nfa_state_t *jump = alloc_state(p->re, NFA_JUMP);
        if (!jump)
        {
            parser_error(p, "out of memory");
            return false;
        }
        fragment_init(out);
        out->start = jump;
        fragment_add_patch(out, &jump->out1);
        return true;
    }

    *out = result;
    return true;
}

static bool parse_alternation(parser_t *p, fragment_t *out)
{
    fragment_t left;
    if (!parse_concatenation(p, &left))
        return false;

    while (parser_match(p, '|'))
    {
        fragment_t right;
        if (!parse_concatenation(p, &right))
        {
            fragment_free(&left);
            return false;
        }

        nfa_state_t *split = alloc_state(p->re, NFA_SPLIT);
        if (!split)
        {
            fragment_free(&left);
            fragment_free(&right);
            parser_error(p, "out of memory");
            return false;
        }

        split->out1 = left.start;
        split->out2 = right.start;

        fragment_t combined;
        fragment_init(&combined);
        combined.start = split;
        fragment_append(&combined, &left);
        fragment_append(&combined, &right);
        fragment_free(&left);
        fragment_free(&right);
        left = combined;
    }

    *out = left;
    return true;
}

/* ── NFA Simulation ─────────────────────────────────────────── */

typedef struct
{
    nfa_state_t **states;
    size_t count;
    size_t capacity;
    size_t *saves; /* Capture group positions */
    size_t save_count;
} state_list_t;

static bool state_list_init(state_list_t *l, size_t cap, size_t save_slots)
{
    l->states = malloc(cap * sizeof(nfa_state_t *));
    l->saves = calloc(cap * save_slots, sizeof(size_t));
    if (!l->states || !l->saves)
    {
        free(l->states);
        free(l->saves);
        return false;
    }
    l->count = 0;
    l->capacity = cap;
    l->save_count = save_slots;
    return true;
}

static void state_list_free(state_list_t *l)
{
    free(l->states);
    free(l->saves);
}

static void state_list_clear(state_list_t *l)
{
    l->count = 0;
}

/* Add state to list with epsilon closure */
static void add_state(state_list_t *l, nfa_state_t *s, const size_t *saves, size_t pos, const char *input,
                      size_t input_len, uint8_t *visited, size_t gen)
{
    if (s == NULL)
        return;
    if (visited[s->id] == gen)
        return;
    visited[s->id] = (uint8_t)gen;

    /* Handle epsilon transitions */
    switch (s->type)
    {
    case NFA_SPLIT:
        add_state(l, s->out1, saves, pos, input, input_len, visited, gen);
        add_state(l, s->out2, saves, pos, input, input_len, visited, gen);
        return;
    case NFA_JUMP:
        add_state(l, s->out1, saves, pos, input, input_len, visited, gen);
        return;
    case NFA_SAVE: {
        size_t scratch[VIGIL_REGEX_MAX_GROUPS * 2];
        memcpy(scratch, saves, l->save_count * sizeof(size_t));
        scratch[s->data.save_slot] = pos;
        add_state(l, s->out1, scratch, pos, input, input_len, visited, gen);
        return;
    }
    case NFA_ANCHOR_START:
        if (pos == 0)
        {
            add_state(l, s->out1, saves, pos, input, input_len, visited, gen);
        }
        return;
    case NFA_ANCHOR_END:
        if (pos == input_len)
        {
            add_state(l, s->out1, saves, pos, input, input_len, visited, gen);
        }
        return;
    case NFA_WORD_BOUNDARY: {
        bool before_word = (pos > 0) && (isalnum((unsigned char)input[pos - 1]) || input[pos - 1] == '_');
        bool after_word = (pos < input_len) && (isalnum((unsigned char)input[pos]) || input[pos] == '_');
        if (before_word != after_word)
        {
            add_state(l, s->out1, saves, pos, input, input_len, visited, gen);
        }
        return;
    }
    case NFA_NOT_WORD_BOUNDARY: {
        bool before_word = (pos > 0) && (isalnum((unsigned char)input[pos - 1]) || input[pos - 1] == '_');
        bool after_word = (pos < input_len) && (isalnum((unsigned char)input[pos]) || input[pos] == '_');
        if (before_word == after_word)
        {
            add_state(l, s->out1, saves, pos, input, input_len, visited, gen);
        }
        return;
    }
    default:
        break;
    }

    /* Add to list */
    if (l->count < l->capacity)
    {
        l->states[l->count] = s;
        memcpy(&l->saves[l->count * l->save_count], saves, l->save_count * sizeof(size_t));
        l->count++;
    }
}

static bool step(state_list_t *curr, state_list_t *next, char ch, size_t pos, const char *input, size_t input_len,
                 uint8_t *visited, size_t gen)
{
    state_list_clear(next);

    for (size_t i = 0; i < curr->count; i++)
    {
        nfa_state_t *s = curr->states[i];
        const size_t *saves = &curr->saves[i * curr->save_count];
        bool match = false;

        switch (s->type)
        {
        case NFA_LITERAL:
            match = (s->data.literal == (uint8_t)ch);
            break;
        case NFA_ANY:
            match = (ch != '\n'); /* . doesn't match newline by default */
            break;
        case NFA_CLASS:
            match = class_test(s->data.cclass, (uint8_t)ch);
            break;
        case NFA_CLASS_NEG:
            match = !class_test(s->data.cclass, (uint8_t)ch);
            break;
        default:
            break;
        }

        if (match)
        {
            add_state(next, s->out1, saves, pos + 1, input, input_len, visited, gen);
        }
    }

    return next->count > 0;
}

static bool check_match(state_list_t *l, vigil_regex_result_t *result, size_t group_count)
{
    for (size_t i = 0; i < l->count; i++)
    {
        if (l->states[i]->type == NFA_MATCH)
        {
            if (result)
            {
                result->matched = true;
                result->group_count = group_count;
                const size_t *saves = &l->saves[i * l->save_count];
                for (size_t g = 0; g < group_count && g < VIGIL_REGEX_MAX_GROUPS; g++)
                {
                    result->groups[g].start = saves[g * 2];
                    result->groups[g].end = saves[g * 2 + 1];
                }
            }
            return true;
        }
    }
    return false;
}

/* ── Public API ─────────────────────────────────────────────── */

vigil_regex_t *vigil_regex_compile(const char *pattern, size_t pattern_len, char *error_buf, size_t error_buf_size)
{
    vigil_regex_t *re = calloc(1, sizeof(vigil_regex_t));
    if (!re)
    {
        if (error_buf)
            snprintf(error_buf, error_buf_size, "out of memory");
        return NULL;
    }

    parser_t p = {
        .pattern = pattern,
        .length = pattern_len,
        .pos = 0,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .re = re,
        .group_count = 1 /* Group 0 is the whole match */
    };

    fragment_t frag;
    if (!parse_alternation(&p, &frag))
    {
        vigil_regex_free(re);
        return NULL;
    }

    if (!parser_eof(&p))
    {
        parser_error(&p, "unexpected character");
        fragment_free(&frag);
        vigil_regex_free(re);
        return NULL;
    }

    /* Add match state */
    nfa_state_t *match = alloc_state(re, NFA_MATCH);
    if (!match)
    {
        fragment_free(&frag);
        vigil_regex_free(re);
        if (error_buf)
            snprintf(error_buf, error_buf_size, "out of memory");
        return NULL;
    }
    fragment_patch(&frag, match);

    /* Wrap entire pattern in group 0 saves */
    nfa_state_t *save_start = alloc_state(re, NFA_SAVE);
    nfa_state_t *save_end = alloc_state(re, NFA_SAVE);
    if (!save_start || !save_end)
    {
        fragment_free(&frag);
        vigil_regex_free(re);
        if (error_buf)
            snprintf(error_buf, error_buf_size, "out of memory");
        return NULL;
    }
    save_start->data.save_slot = 0;
    save_end->data.save_slot = 1;
    save_start->out1 = frag.start;

    /* Insert save_end before match */
    for (size_t i = 0; i < re->state_count; i++)
    {
        if (re->states[i].out1 == match)
        {
            re->states[i].out1 = save_end;
        }
        if (re->states[i].out2 == match)
        {
            re->states[i].out2 = save_end;
        }
    }
    save_end->out1 = match;

    re->start = save_start;
    re->group_count = p.group_count;
    fragment_free(&frag);
    return re;
}

void vigil_regex_free(vigil_regex_t *re)
{
    if (!re)
        return;
    free(re->states);
    free(re->classes);
    free(re);
}

bool vigil_regex_match(const vigil_regex_t *re, const char *input, size_t input_len, vigil_regex_result_t *result)
{
    if (!re || !re->start)
        return false;

    size_t save_slots = re->group_count * 2;
    state_list_t curr, next;
    if (!state_list_init(&curr, re->state_count + 1, save_slots))
        return false;
    if (!state_list_init(&next, re->state_count + 1, save_slots))
    {
        state_list_free(&curr);
        return false;
    }

    uint8_t *visited = calloc(re->state_count + 1, 1);
    if (!visited)
    {
        state_list_free(&curr);
        state_list_free(&next);
        return false;
    }

    size_t *init_saves = calloc(save_slots, sizeof(size_t));
    if (!init_saves)
    {
        free(visited);
        state_list_free(&curr);
        state_list_free(&next);
        return false;
    }
    for (size_t i = 0; i < save_slots; i++)
        init_saves[i] = SIZE_MAX;

    size_t gen = 1;
    add_state(&curr, re->start, init_saves, 0, input, input_len, visited, gen);

    for (size_t i = 0; i < input_len; i++)
    {
        gen++;
        memset(visited, 0, re->state_count + 1);
        step(&curr, &next, input[i], i, input, input_len, visited, gen);
        state_list_t tmp = curr;
        curr = next;
        next = tmp;
    }

    bool matched = check_match(&curr, result, re->group_count);

    free(init_saves);
    free(visited);
    state_list_free(&curr);
    state_list_free(&next);
    return matched;
}

bool vigil_regex_find(const vigil_regex_t *re, const char *input, size_t input_len, vigil_regex_result_t *result)
{
    if (!re || !re->start)
        return false;

    size_t save_slots = re->group_count * 2;
    size_t state_cap = re->state_count + 1;
    state_list_t curr, next;
    uint8_t *visited;
    size_t *init_saves;
    bool found = false;

    if (!state_list_init(&curr, state_cap, save_slots))
        return false;
    if (!state_list_init(&next, state_cap, save_slots))
    { state_list_free(&curr); return false; }
    visited = calloc(state_cap, 1);
    init_saves = malloc(save_slots * sizeof(size_t));
    if (!visited || !init_saves)
    { free(visited); free(init_saves); state_list_free(&curr); state_list_free(&next); return false; }

    /* Try matching at each position */
    for (size_t start = 0; start <= input_len; start++)
    {
        for (size_t i = 0; i < save_slots; i++)
            init_saves[i] = SIZE_MAX;

        state_list_clear(&curr);
        state_list_clear(&next);
        memset(visited, 0, state_cap);

        size_t gen = 1;
        add_state(&curr, re->start, init_saves, start, input, input_len, visited, gen);

        /* Track best (longest) match at this start position */
        vigil_regex_result_t best;
        best.matched = false;

        /* Check for immediate match (empty pattern) */
        if (check_match(&curr, &best, re->group_count))
        {
            /* Continue to find longer match */
        }

        for (size_t i = start; i < input_len; i++)
        {
            gen++;
            memset(visited, 0, state_cap);
            step(&curr, &next, input[i], i, input, input_len, visited, gen);
            state_list_t tmp = curr;
            curr = next;
            next = tmp;

            vigil_regex_result_t candidate;
            if (check_match(&curr, &candidate, re->group_count))
            {
                best = candidate;
                /* Continue to find longer match (greedy) */
            }
        }

        if (best.matched)
        {
            if (result)
                *result = best;
            found = true;
            break;
        }
    }

    free(init_saves);
    free(visited);
    state_list_free(&curr);
    state_list_free(&next);
    return found;
}

size_t vigil_regex_find_all(const vigil_regex_t *re, const char *input, size_t input_len, vigil_regex_result_t *results,
                            size_t max_results)
{
    if (!re || !results || max_results == 0)
        return 0;

    size_t count = 0;
    size_t pos = 0;

    while (pos <= input_len && count < max_results)
    {
        vigil_regex_result_t r;
        if (vigil_regex_find(re, input + pos, input_len - pos, &r))
        {
            /* Adjust offsets to be relative to original input */
            for (size_t g = 0; g < r.group_count; g++)
            {
                if (r.groups[g].start != SIZE_MAX)
                {
                    r.groups[g].start += pos;
                    r.groups[g].end += pos;
                }
            }
            results[count++] = r;

            /* Move past this match */
            size_t match_end = r.groups[0].end;
            if (match_end == pos + r.groups[0].start)
            {
                /* Empty match - advance by one to avoid infinite loop */
                pos = match_end + 1;
            }
            else
            {
                pos = match_end;
            }
        }
        else
        {
            break;
        }
    }

    return count;
}

vigil_status_t vigil_regex_replace(const vigil_regex_t *re, const char *input, size_t input_len,
                                   const char *replacement, size_t replacement_len, char **output, size_t *output_len)
{
    vigil_regex_result_t r;
    if (!vigil_regex_find(re, input, input_len, &r))
    {
        /* No match - return copy of input */
        *output = malloc(input_len + 1);
        if (!*output)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        memcpy(*output, input, input_len);
        (*output)[input_len] = '\0';
        *output_len = input_len;
        return VIGIL_STATUS_OK;
    }

    size_t match_start = r.groups[0].start;
    size_t match_end = r.groups[0].end;
    size_t new_len = match_start + replacement_len + (input_len - match_end);

    *output = malloc(new_len + 1);
    if (!*output)
        return VIGIL_STATUS_OUT_OF_MEMORY;

    memcpy(*output, input, match_start);
    memcpy(*output + match_start, replacement, replacement_len);
    memcpy(*output + match_start + replacement_len, input + match_end, input_len - match_end);
    (*output)[new_len] = '\0';
    *output_len = new_len;

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_regex_replace_all(const vigil_regex_t *re, const char *input, size_t input_len,
                                       const char *replacement, size_t replacement_len, char **output,
                                       size_t *output_len)
{
    /* Find all matches first */
    vigil_regex_result_t results[256];
    size_t match_count = vigil_regex_find_all(re, input, input_len, results, 256);

    if (match_count == 0)
    {
        *output = malloc(input_len + 1);
        if (!*output)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        memcpy(*output, input, input_len);
        (*output)[input_len] = '\0';
        *output_len = input_len;
        return VIGIL_STATUS_OK;
    }

    /* Calculate new length */
    size_t new_len = input_len;
    for (size_t i = 0; i < match_count; i++)
    {
        size_t match_len = results[i].groups[0].end - results[i].groups[0].start;
        new_len = new_len - match_len + replacement_len;
    }

    *output = malloc(new_len + 1);
    if (!*output)
        return VIGIL_STATUS_OUT_OF_MEMORY;

    size_t out_pos = 0;
    size_t in_pos = 0;
    for (size_t i = 0; i < match_count; i++)
    {
        size_t match_start = results[i].groups[0].start;
        size_t match_end = results[i].groups[0].end;

        /* Copy text before match */
        memcpy(*output + out_pos, input + in_pos, match_start - in_pos);
        out_pos += match_start - in_pos;

        /* Copy replacement */
        memcpy(*output + out_pos, replacement, replacement_len);
        out_pos += replacement_len;

        in_pos = match_end;
    }

    /* Copy remaining text */
    memcpy(*output + out_pos, input + in_pos, input_len - in_pos);
    out_pos += input_len - in_pos;
    (*output)[out_pos] = '\0';
    *output_len = out_pos;

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_regex_split(const vigil_regex_t *re, const char *input, size_t input_len, char ***parts,
                                 size_t **part_lens, size_t *part_count)
{
    /* Find all matches */
    vigil_regex_result_t results[256];
    size_t match_count = vigil_regex_find_all(re, input, input_len, results, 256);

    *part_count = match_count + 1;
    *parts = malloc(*part_count * sizeof(char *));
    *part_lens = malloc(*part_count * sizeof(size_t));
    if (!*parts || !*part_lens)
    {
        free(*parts);
        free(*part_lens);
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    size_t pos = 0;
    for (size_t i = 0; i < match_count; i++)
    {
        size_t match_start = results[i].groups[0].start;
        size_t len = match_start - pos;

        (*parts)[i] = malloc(len + 1);
        if (!(*parts)[i])
        {
            for (size_t j = 0; j < i; j++)
                free((*parts)[j]);
            free(*parts);
            free(*part_lens);
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        memcpy((*parts)[i], input + pos, len);
        (*parts)[i][len] = '\0';
        (*part_lens)[i] = len;

        pos = results[i].groups[0].end;
    }

    /* Last part */
    size_t len = input_len - pos;
    (*parts)[match_count] = malloc(len + 1);
    if (!(*parts)[match_count])
    {
        for (size_t j = 0; j < match_count; j++)
            free((*parts)[j]);
        free(*parts);
        free(*part_lens);
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memcpy((*parts)[match_count], input + pos, len);
    (*parts)[match_count][len] = '\0';
    (*part_lens)[match_count] = len;

    return VIGIL_STATUS_OK;
}
