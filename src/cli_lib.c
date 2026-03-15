#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/cli_lib.h"
#include "internal/basl_internal.h"

/* ── Allocator helpers ───────────────────────────────────────────── */

static void *cli_realloc(const basl_allocator_t *a, void *p, size_t size) {
    return a->reallocate(a->user_data, p, size);
}

static void cli_dealloc(const basl_allocator_t *a, void *p) {
    a->deallocate(a->user_data, p);
}

/* ── Lifecycle ───────────────────────────────────────────────────── */

void basl_cli_init_with_allocator(
    basl_cli_t *cli,
    const char *program_name,
    const char *description,
    const basl_allocator_t *allocator
) {
    if (cli == NULL) return;
    memset(cli, 0, sizeof(*cli));
    cli->program_name = program_name;
    cli->description = description;
    cli->allocator = (allocator != NULL && basl_allocator_is_valid(allocator))
                         ? *allocator
                         : basl_default_allocator();
}

void basl_cli_init(
    basl_cli_t *cli,
    const char *program_name,
    const char *description
) {
    basl_cli_init_with_allocator(cli, program_name, description, NULL);
}

void basl_cli_free(basl_cli_t *cli) {
    if (cli == NULL) return;
    const basl_allocator_t *a = &cli->allocator;
    for (size_t i = 0; i < cli->command_count; i++) {
        cli_dealloc(a, cli->commands[i].flags);
        cli_dealloc(a, cli->commands[i].positionals);
    }
    cli_dealloc(a, cli->commands);
    cli_dealloc(a, cli->flags);
    cli_dealloc(a, cli->positionals);
    memset(cli, 0, sizeof(*cli));
}

/* ── Commands ────────────────────────────────────────────────────── */

basl_cli_command_t *basl_cli_add_command(
    basl_cli_t *cli,
    const char *name,
    const char *help
) {
    if (cli == NULL) return NULL;
    const basl_allocator_t *a = &cli->allocator;

    if (cli->command_count >= cli->command_capacity) {
        size_t new_cap = cli->command_capacity < 4 ? 4 : cli->command_capacity * 2;
        basl_cli_command_t *new_cmds = (basl_cli_command_t *)cli_realloc(
            a, cli->commands, new_cap * sizeof(basl_cli_command_t));
        if (new_cmds == NULL) return NULL;
        cli->commands = new_cmds;
        cli->command_capacity = new_cap;
    }

    basl_cli_command_t *cmd = &cli->commands[cli->command_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->name = name;
    cmd->help = help;
    return cmd;
}

const basl_cli_command_t *basl_cli_matched_command(const basl_cli_t *cli) {
    return cli != NULL ? cli->matched_command : NULL;
}

/* ── Flag/positional helpers ─────────────────────────────────────── */

static void add_flag(
    const basl_allocator_t *a,
    basl_cli_flag_t **flags, size_t *count, size_t *capacity,
    const char *name, char short_name, const char *help,
    int is_bool, const char **out_string, int *out_bool
) {
    if (*count >= *capacity) {
        size_t new_cap = *capacity < 8 ? 8 : *capacity * 2;
        basl_cli_flag_t *nf = (basl_cli_flag_t *)cli_realloc(
            a, *flags, new_cap * sizeof(basl_cli_flag_t));
        if (nf == NULL) return;
        *flags = nf;
        *capacity = new_cap;
    }
    basl_cli_flag_t *f = &(*flags)[(*count)++];
    f->name = name;
    f->short_name = short_name;
    f->help = help;
    f->is_bool = is_bool;
    f->out_string = out_string;
    f->out_bool = out_bool;
}

static void add_positional(
    const basl_allocator_t *a,
    basl_cli_positional_t **positionals, size_t *count, size_t *capacity,
    const char *name, const char *help, const char **out_value
) {
    if (*count >= *capacity) {
        size_t new_cap = *capacity < 4 ? 4 : *capacity * 2;
        basl_cli_positional_t *np = (basl_cli_positional_t *)cli_realloc(
            a, *positionals, new_cap * sizeof(basl_cli_positional_t));
        if (np == NULL) return;
        *positionals = np;
        *capacity = new_cap;
    }
    basl_cli_positional_t *p = &(*positionals)[(*count)++];
    p->name = name;
    p->help = help;
    p->out_value = out_value;
}

/* ── Public flag/positional API ──────────────────────────────────── */

void basl_cli_add_string_flag(
    basl_cli_command_t *cmd, const char *name, char short_name,
    const char *help, const char **out_value
) {
    if (cmd == NULL) return;
    /* Commands don't own an allocator — use default. Caller must keep
       the cli alive.  We store into the command's own arrays. */
    basl_allocator_t a = basl_default_allocator();
    add_flag(&a, &cmd->flags, &cmd->flag_count, &cmd->flag_capacity,
             name, short_name, help, 0, out_value, NULL);
}

void basl_cli_add_bool_flag(
    basl_cli_command_t *cmd, const char *name, char short_name,
    const char *help, int *out_value
) {
    if (cmd == NULL) return;
    basl_allocator_t a = basl_default_allocator();
    add_flag(&a, &cmd->flags, &cmd->flag_count, &cmd->flag_capacity,
             name, short_name, help, 1, NULL, out_value);
}

void basl_cli_add_global_string_flag(
    basl_cli_t *cli, const char *name, char short_name,
    const char *help, const char **out_value
) {
    if (cli == NULL) return;
    add_flag(&cli->allocator, &cli->flags, &cli->flag_count, &cli->flag_capacity,
             name, short_name, help, 0, out_value, NULL);
}

void basl_cli_add_global_bool_flag(
    basl_cli_t *cli, const char *name, char short_name,
    const char *help, int *out_value
) {
    if (cli == NULL) return;
    add_flag(&cli->allocator, &cli->flags, &cli->flag_count, &cli->flag_capacity,
             name, short_name, help, 1, NULL, out_value);
}

void basl_cli_add_positional(
    basl_cli_command_t *cmd, const char *name, const char *help,
    const char **out_value
) {
    if (cmd == NULL) return;
    basl_allocator_t a = basl_default_allocator();
    add_positional(&a, &cmd->positionals, &cmd->positional_count,
                   &cmd->positional_capacity, name, help, out_value);
}

void basl_cli_add_global_positional(
    basl_cli_t *cli, const char *name, const char *help,
    const char **out_value
) {
    if (cli == NULL) return;
    add_positional(&cli->allocator, &cli->positionals, &cli->positional_count,
                   &cli->positional_capacity, name, help, out_value);
}

/* ── Help printing ───────────────────────────────────────────────── */

static void print_flags(const basl_cli_flag_t *flags, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const basl_cli_flag_t *f = &flags[i];
        if (f->short_name != 0) {
            fprintf(stderr, "  -%c, --%-16s %s\n",
                    f->short_name, f->name, f->help ? f->help : "");
        } else {
            fprintf(stderr, "      --%-16s %s\n",
                    f->name, f->help ? f->help : "");
        }
    }
}

void basl_cli_print_help(const basl_cli_t *cli) {
    if (cli == NULL) return;
    fprintf(stderr, "%s", cli->program_name ? cli->program_name : "program");
    if (cli->description != NULL) {
        fprintf(stderr, " - %s", cli->description);
    }
    fprintf(stderr, "\n\nUsage:\n");

    if (cli->command_count > 0) {
        fprintf(stderr, "  %s <command> [options]\n\n",
                cli->program_name ? cli->program_name : "program");
        fprintf(stderr, "Commands:\n");
        for (size_t i = 0; i < cli->command_count; i++) {
            fprintf(stderr, "  %-20s %s\n",
                    cli->commands[i].name,
                    cli->commands[i].help ? cli->commands[i].help : "");
        }
    } else {
        fprintf(stderr, "  %s [options]",
                cli->program_name ? cli->program_name : "program");
        for (size_t i = 0; i < cli->positional_count; i++) {
            fprintf(stderr, " <%s>", cli->positionals[i].name);
        }
        fprintf(stderr, "\n");
    }

    if (cli->flag_count > 0) {
        fprintf(stderr, "\nGlobal options:\n");
        print_flags(cli->flags, cli->flag_count);
    }
}

void basl_cli_print_command_help(
    const basl_cli_t *cli,
    const basl_cli_command_t *cmd
) {
    if (cli == NULL || cmd == NULL) return;
    fprintf(stderr, "Usage: %s %s [options]",
            cli->program_name ? cli->program_name : "program",
            cmd->name);
    for (size_t i = 0; i < cmd->positional_count; i++) {
        fprintf(stderr, " <%s>", cmd->positionals[i].name);
    }
    fprintf(stderr, "\n");
    if (cmd->help != NULL) {
        fprintf(stderr, "\n%s\n", cmd->help);
    }
    if (cmd->flag_count > 0) {
        fprintf(stderr, "\nOptions:\n");
        print_flags(cmd->flags, cmd->flag_count);
    }
    if (cli->flag_count > 0) {
        fprintf(stderr, "\nGlobal options:\n");
        print_flags(cli->flags, cli->flag_count);
    }
}

/* ── Parsing ─────────────────────────────────────────────────────── */

static basl_cli_flag_t *find_long_flag(
    basl_cli_flag_t *flags, size_t count,
    const char *name, size_t name_len
) {
    for (size_t i = 0; i < count; i++) {
        if (strlen(flags[i].name) == name_len &&
            memcmp(flags[i].name, name, name_len) == 0) {
            return &flags[i];
        }
    }
    return NULL;
}

static basl_cli_flag_t *find_short_flag(
    basl_cli_flag_t *flags, size_t count, char c
) {
    for (size_t i = 0; i < count; i++) {
        if (flags[i].short_name == c) return &flags[i];
    }
    return NULL;
}

static void set_flag_value(basl_cli_flag_t *f, const char *value) {
    if (f->is_bool && f->out_bool != NULL) {
        *f->out_bool = 1;
    } else if (!f->is_bool && f->out_string != NULL) {
        *f->out_string = value;
    }
}

basl_status_t basl_cli_parse(
    basl_cli_t *cli,
    int argc,
    char **argv,
    basl_error_t *error
) {
    if (cli == NULL || argv == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "cli: invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    cli->matched_command = NULL;
    int arg_start = 1; /* skip program name */

    /* Try to match a subcommand. */
    basl_cli_command_t *cmd = NULL;
    if (cli->command_count > 0 && argc > 1) {
        /* Check if first arg is --help before command matching. */
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            basl_cli_print_help(cli);
            return BASL_STATUS_OK;
        }
        for (size_t i = 0; i < cli->command_count; i++) {
            if (strcmp(argv[1], cli->commands[i].name) == 0) {
                cmd = &cli->commands[i];
                cmd->matched = 1;
                cli->matched_command = cmd;
                arg_start = 2;
                break;
            }
        }
        if (cmd == NULL) {
            /* Not a known command — could be a global positional or error. */
            if (cli->positional_count == 0 && argv[1][0] != '-') {
                basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                                       "cli: unknown command");
                return BASL_STATUS_INVALID_ARGUMENT;
            }
        }
    }

    /* Determine which flags and positionals to use. */
    basl_cli_flag_t *cmd_flags = cmd ? cmd->flags : NULL;
    size_t cmd_flag_count = cmd ? cmd->flag_count : 0;
    basl_cli_positional_t *pos = cmd ? cmd->positionals : cli->positionals;
    size_t pos_count = cmd ? cmd->positional_count : cli->positional_count;
    size_t pos_idx = 0;

    for (int i = arg_start; i < argc; i++) {
        const char *arg = argv[i];

        /* --help / -h */
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            if (cmd != NULL) {
                basl_cli_print_command_help(cli, cmd);
            } else {
                basl_cli_print_help(cli);
            }
            return BASL_STATUS_OK;
        }

        /* -- stops flag parsing */
        if (strcmp(arg, "--") == 0) {
            for (int j = i + 1; j < argc && pos_idx < pos_count; j++) {
                *pos[pos_idx++].out_value = argv[j];
            }
            break;
        }

        /* Long flag: --name or --name=value */
        if (arg[0] == '-' && arg[1] == '-') {
            const char *name = arg + 2;
            const char *eq = strchr(name, '=');
            size_t name_len = eq ? (size_t)(eq - name) : strlen(name);

            basl_cli_flag_t *f = find_long_flag(cmd_flags, cmd_flag_count, name, name_len);
            if (f == NULL) {
                f = find_long_flag(cli->flags, cli->flag_count, name, name_len);
            }
            if (f == NULL) {
                basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                                       "cli: unknown flag");
                return BASL_STATUS_INVALID_ARGUMENT;
            }
            if (f->is_bool) {
                set_flag_value(f, NULL);
            } else if (eq != NULL) {
                set_flag_value(f, eq + 1);
            } else if (i + 1 < argc) {
                set_flag_value(f, argv[++i]);
            } else {
                basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                                       "cli: flag requires a value");
                return BASL_STATUS_INVALID_ARGUMENT;
            }
            continue;
        }

        /* Short flag: -v or -o value */
        if (arg[0] == '-' && arg[1] != '\0') {
            char c = arg[1];
            basl_cli_flag_t *f = find_short_flag(cmd_flags, cmd_flag_count, c);
            if (f == NULL) {
                f = find_short_flag(cli->flags, cli->flag_count, c);
            }
            if (f == NULL) {
                basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                                       "cli: unknown flag");
                return BASL_STATUS_INVALID_ARGUMENT;
            }
            if (f->is_bool) {
                set_flag_value(f, NULL);
            } else if (arg[2] != '\0') {
                /* -ovalue */
                set_flag_value(f, arg + 2);
            } else if (i + 1 < argc) {
                set_flag_value(f, argv[++i]);
            } else {
                basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                                       "cli: flag requires a value");
                return BASL_STATUS_INVALID_ARGUMENT;
            }
            continue;
        }

        /* Positional argument. */
        if (pos_idx < pos_count) {
            *pos[pos_idx++].out_value = arg;
        } else {
            basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                                   "cli: unexpected argument");
            return BASL_STATUS_INVALID_ARGUMENT;
        }
    }

    return BASL_STATUS_OK;
}
