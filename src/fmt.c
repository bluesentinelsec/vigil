/*
 * vigil_fmt — token-stream source formatter.
 *
 * Walks the token list and the raw source text to produce canonically
 * formatted output.  Comments are preserved by scanning the gaps between
 * consecutive token spans in the original source.
 *
 * Indentation: 4 spaces per brace-depth level.
 * All control-flow bodies are multi-line (no single-line ifs).
 * Imports are sorted alphabetically and grouped before other decls.
 */

#include "vigil/fmt.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/token.h"

/* ── dynamic buffer ──────────────────────────────────────────────── */

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
} buf_t;

static void buf_init(buf_t *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_grow(buf_t *b, size_t need)
{
    if (b->len + need <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + need)
        cap *= 2;
    b->data = realloc(b->data, cap);
    b->cap = cap;
}

static void buf_push(buf_t *b, char c)
{
    buf_grow(b, 1);
    b->data[b->len++] = c;
}

static void buf_write(buf_t *b, const char *s, size_t n)
{
    buf_grow(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void buf_puts(buf_t *b, const char *s)
{
    buf_write(b, s, strlen(s));
}

/* ── formatter state ─────────────────────────────────────────────── */

typedef struct
{
    const char *src;
    size_t src_len;
    const vigil_token_list_t *tokens;
    size_t count;
    buf_t out;
    int indent;
    bool at_line_start; /* true if we haven't written non-ws on this line */
} fmt_state_t;

static void emit_indent(fmt_state_t *f)
{
    for (int i = 0; i < f->indent; i++)
    {
        buf_puts(&f->out, "    ");
    }
    f->at_line_start = false;
}

static void emit_newline(fmt_state_t *f)
{
    buf_push(&f->out, '\n');
    f->at_line_start = true;
}

static void emit_space(fmt_state_t *f)
{
    buf_push(&f->out, ' ');
}

static void emit_str(fmt_state_t *f, const char *s, size_t n)
{
    if (f->at_line_start && n > 0)
    {
        emit_indent(f);
    }
    buf_write(&f->out, s, n);
    f->at_line_start = false;
}

static void emit_cstr(fmt_state_t *f, const char *s)
{
    emit_str(f, s, strlen(s));
}

/* Get the source text for a token. */
static const char *tok_text(const fmt_state_t *f, const vigil_token_t *t)
{
    return f->src + t->span.start_offset;
}

static size_t tok_len(const vigil_token_t *t)
{
    return t->span.end_offset - t->span.start_offset;
}

/* ── comment extraction ──────────────────────────────────────────── */

/*
 * Scan source text in [start, end) for line comments (// ...) and
 * block comments.  Emit each one indented on its own line.
 * Returns true if any comment was emitted.
 */
static bool emit_comments_between(fmt_state_t *f, size_t start, size_t end)
{
    bool emitted = false;
    size_t i = start;
    while (i < end)
    {
        if (i + 1 < end && f->src[i] == '/' && f->src[i + 1] == '/')
        {
            /* Line comment — find end of line. */
            size_t cstart = i;
            while (i < end && f->src[i] != '\n')
                i++;
            if (f->at_line_start)
                emit_indent(f);
            else
            {
                emit_newline(f);
                emit_indent(f);
            }
            buf_write(&f->out, f->src + cstart, i - cstart);
            emit_newline(f);
            emitted = true;
        }
        else if (i + 1 < end && f->src[i] == '/' && f->src[i + 1] == '*')
        {
            /* Block comment — find closing */
            size_t cstart = i;
            i += 2;
            while (i + 1 < end && !(f->src[i] == '*' && f->src[i + 1] == '/'))
                i++;
            if (i + 1 < end)
                i += 2;
            if (f->at_line_start)
                emit_indent(f);
            else
            {
                emit_newline(f);
                emit_indent(f);
            }
            buf_write(&f->out, f->src + cstart, i - cstart);
            emit_newline(f);
            emitted = true;
        }
        else
        {
            i++;
        }
    }
    return emitted;
}

/* ── token classification helpers ────────────────────────────────── */

/* ── token classification helpers (bitmap tables) ────────────────── */

/* Token kind values are dense integers 0–72; a two-element uint64_t
   bitmap gives O(1) classification with no branching. */

#define TOK_BIT(k) (UINT64_C(1) << ((k) & 63))
#define TOK_IDX(k) ((unsigned)(k) >> 6)
#define TOK_SET(tbl, k)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        (tbl)[TOK_IDX(k)] |= TOK_BIT(k);                                                                             \
    } while (0)
#define TOK_TEST(tbl, k) (((tbl)[TOK_IDX(k)] & TOK_BIT(k)) != 0)

// clang-format off
static const uint64_t kBinaryOpBits[2] = {
    TOK_BIT(VIGIL_TOKEN_PLUS) | TOK_BIT(VIGIL_TOKEN_MINUS) | TOK_BIT(VIGIL_TOKEN_STAR) |
    TOK_BIT(VIGIL_TOKEN_SLASH) | TOK_BIT(VIGIL_TOKEN_PERCENT) | TOK_BIT(VIGIL_TOKEN_EQUAL_EQUAL) |
    TOK_BIT(VIGIL_TOKEN_BANG_EQUAL) | TOK_BIT(VIGIL_TOKEN_LESS) | TOK_BIT(VIGIL_TOKEN_LESS_EQUAL) |
    TOK_BIT(VIGIL_TOKEN_GREATER) | TOK_BIT(VIGIL_TOKEN_GREATER_EQUAL) | TOK_BIT(VIGIL_TOKEN_AMPERSAND_AMPERSAND) |
    TOK_BIT(VIGIL_TOKEN_AMPERSAND) ,
    TOK_BIT(VIGIL_TOKEN_PIPE_PIPE) | TOK_BIT(VIGIL_TOKEN_PIPE) | TOK_BIT(VIGIL_TOKEN_CARET) |
    TOK_BIT(VIGIL_TOKEN_SHIFT_LEFT) | TOK_BIT(VIGIL_TOKEN_SHIFT_RIGHT)
};

static const uint64_t kAssignOpBits[2] = {
    TOK_BIT(VIGIL_TOKEN_ASSIGN) | TOK_BIT(VIGIL_TOKEN_PLUS_ASSIGN) | TOK_BIT(VIGIL_TOKEN_MINUS_ASSIGN) |
    TOK_BIT(VIGIL_TOKEN_STAR_ASSIGN) | TOK_BIT(VIGIL_TOKEN_SLASH_ASSIGN) | TOK_BIT(VIGIL_TOKEN_PERCENT_ASSIGN) ,
    0
};

static const uint64_t kKeywordBits[2] = {
    TOK_BIT(VIGIL_TOKEN_IMPORT) | TOK_BIT(VIGIL_TOKEN_AS) | TOK_BIT(VIGIL_TOKEN_PUB) |
    TOK_BIT(VIGIL_TOKEN_FN) | TOK_BIT(VIGIL_TOKEN_CLASS) | TOK_BIT(VIGIL_TOKEN_INTERFACE) |
    TOK_BIT(VIGIL_TOKEN_ENUM) | TOK_BIT(VIGIL_TOKEN_CONST) | TOK_BIT(VIGIL_TOKEN_RETURN) |
    TOK_BIT(VIGIL_TOKEN_DEFER) | TOK_BIT(VIGIL_TOKEN_IF) | TOK_BIT(VIGIL_TOKEN_ELSE) |
    TOK_BIT(VIGIL_TOKEN_FOR) | TOK_BIT(VIGIL_TOKEN_WHILE) | TOK_BIT(VIGIL_TOKEN_SWITCH) |
    TOK_BIT(VIGIL_TOKEN_GUARD) | TOK_BIT(VIGIL_TOKEN_CASE) | TOK_BIT(VIGIL_TOKEN_DEFAULT) |
    TOK_BIT(VIGIL_TOKEN_BREAK) | TOK_BIT(VIGIL_TOKEN_CONTINUE) | TOK_BIT(VIGIL_TOKEN_IN) |
    TOK_BIT(VIGIL_TOKEN_NIL) | TOK_BIT(VIGIL_TOKEN_TRUE) | TOK_BIT(VIGIL_TOKEN_FALSE) ,
    0
};
// clang-format on

static bool is_binary_op(vigil_token_kind_t k) { return TOK_TEST(kBinaryOpBits, k); }
static bool is_assign_op(vigil_token_kind_t k) { return TOK_TEST(kAssignOpBits, k); }
static bool is_keyword(vigil_token_kind_t k) { return TOK_TEST(kKeywordBits, k); }

/* ── import sorting ──────────────────────────────────────────────── */

typedef struct
{
    size_t start_idx; /* index of VIGIL_TOKEN_IMPORT */
    size_t end_idx;   /* index after the semicolon */
    const char *path; /* pointer into source text (inside quotes) */
    size_t path_len;
} import_info_t;

static int import_cmp(const void *a, const void *b)
{
    const import_info_t *ia = a;
    const import_info_t *ib = b;
    size_t min = ia->path_len < ib->path_len ? ia->path_len : ib->path_len;
    int c = memcmp(ia->path, ib->path, min);
    if (c != 0)
        return c;
    return (ia->path_len > ib->path_len) - (ia->path_len < ib->path_len);
}

/* ── main formatting logic ───────────────────────────────────────── */

/*
 * Determine whether a space is needed before the current token, given
 * the previous token.  This encodes the canonical VIGIL spacing rules.
 */
// clang-format off
static const uint64_t kOpenGroupBits[2] = {
    TOK_BIT(VIGIL_TOKEN_LPAREN) | TOK_BIT(VIGIL_TOKEN_LBRACKET) | TOK_BIT(VIGIL_TOKEN_LBRACE), 0
};
static const uint64_t kNoSpaceBeforeBits[2] = {
    TOK_BIT(VIGIL_TOKEN_RPAREN) | TOK_BIT(VIGIL_TOKEN_RBRACKET) | TOK_BIT(VIGIL_TOKEN_RBRACE) |
    TOK_BIT(VIGIL_TOKEN_COMMA) | TOK_BIT(VIGIL_TOKEN_SEMICOLON) | TOK_BIT(VIGIL_TOKEN_DOT) |
    TOK_BIT(VIGIL_TOKEN_COLON) | TOK_BIT(VIGIL_TOKEN_PLUS_PLUS) | TOK_BIT(VIGIL_TOKEN_MINUS_MINUS), 0
};
static const uint64_t kCallPrevBits[2] = {
    TOK_BIT(VIGIL_TOKEN_IDENTIFIER) | TOK_BIT(VIGIL_TOKEN_RPAREN) | TOK_BIT(VIGIL_TOKEN_RBRACKET), 0
};
// clang-format on

static bool need_space_before(const vigil_token_t *prev, const vigil_token_t *cur, const fmt_state_t *f)
{
    vigil_token_kind_t pk = prev->kind;
    vigil_token_kind_t ck = cur->kind;

    (void)f;

    if (TOK_TEST(kOpenGroupBits, pk) || TOK_TEST(kNoSpaceBeforeBits, ck))
        return false;
    if (pk == VIGIL_TOKEN_DOT)
        return false;
    if ((ck == VIGIL_TOKEN_LPAREN || ck == VIGIL_TOKEN_LBRACKET) && TOK_TEST(kCallPrevBits, pk))
        return false;
    if (pk == VIGIL_TOKEN_COMMA)
        return true;
    if (is_binary_op(ck) || is_assign_op(ck) || is_binary_op(pk) || is_assign_op(pk))
        return true;
    if (ck == VIGIL_TOKEN_QUESTION || pk == VIGIL_TOKEN_QUESTION || pk == VIGIL_TOKEN_COLON)
        return true;
    if (ck == VIGIL_TOKEN_ARROW || pk == VIGIL_TOKEN_ARROW)
        return true;
    if (is_keyword(pk) || ck == VIGIL_TOKEN_LBRACE)
        return true;
    if ((pk == VIGIL_TOKEN_IDENTIFIER || pk == VIGIL_TOKEN_GREATER) && ck == VIGIL_TOKEN_IDENTIFIER)
        return true;
    if (pk == VIGIL_TOKEN_LBRACE)
        return false;
    if (ck == VIGIL_TOKEN_BANG || ck == VIGIL_TOKEN_TILDE)
        return pk == VIGIL_TOKEN_IDENTIFIER || pk == VIGIL_TOKEN_INT_LITERAL || pk == VIGIL_TOKEN_RPAREN;
    if (ck == VIGIL_TOKEN_MINUS &&
        (pk == VIGIL_TOKEN_LPAREN || pk == VIGIL_TOKEN_COMMA || pk == VIGIL_TOKEN_RETURN ||
         is_assign_op(pk) || is_binary_op(pk)))
        return false;
    return true;
}

/*
 * Should we emit a newline before this token?
 * Returns: 0 = no newline, 1 = newline, 2 = blank line + newline.
 */
static int need_newline_before(const vigil_token_t *prev, const vigil_token_t *cur, const fmt_state_t *f)
{
    vigil_token_kind_t pk = prev->kind;
    vigil_token_kind_t ck = cur->kind;

    (void)f;

    /* After { -> newline. */
    if (pk == VIGIL_TOKEN_LBRACE)
        return 1;

    /* Before } -> newline. */
    if (ck == VIGIL_TOKEN_RBRACE)
        return 1;

    /* After ; -> newline (unless inside for-loop header). */
    if (pk == VIGIL_TOKEN_SEMICOLON)
        return 1;

    /* After } and before something that isn't } or else -> blank line
       (top-level decl separation). But } else { stays on one line. */
    if (pk == VIGIL_TOKEN_RBRACE)
    {
        if (ck == VIGIL_TOKEN_ELSE)
            return 0;
        if (ck == VIGIL_TOKEN_RBRACE)
            return 1;
        return 2;
    }

    /* Before case/default in switch. */
    if (ck == VIGIL_TOKEN_CASE || ck == VIGIL_TOKEN_DEFAULT)
        return 1;

    return 0;
}

vigil_status_t vigil_fmt(const char *source_text, size_t source_length, const vigil_token_list_t *tokens,
                         char **out_text, size_t *out_length, vigil_error_t *error)
{
    (void)error;
    fmt_state_t f;
    f.src = source_text;
    f.src_len = source_length;
    f.tokens = tokens;
    f.count = vigil_token_list_count(tokens);
    buf_init(&f.out);
    f.indent = 0;
    f.at_line_start = true;

    if (f.count == 0 || (f.count == 1 && vigil_token_list_get(tokens, 0)->kind == VIGIL_TOKEN_EOF))
    {
        *out_text = calloc(1, 1);
        *out_length = 0;
        return VIGIL_STATUS_OK;
    }

    /* ── Pass 1: collect and sort imports ─────────────────────────── */

    import_info_t *imports = NULL;
    size_t import_count = 0;
    size_t import_cap = 0;
    size_t first_non_import = 0; /* token index of first non-import decl */

    {
        size_t i = 0;
        while (i < f.count)
        {
            const vigil_token_t *t = vigil_token_list_get(tokens, i);
            if (t->kind != VIGIL_TOKEN_IMPORT)
                break;

            import_info_t imp = {0};
            imp.start_idx = i;
            i++; /* skip 'import' */

            /* Expect string literal. */
            if (i < f.count)
            {
                const vigil_token_t *path_tok = vigil_token_list_get(tokens, i);
                if (path_tok->kind == VIGIL_TOKEN_STRING_LITERAL)
                {
                    /* Strip quotes. */
                    imp.path = tok_text(&f, path_tok) + 1;
                    imp.path_len = tok_len(path_tok) - 2;
                }
                i++;
            }

            /* Skip optional 'as' alias. */
            if (i < f.count && vigil_token_list_get(tokens, i)->kind == VIGIL_TOKEN_AS)
            {
                i++; /* 'as' */
                if (i < f.count)
                    i++; /* alias ident */
            }

            /* Expect semicolon. */
            if (i < f.count && vigil_token_list_get(tokens, i)->kind == VIGIL_TOKEN_SEMICOLON)
            {
                i++;
            }
            imp.end_idx = i;

            if (import_count == import_cap)
            {
                import_cap = import_cap ? import_cap * 2 : 8;
                imports = realloc(imports, import_cap * sizeof(import_info_t));
            }
            imports[import_count++] = imp;
        }
        first_non_import = i;
    }

    /* Sort imports by path. */
    if (import_count > 1)
    {
        qsort(imports, import_count, sizeof(import_info_t), import_cmp);
    }

    /* ── Pass 2: emit sorted imports ─────────────────────────────── */

    /* Emit any comments that appear before the first import (module header). */
    size_t initial_comment_end = 0;
    if (import_count > 0)
    {
        const vigil_token_t *first_imp_tok = vigil_token_list_get(tokens, imports[0].start_idx);
        if (emit_comments_between(&f, 0, first_imp_tok->span.start_offset))
        {
            emit_newline(&f);
        }
    }

    /* Emit any comments between the last import and the first non-import. */
    if (first_non_import < f.count)
    {
        const vigil_token_t *first_ni = vigil_token_list_get(tokens, first_non_import);
        size_t scan_start = 0;
        if (import_count > 0)
        {
            const vigil_token_t *last_imp_tok = vigil_token_list_get(tokens, imports[import_count - 1].end_idx - 1);
            scan_start = last_imp_tok->span.end_offset;
        }
        emit_comments_between(&f, scan_start, first_ni->span.start_offset);
        initial_comment_end = first_ni->span.start_offset;
    }

    for (size_t ii = 0; ii < import_count; ii++)
    {
        const import_info_t *imp = &imports[ii];
        /* Emit comments before this import in original source. */
        const vigil_token_t *imp_tok = vigil_token_list_get(tokens, imp->start_idx);
        if (ii > 0)
        {
            const vigil_token_t *prev_end_tok = vigil_token_list_get(tokens, imports[ii - 1].end_idx - 1);
            emit_comments_between(&f, prev_end_tok->span.end_offset, imp_tok->span.start_offset);
        }

        /* Emit: import "path"; or import "path" as alias; */
        emit_cstr(&f, "import \"");
        buf_write(&f.out, imp->path, imp->path_len);
        buf_push(&f.out, '"');

        /* Check for 'as' alias. */
        size_t j = imp->start_idx + 2; /* after 'import' and string */
        if (j < imp->end_idx && vigil_token_list_get(tokens, j)->kind == VIGIL_TOKEN_AS)
        {
            buf_puts(&f.out, " as ");
            j++;
            if (j < imp->end_idx)
            {
                const vigil_token_t *alias = vigil_token_list_get(tokens, j);
                buf_write(&f.out, tok_text(&f, alias), tok_len(alias));
            }
        }
        buf_push(&f.out, ';');
        emit_newline(&f);
    }

    /* Blank line between imports and rest. */
    if (import_count > 0 && first_non_import < f.count)
    {
        const vigil_token_t *next = vigil_token_list_get(tokens, first_non_import);
        if (next->kind != VIGIL_TOKEN_EOF)
        {
            emit_newline(&f);
        }
    }

    /* ── Pass 3: emit remaining tokens ───────────────────────────── */

    /*
     * Context tracking:
     * - for-loop headers: semicolons inside for(...) don't cause newlines
     * - switch: case/default labels get indented, bodies get double-indented
     * - in_case_body: true after a case/default colon, dedent before next case/}/default
     */
    int for_paren_depth = 0;
    bool in_for_header = false;
    bool in_case_body = false;
    bool after_case_kw = false; /* true between case/default keyword and its colon */
    /* Track literal braces (map/array literals) vs block braces.
       A { is literal when preceded by =, (, [, comma, return, or colon. */
    bool brace_is_literal[64]; /* stack: true if brace at this depth is literal */
    int brace_depth = 0;
    bool prev_was_literal_rbrace = false;
    memset(brace_is_literal, 0, sizeof(brace_is_literal));
    /* Generic type tracking: array<...>, map<...> */
    int generic_depth = 0; /* >0 when inside <...> of a generic type */
    /* Ternary tracking: count of unmatched ? tokens. */
    int ternary_depth = 0;
    /* Enum body tracking. */
    bool in_enum_body = false;

    for (size_t i = first_non_import; i < f.count; i++)
    {
        const vigil_token_t *cur = vigil_token_list_get(tokens, i);
        if (cur->kind == VIGIL_TOKEN_EOF)
            break;

        /* Emit comments between previous token and this one. */
        size_t gap_start;
        if (i > first_non_import)
        {
            const vigil_token_t *prev = vigil_token_list_get(tokens, i - 1);
            gap_start = prev->span.end_offset;
        }
        else
        {
            gap_start = initial_comment_end;
        }
        bool had_comment = emit_comments_between(&f, gap_start, cur->span.start_offset);

        /* Dedent before } */
        if (cur->kind == VIGIL_TOKEN_RBRACE)
        {
            brace_depth--;
            bool is_lit = (brace_depth >= 0 && brace_depth < 64 && brace_is_literal[brace_depth]);
            if (!is_lit)
            {
                if (in_case_body)
                {
                    f.indent--;
                    in_case_body = false;
                }
                f.indent--;
            }
        }

        /* Dedent case body before next case/default. */
        if ((cur->kind == VIGIL_TOKEN_CASE || cur->kind == VIGIL_TOKEN_DEFAULT) && in_case_body)
        {
            f.indent--;
            in_case_body = false;
        }

        /* Track case/default keyword for colon detection. */
        if (cur->kind == VIGIL_TOKEN_CASE || cur->kind == VIGIL_TOKEN_DEFAULT)
        {
            after_case_kw = true;
        }

        /* Determine spacing/newlines. */
        if (i > first_non_import && !had_comment)
        {
            const vigil_token_t *prev = vigil_token_list_get(tokens, i - 1);

            /* Special: semicolons inside for-header don't cause newlines. */
            bool suppress_newline = in_for_header && prev->kind == VIGIL_TOKEN_SEMICOLON;

            /* In enum body, commas cause newlines. */
            bool force_newline = false;
            if (in_enum_body && prev->kind == VIGIL_TOKEN_COMMA)
            {
                force_newline = true;
            }

            /* Suppress newlines around literal braces (map/array). */
            bool in_literal = (brace_depth > 0 && brace_depth < 64 && brace_is_literal[brace_depth - 1]);
            /* Also suppress if we're about to close a literal brace. */
            if (cur->kind == VIGIL_TOKEN_RBRACE && brace_depth >= 0 && brace_depth < 64 &&
                brace_is_literal[brace_depth])
            {
                in_literal = true;
            }
            /* Suppress newline after literal { */
            if (prev->kind == VIGIL_TOKEN_LBRACE && brace_depth > 0 && brace_depth <= 64 &&
                brace_is_literal[brace_depth - 1])
            {
                in_literal = true;
            }
            if (in_literal && (prev->kind == VIGIL_TOKEN_LBRACE || cur->kind == VIGIL_TOKEN_RBRACE ||
                               prev->kind == VIGIL_TOKEN_SEMICOLON || prev->kind == VIGIL_TOKEN_RBRACE))
            {
                suppress_newline = true;
            }
            /* After a literal }, don't insert blank line before ; */
            if (prev_was_literal_rbrace && prev->kind == VIGIL_TOKEN_RBRACE)
            {
                suppress_newline = true;
            }

            int nl = need_newline_before(prev, cur, &f);

            /* Blank line between top-level declarations. */
            if (nl == 1 && f.indent == 0 && prev->kind == VIGIL_TOKEN_SEMICOLON &&
                (cur->kind == VIGIL_TOKEN_FN || cur->kind == VIGIL_TOKEN_CLASS || cur->kind == VIGIL_TOKEN_INTERFACE ||
                 cur->kind == VIGIL_TOKEN_ENUM || cur->kind == VIGIL_TOKEN_CONST || cur->kind == VIGIL_TOKEN_PUB))
            {
                nl = 2;
            }

            if (suppress_newline)
            {
                if (need_space_before(prev, cur, &f))
                {
                    emit_space(&f);
                }
            }
            else if (force_newline || nl == 2)
            {
                if (!f.at_line_start)
                    emit_newline(&f);
                if (nl == 2 && !force_newline)
                    emit_newline(&f);
            }
            else if (nl == 1)
            {
                if (!f.at_line_start)
                    emit_newline(&f);
            }
            else
            {
                bool space = need_space_before(prev, cur, &f);
                /* Override: no space around < > inside generics. */
                if (generic_depth > 0)
                {
                    if (cur->kind == VIGIL_TOKEN_GREATER)
                        space = false;
                    if (prev->kind == VIGIL_TOKEN_LESS)
                        space = false;
                    if (prev->kind == VIGIL_TOKEN_GREATER)
                        space = false;
                }
                /* No space before < when it opens a generic (after array/map/fn). */
                if (cur->kind == VIGIL_TOKEN_LESS && prev->kind == VIGIL_TOKEN_IDENTIFIER)
                {
                    const char *pt = tok_text(&f, prev);
                    size_t pl = tok_len(prev);
                    if ((pl == 5 && memcmp(pt, "array", 5) == 0) || (pl == 3 && memcmp(pt, "map", 3) == 0))
                    {
                        space = false;
                    }
                }
                /* Add space before ternary colon. */
                if (cur->kind == VIGIL_TOKEN_COLON && ternary_depth > 0 && !after_case_kw)
                {
                    space = true;
                }
                if (space)
                {
                    emit_space(&f);
                }
            }
        }

        /* Emit the token. */
        emit_str(&f, tok_text(&f, cur), tok_len(cur));

        /* Indent after { (only for block braces). */
        if (cur->kind == VIGIL_TOKEN_LBRACE)
        {
            /* Determine if this is a literal brace. */
            bool is_lit = false;
            if (i > 0)
            {
                const vigil_token_t *prev = vigil_token_list_get(tokens, i - 1);
                if (is_assign_op(prev->kind) || prev->kind == VIGIL_TOKEN_LPAREN ||
                    prev->kind == VIGIL_TOKEN_LBRACKET || prev->kind == VIGIL_TOKEN_COMMA ||
                    prev->kind == VIGIL_TOKEN_RETURN || prev->kind == VIGIL_TOKEN_COLON)
                {
                    is_lit = true;
                }
            }
            if (brace_depth < 64)
            {
                brace_is_literal[brace_depth] = is_lit;
            }
            brace_depth++;
            if (!is_lit)
            {
                f.indent++;
            }
        }

        /* After a case/default colon, indent the body. */
        if (cur->kind == VIGIL_TOKEN_COLON && after_case_kw)
        {
            after_case_kw = false;
            in_case_body = true;
            f.indent++;
        }

        /* Track for-loop header. */
        if (cur->kind == VIGIL_TOKEN_FOR)
        {
            in_for_header = true;
            for_paren_depth = 0;
        }
        if (in_for_header)
        {
            if (cur->kind == VIGIL_TOKEN_LPAREN)
                for_paren_depth++;
            if (cur->kind == VIGIL_TOKEN_RPAREN)
            {
                for_paren_depth--;
                if (for_paren_depth <= 0)
                    in_for_header = false;
            }
        }

        /* Track generic type angle brackets. */
        if (cur->kind == VIGIL_TOKEN_LESS && i > 0)
        {
            const vigil_token_t *prev = vigil_token_list_get(tokens, i - 1);
            if (prev->kind == VIGIL_TOKEN_IDENTIFIER)
            {
                const char *pt = tok_text(&f, prev);
                size_t pl = tok_len(prev);
                if ((pl == 5 && memcmp(pt, "array", 5) == 0) || (pl == 3 && memcmp(pt, "map", 3) == 0))
                {
                    generic_depth++;
                }
            }
            /* Also handle nested: map<string, array<i32>> */
            if (generic_depth > 0)
            {
                /* Already inside a generic, this < opens a nested one. */
                if (prev->kind == VIGIL_TOKEN_COMMA || prev->kind == VIGIL_TOKEN_LESS)
                {
                    /* Check next token for type name. */
                }
            }
        }
        if (cur->kind == VIGIL_TOKEN_GREATER && generic_depth > 0)
        {
            generic_depth--;
        }

        /* Track ternary ? : pairs. */
        if (cur->kind == VIGIL_TOKEN_QUESTION)
        {
            ternary_depth++;
        }
        if (cur->kind == VIGIL_TOKEN_COLON && ternary_depth > 0 && !after_case_kw)
        {
            ternary_depth--;
        }

        /* Track whether this was a literal }. */
        if (cur->kind == VIGIL_TOKEN_RBRACE)
        {
            prev_was_literal_rbrace = (brace_depth >= 0 && brace_depth < 64 && brace_is_literal[brace_depth]);
            if (in_enum_body)
                in_enum_body = false;
        }
        else
        {
            prev_was_literal_rbrace = false;
        }

        /* Track enum body. */
        if (cur->kind == VIGIL_TOKEN_ENUM)
        {
            /* Next { starts enum body. */
        }
        if (cur->kind == VIGIL_TOKEN_LBRACE && i > 0)
        {
            /* Check if this { follows an enum declaration. */
            for (size_t k = i; k > 0 && i - k < 10; k--)
            {
                const vigil_token_t *tk = vigil_token_list_get(tokens, k - 1);
                if (tk->kind == VIGIL_TOKEN_ENUM)
                {
                    in_enum_body = true;
                    break;
                }
                if (tk->kind == VIGIL_TOKEN_LBRACE || tk->kind == VIGIL_TOKEN_RBRACE ||
                    tk->kind == VIGIL_TOKEN_SEMICOLON)
                    break;
            }
        }
    }

    /* Emit trailing comments. */
    if (f.count > 0)
    {
        const vigil_token_t *last = vigil_token_list_get(tokens, f.count - 1);
        emit_comments_between(&f, last->span.end_offset, f.src_len);
    }

    /* Ensure trailing newline. */
    if (f.out.len > 0 && f.out.data[f.out.len - 1] != '\n')
    {
        buf_push(&f.out, '\n');
    }

    /* Strip trailing blank lines (keep exactly one \n). */
    while (f.out.len > 1 && f.out.data[f.out.len - 1] == '\n' && f.out.data[f.out.len - 2] == '\n')
    {
        f.out.len--;
    }

    free(imports);

    buf_push(&f.out, '\0');
    *out_text = f.out.data;
    *out_length = f.out.len - 1; /* exclude NUL */
    return VIGIL_STATUS_OK;
}
