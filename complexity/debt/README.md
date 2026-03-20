# Current Debt Inventory

This directory records the current full-repo complexity/readability debt that
must be burned down before the complexity gate can become a hard full-repo
enforcement check.

## Lizard Snapshot

Snapshot source:

- branch: `feature/complexity-burndown`
- thresholds:
  - cyclomatic complexity (`ccn`): `30`
  - function length: `120`
  - parameter count: `6`

Current state after the current cleanup tranche on this branch:

- first-party C functions analyzed: `3043`
- inherited above-threshold functions: `115`
- metric overages:
  - `63` `ccn` violations
  - `76` function-length violations
  - `33` parameter-count violations

Highest-debt files:

- `src/compiler.c`: `33`
- `src/cli/main.c`: `7`
- `src/stdlib/regex_engine.c`: `7`
- `src/toml.c`: `5`
- `src/value.c`: `5`
- `src/vm.c`: `5`
- `src/compiler_declarations.c`: `4`
- `src/doc.c`: `4`
- `src/compiler_builtins.c`: `3`
- `src/platform/platform_win32.c`: `3`

Highest-priority functions to split next:

- `src/cli/main.c:3059` `cmd_repl` (`ccn=44`, `length=243`)
- `src/cli/main.c:3536` `main` (`ccn=46`, `length=230`)
- `src/cli/main.c:1176` `debug_cli_callback` (`ccn=53`, `length=183`)
- `src/cli/main.c:262` `register_source_tree` (`ccn=36`, `length=190`)
- `src/compiler.c:12274` `vigil_parser_parse_assignment_statement_internal`
- `src/compiler.c:8504` `vigil_parser_parse_primary_base`
- `src/compiler_typeparsing.c:83` `vigil_program_parse_type_reference`
- `src/stdlib/regex_engine.c:643` `parse_quantifier`
- `src/vm.c:2728` `vigil_vm_execute_function`

Debt reduced on this branch so far:

- `src/chunk.c` no longer carries inherited lizard debt
- `src/cli_lib.c` no longer carries inherited lizard debt
- `src/binding.c` no longer carries inherited lizard debt
- `src/cli/main.c` no longer carries inherited lizard debt in `cmd_new`, `cmd_test`, or `run_one_test`
- the inherited lizard debt count dropped from `126` to `115`

## Clang-Tidy Snapshot

The first version of PR `#187` did not execute `clang-tidy` in CI because the
runner script depended on `rg`, which is not installed on GitHub's Ubuntu
runner image by default.

This PR fixes that runner and also scopes analysis to the files present in the
configured `compile_commands.json` database. That avoids Linux `clang-tidy`
trying to parse Windows-only sources like `src/platform/platform_win32.c`.

First real Linux inventory from the corrected workflow before the compile-db
filter landed:

- `361` diagnostics total
- `350` `readability-function-cognitive-complexity`
- `6` `readability-else-after-return`
- `4` `bugprone-branch-clone`
- `1` `clang-diagnostic-error` caused by Linux parsing `platform_win32.c`

That means the current readability backlog is overwhelmingly cognitive
complexity in large parser, VM, CLI, and test functions.

The canonical readability inventory is produced by the `complexity` workflow
artifacts:

- `complexity-artifacts/clang-tidy-main.log`
- `complexity-artifacts/clang-tidy-pr.log`
- `complexity-artifacts/clang-tidy-summary.json`

Until the readability backlog is burned down, the CI policy remains:

- analyze the full repo
- fail on new or worsened debt
- keep the inherited debt inventory explicit here
