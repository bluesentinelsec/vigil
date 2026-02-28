# BASL Maintainer Runbook

Checklist for every change to the BASL language, stdlib, or tooling.

---

## 1. Update the Syntax Guide

File: `docs/syntax.md` — language syntax only (types, operators, control flow, classes, etc.)
File: `docs/stdlib.md` — standard library module reference (fmt, os, file, math, etc.)

- New keyword or operator → add to the relevant section in `syntax.md`
- New type → add to Types section in `syntax.md`
- New stdlib module or function → add to `stdlib.md`
- Changed semantics (e.g. new error format) → update examples in-place
- New language construct (e.g. new statement form) → add section with grammar + example in `syntax.md`

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

### kick_tires (`examples/kick_tires/`)

- New language feature → write at least one exercise using it
- Changed syntax → update any affected programs so they still run
- Verify all examples still work:

```sh
for f in $(find examples/kick_tires -name '*.basl' -path '*/main.basl'); do
    echo "=== $f ==="
    go run ./cmd/basl "$f" < /dev/null 2>&1 | head -5
done
```

Note: ch12 (GUI) programs require interactive testing — they open windows.

### Feature examples (`examples/`)

- New stdlib module → add a standalone example demonstrating it
- New major feature (classes, ffi, raylib) → add or update a focused example
- Verify standalone examples:

```sh
for f in examples/*.basl; do
    echo "=== $f ==="
    go run ./cmd/basl "$f" < /dev/null 2>&1 | head -5
done
```

### Raylib examples (`examples/raylib/`)

- New raylib/raygui binding → update `hello_window.basl` or `demo_3d.basl` if relevant, or add a new example
- These require manual testing (they open windows)

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
