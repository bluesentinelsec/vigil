# BASL Maintainer Runbook

Checklist for every change to the BASL language, stdlib, or tooling.

---

## 1. Update Documentation

### Language Documentation

File: `docs/syntax.md` — language syntax only (types, operators, control flow, classes, etc.)
File: `docs/stdlib.md` — standard library module reference (fmt, os, file, math, etc.)

- New keyword or operator → add to the relevant section in `syntax.md`
- New type → add to Types section in `syntax.md`
- New stdlib module or function → add to `stdlib.md` and create detailed doc in `docs/stdlib/<module>.md`
- Changed semantics (e.g. new error format) → update examples in-place
- New language construct (e.g. new statement form) → add section with grammar + example in `syntax.md`

### Toolchain Documentation

File: `docs/cli.md` — comprehensive CLI reference for all commands
File: `docs/package_cli.md` — detailed `basl package` documentation
File: `docs/embed.md` — `basl embed` documentation
File: `docs/debugger.md` — debugger usage
File: `docs/dependencies.md` — dependency management
File: `docs/project_structure.md` — project layout conventions
File: `docs/editor_cli.md` — editor support

- New CLI command → add to `cli.md` quick reference and create detailed section
- Changed CLI behavior → update relevant doc file
- New CLI flag or option → document in appropriate file

### User-Facing Documentation

File: `README.md` — project overview, quick start, feature highlights

- Major new feature → add to README features section
- Breaking change → update README examples if affected

## 2. Update Editor / IDE Support

### Vim (`editors/vim/`)

- New keyword → add to `syn keyword baslKeyword` in `syntax/basl.vim`
- New type → add to `syn keyword baslType`
- New constant/boolean → add to `syn keyword baslConstant` or `syn keyword baslBoolean`
- New string syntax → add `syn region` or `syn match`
- New comment syntax → add `syn match` or `syn region`

### VS Code (`editors/vscode/`)

- New keyword → add to `keywords` patterns in `syntaxes/basl.tmLanguage.json`
- New type → add to `types` patterns in `syntaxes/basl.tmLanguage.json`
- New constant → add to `constants` patterns in `syntaxes/basl.tmLanguage.json`
- New stdlib module → add module + function list to `completions.json`
- New stdlib function → add to the module's array in `completions.json`
- New snippet-worthy construct → add to `snippets/basl.json`
- New bracket/delimiter → update `language-configuration.json`

After changes, reload VS Code (`Cmd+Shift+P` → `Developer: Reload Window`) to verify.

## 3. Add Unit Tests

Location: `pkg/basl/interp/`, `pkg/basl/lexer/`, `pkg/basl/parser/`, `pkg/basl/value/`

- New language feature → add parser test (parses correctly) + interpreter test (evaluates correctly)
- New stdlib function → add interpreter test exercising the function and its error cases
- Bug fix → add regression test reproducing the bug before fixing
- New token/operator → add lexer test

Run:
```sh
make test
```

## 4. Format and Lint

```sh
make fmt       # gofmt -w .
make vet       # go vet ./...
make build     # runs fmt + vet, then builds
```

All three must pass clean before committing. `make build` chains them.

## 5. Verify BASL Formatter

After any change to the parser or AST, verify the BASL formatter still works:

```sh
go test ./pkg/basl/formatter/ -count=1   # unit tests
```

For new syntax constructs, add a formatter emit method and test case.
The formatter must be idempotent: formatting twice produces identical output.

## 6. Profile for Anomalies

Run after performance-sensitive changes (new eval paths, new stdlib, large refactors):

```sh
make prof-cpu   # CPU profile via benchmarks
make prof-mem   # Memory profile via benchmarks
```

Look for:
- New hot spots in `evalExpr` or `execStmt`
- Unexpected allocations in tight loops
- Stdlib functions doing redundant work

For coverage gaps:
```sh
make cover      # prints per-function coverage
```

## 7. Update Examples

### Examples directory (`examples/`)

- New language feature → add or update an example demonstrating it
- New stdlib module → add a standalone example showing practical usage
- Changed syntax → update affected examples so they still run
- Verify all examples work:

```sh
for f in examples/*.basl; do
    echo "=== $f ==="
    ./basl "$f" < /dev/null 2>&1 | head -10
done
```

Current examples:
- `hello.basl` - Basic hello world
- `collections.basl` - Arrays and maps
- `error_handling.basl` - Multi-return error patterns
- `classes.basl` - Object-oriented programming
- `file_operations.basl` - File I/O
- `json_parsing.basl` - JSON handling
- `database.basl` - SQLite operations
- `regex_patterns.basl` - Regular expressions
- `http_server.basl` - HTTP server
- `concurrency.basl` - Threads and mutexes
- `cli_tool.basl` - Argument parsing

---

## Quick Reference: Change → Actions

| Change | syntax.md | stdlib.md | vim | vscode | tests | examples |
|---|---|---|---|---|---|---|
| New keyword | ✓ | — | `baslKeyword` | `tmLanguage` | parser + interp | kick_tires |
| New type | ✓ | — | `baslType` | `tmLanguage` | parser + interp | kick_tires |
| New stdlib function | — | ✓ | — | `completions.json` | interp | standalone |
| New stdlib module | — | ✓ | — | `completions.json` | interp | standalone |
| New operator | ✓ | — | syntax if needed | `tmLanguage` | lexer + parser + interp | kick_tires |
| Bug fix | if behavior changed | — | — | — | regression test | fix affected |
| New snippet | — | — | — | `snippets.json` | — | — |
| Raylib binding | — | ✓ | — | `completions.json` | — | raylib example |
| New syntax construct | ✓ | — | if needed | if needed | parser + interp + formatter | kick_tires |

---

## Commit Discipline

- One logical change per commit
- Run `make test` before every commit
- `git add -A && git commit -m "descriptive message"`
- Message format: imperative mood, what changed and why
