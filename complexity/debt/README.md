# Current Debt Inventory

This directory records the current full-repo complexity/readability debt that
must be burned down before the complexity gate can become a hard full-repo
enforcement check.

## Lizard Snapshot

Snapshot source:

- branch: `feature/complexity-gate`
- thresholds:
  - cyclomatic complexity (`ccn`): `30`
  - function length: `120`
  - parameter count: `6`

Current state after the first cleanup pass in PR `#187`:

- first-party C functions analyzed: `2981`
- inherited above-threshold functions: `126`
- metric overages:
  - `66` `ccn` violations
  - `82` function-length violations
  - `37` parameter-count violations

Highest-debt files:

- `src/compiler.c`: `33`
- `src/cli/main.c`: `11`
- `src/stdlib/regex_engine.c`: `7`
- `src/toml.c`: `5`
- `src/value.c`: `5`
- `src/vm.c`: `5`
- `src/compiler_declarations.c`: `4`
- `src/doc.c`: `4`
- `src/cli_lib.c`: `3`
- `src/compiler_builtins.c`: `3`

Highest-priority functions to split next:

- `src/chunk.c:686` `vigil_chunk_disassemble` (`ccn=94`, `length=330`)
- `src/cli/main.c:3059` `cmd_repl` (`ccn=44`, `length=243`)
- `src/cli/main.c:3536` `main` (`ccn=46`, `length=230`)
- `src/cli/main.c:1176` `debug_cli_callback` (`ccn=53`, `length=183`)
- `src/cli/main.c:262` `register_source_tree` (`ccn=36`, `length=190`)
- `src/compiler.c:12274` `vigil_parser_parse_assignment_statement_internal`
- `src/compiler.c:8504` `vigil_parser_parse_primary_base`
- `src/compiler_typeparsing.c:83` `vigil_program_parse_type_reference`
- `src/stdlib/regex_engine.c:643` `parse_quantifier`
- `src/vm.c:2728` `vigil_vm_execute_function`

Debt reduced in this PR so far:

- `vigil_opcode_name()` is no longer above threshold
- the inherited lizard debt count dropped from `128` to `126`

## Clang-Tidy Snapshot

The first version of PR `#187` did not execute `clang-tidy` in CI because the
runner script depended on `rg`, which is not installed on GitHub's Ubuntu
runner image by default.

This PR fixes that runner. The canonical readability inventory is now produced
by the `complexity` workflow artifacts:

- `complexity-artifacts/clang-tidy-main.log`
- `complexity-artifacts/clang-tidy-pr.log`
- `complexity-artifacts/clang-tidy-summary.json`

Until the readability backlog is burned down, the CI policy remains:

- analyze the full repo
- fail on new or worsened debt
- keep the inherited debt inventory explicit here
