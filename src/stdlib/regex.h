/* BASL regex engine - Thompson NFA implementation (RE2-style)
 *
 * Guarantees linear time O(n*m) matching where n=pattern, m=input.
 * No backtracking, no pathological cases.
 */
#ifndef BASL_STDLIB_REGEX_H
#define BASL_STDLIB_REGEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of capture groups */
#define BASL_REGEX_MAX_GROUPS 32

/* Opaque regex handle */
typedef struct basl_regex basl_regex_t;

/* Match result for a single capture group */
typedef struct basl_regex_match {
    size_t start;       /* Start offset in input (SIZE_MAX if not matched) */
    size_t end;         /* End offset in input */
} basl_regex_match_t;

/* Full match result */
typedef struct basl_regex_result {
    bool matched;
    size_t group_count;
    basl_regex_match_t groups[BASL_REGEX_MAX_GROUPS];
} basl_regex_result_t;

/* Compile a regex pattern. Returns NULL on error, sets error message. */
basl_regex_t *basl_regex_compile(
    const char *pattern,
    size_t pattern_len,
    char *error_buf,
    size_t error_buf_size
);

/* Free a compiled regex */
void basl_regex_free(basl_regex_t *re);

/* Check if input matches the pattern (anchored at start) */
bool basl_regex_match(
    const basl_regex_t *re,
    const char *input,
    size_t input_len,
    basl_regex_result_t *result
);

/* Find first match anywhere in input */
bool basl_regex_find(
    const basl_regex_t *re,
    const char *input,
    size_t input_len,
    basl_regex_result_t *result
);

/* Find all non-overlapping matches */
size_t basl_regex_find_all(
    const basl_regex_t *re,
    const char *input,
    size_t input_len,
    basl_regex_result_t *results,
    size_t max_results
);

/* Replace first match with replacement string */
basl_status_t basl_regex_replace(
    const basl_regex_t *re,
    const char *input,
    size_t input_len,
    const char *replacement,
    size_t replacement_len,
    char **output,
    size_t *output_len
);

/* Replace all matches with replacement string */
basl_status_t basl_regex_replace_all(
    const basl_regex_t *re,
    const char *input,
    size_t input_len,
    const char *replacement,
    size_t replacement_len,
    char **output,
    size_t *output_len
);

/* Split input by pattern */
basl_status_t basl_regex_split(
    const basl_regex_t *re,
    const char *input,
    size_t input_len,
    char ***parts,
    size_t **part_lens,
    size_t *part_count
);

#ifdef __cplusplus
}
#endif

#endif /* BASL_STDLIB_REGEX_H */
