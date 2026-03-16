#ifndef BASL_CLI_LIB_H
#define BASL_CLI_LIB_H

#include <stddef.h>

#include "basl/allocator.h"
#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal CLI argument parser.
 *
 * Supports:
 *   - Subcommands:       basl run file.basl
 *   - Long flags:        --verbose, --output=file.txt, --output file.txt
 *   - Short flags:       -v, -o file.txt
 *   - Boolean flags:     --verbose (no value)
 *   - Positional args:   basl run file.basl
 *   - Auto help:         --help / -h prints usage
 *
 * Usage:
 *   basl_cli_t cli;
 *   basl_cli_init(&cli, "basl", "Blazingly Awesome Scripting Language");
 *
 *   basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run a script");
 *   const char *file = NULL;
 *   int verbose = 0;
 *   basl_cli_add_positional(run, "file", "Script file to run", &file);
 *   basl_cli_add_bool_flag(run, "verbose", 'v', "Enable verbose output", &verbose);
 *
 *   basl_cli_parse(&cli, argc, argv, &error);
 *   // file and verbose are now populated
 *
 *   basl_cli_free(&cli);
 */

typedef struct basl_cli_flag {
    const char *name;       /* long name (without --) */
    char short_name;        /* single char (0 if none) */
    const char *help;
    int is_bool;
    /* Output pointers — exactly one is non-NULL. */
    const char **out_string;
    int *out_bool;
} basl_cli_flag_t;

typedef struct basl_cli_positional {
    const char *name;
    const char *help;
    const char **out_value;
} basl_cli_positional_t;

typedef struct basl_cli_command {
    const char *name;
    const char *help;
    basl_cli_flag_t *flags;
    size_t flag_count;
    size_t flag_capacity;
    basl_cli_positional_t *positionals;
    size_t positional_count;
    size_t positional_capacity;
    int matched;
} basl_cli_command_t;

typedef struct basl_cli {
    const char *program_name;
    const char *description;
    basl_allocator_t allocator;
    /* Global flags (apply to all commands). */
    basl_cli_flag_t *flags;
    size_t flag_count;
    size_t flag_capacity;
    /* Subcommands. */
    basl_cli_command_t *commands;
    size_t command_count;
    size_t command_capacity;
    /* The matched command after parsing (NULL if none). */
    basl_cli_command_t *matched_command;
    /* Global positionals (when no subcommands are used). */
    basl_cli_positional_t *positionals;
    size_t positional_count;
    size_t positional_capacity;
    /* Set to 1 if help was printed during parsing. */
    int help_shown;
} basl_cli_t;

/* ── Lifecycle ───────────────────────────────────────────────────── */

BASL_API void basl_cli_init(
    basl_cli_t *cli,
    const char *program_name,
    const char *description
);

BASL_API void basl_cli_init_with_allocator(
    basl_cli_t *cli,
    const char *program_name,
    const char *description,
    const basl_allocator_t *allocator
);

BASL_API void basl_cli_free(basl_cli_t *cli);

/* ── Commands ────────────────────────────────────────────────────── */

BASL_API basl_cli_command_t *basl_cli_add_command(
    basl_cli_t *cli,
    const char *name,
    const char *help
);

/** Returns the matched command after parsing, or NULL. */
BASL_API const basl_cli_command_t *basl_cli_matched_command(
    const basl_cli_t *cli
);

/* ── Flags (on a command, or globally on the cli) ────────────────── */

BASL_API void basl_cli_add_string_flag(
    basl_cli_command_t *command,
    const char *name,
    char short_name,
    const char *help,
    const char **out_value
);

BASL_API void basl_cli_add_bool_flag(
    basl_cli_command_t *command,
    const char *name,
    char short_name,
    const char *help,
    int *out_value
);

/* Global flags (not tied to a command). */
BASL_API void basl_cli_add_global_string_flag(
    basl_cli_t *cli,
    const char *name,
    char short_name,
    const char *help,
    const char **out_value
);

BASL_API void basl_cli_add_global_bool_flag(
    basl_cli_t *cli,
    const char *name,
    char short_name,
    const char *help,
    int *out_value
);

/* ── Positional args ─────────────────────────────────────────────── */

BASL_API void basl_cli_add_positional(
    basl_cli_command_t *command,
    const char *name,
    const char *help,
    const char **out_value
);

/* Global positional (when not using subcommands). */
BASL_API void basl_cli_add_global_positional(
    basl_cli_t *cli,
    const char *name,
    const char *help,
    const char **out_value
);

/* ── Parsing ─────────────────────────────────────────────────────── */

/**
 * Parse argc/argv.  Populates all bound output pointers.
 * Returns BASL_STATUS_OK on success.
 * Returns BASL_STATUS_INVALID_ARGUMENT on parse error (message in error).
 * Prints help and returns BASL_STATUS_OK if --help/-h is found.
 */
BASL_API basl_status_t basl_cli_parse(
    basl_cli_t *cli,
    int argc,
    char **argv,
    basl_error_t *error
);

/* ── Help ────────────────────────────────────────────────────────── */

BASL_API void basl_cli_print_help(const basl_cli_t *cli);
BASL_API void basl_cli_print_command_help(
    const basl_cli_t *cli,
    const basl_cli_command_t *command
);

#ifdef __cplusplus
}
#endif

#endif
