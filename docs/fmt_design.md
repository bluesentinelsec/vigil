# basl fmt — Code Formatter Design

## User Experience

```sh
basl fmt script.basl           # format one file (in-place)
basl fmt ./src/...             # format all .basl files recursively
basl fmt --check script.basl   # exit 1 if not formatted (for CI)
```

No configuration. No options beyond `--check`. One canonical style.

## Approach: Token-Preserving Rewrite

Parse source → AST → pretty-print back to source.

This is the `gofmt` approach. The AST already captures all structural information.
Comments are the one thing the AST doesn't preserve — they need special handling.

### Comment Handling

The lexer currently skips comments. Two options:

**Option A: Attach comments to tokens** — Modify the lexer to emit comment tokens,
then attach them to the nearest AST node during parsing. The formatter emits them
in the right position. This is what `gofmt` does.

**Option B: Line-based preservation** — Before formatting, extract all comments with
their line numbers. After formatting, re-insert them at the corresponding positions.
Simpler but fragile with reordering.

Recommendation: **Option A** — it's more work but correct. Add a `Comments` field to
relevant AST nodes (decls, stmts) that collects preceding comment tokens.

Decision: **Option A** — implemented via `NextTokenLine` attachment. Comments are
associated with the next non-comment token's line number, making placement stable
across reformatting passes.

## Formatting Rules

### Indentation
- 4 spaces, no tabs
- Each `{` increases indent by 1 level
- `}` decreases indent by 1 level

### Braces
- Opening `{` on same line as statement, preceded by a space
- Closing `}` on its own line at the outer indent level
- All block bodies are multi-line — no single-line statements

@kiro - formatter should always expand single-line statements into multi-line statements. There is ONE standard - I don't like single-line statements, they hinder readability, and for a human, it is easy to mentally filter out if/else ceremony and error handling - I like line of sight programming, skimming vertically down the page - NO SINGLE LINE STATEMENTS IN FMT

### Spacing
- One space after keywords: `if (`, `while (`, `for (`, `return `
- One space around binary operators: `a + b`, `x == 0`, `a && b`
- No space after unary operators: `!flag`, `-x`
- No space before semicolons: `return 0;`
- One space after commas: `fn foo(i32 a, i32 b)`
- No space inside parentheses: `(a + b)` not `( a + b )`
- One space before `{`: `if (x) {` not `if (x){`
- Space around `->`: `fn foo() -> i32 {`

### Blank Lines
- One blank line between top-level declarations (fn, class, interface, enum, const)
- No blank line between consecutive imports
- One blank line after the import block
- No more than one consecutive blank line anywhere
- No trailing blank lines at end of file

### Imports
- One import per line: `import "fmt";`
- Grouped at top of file, no blank lines between them
- Sorted alphabetically

### Functions
```c
fn function_name(i32 param1, string param2) -> i32 {
    // body
}
```

### Classes
```c
class ClassName {
    pub string field1;
    pub i32 field2;

    fn init(string field1, i32 field2) -> void {
        self.field1 = field1;
        self.field2 = field2;
    }

    fn method() -> string {
        return self.field1;
    }
}
```
- Blank line between fields and methods
- Blank line between methods
- All method bodies are multi-line

### Control Flow
```c
if (condition) {
    // body
} else {
    // body
}

while (condition) {
    // body
}

for (i32 i = 0; i < n; i++) {
    // body
}

for (item in collection) {
    // body
}

switch (expr) {
    case val1:
        // body
    case val2:
        // body
    default:
        // body
}
```
- `} else {` on same line (no newline before `else`)
- Switch cases indented one level inside switch

### Expressions
- No unnecessary parentheses (but don't remove user parens — they may be for clarity)
- Long expressions: no auto-wrapping (keep user's line breaks)

### Line Length
- No hard limit enforced

### Trailing Whitespace
- Removed from all lines

### File Ending
- Single newline at end of file

## Implementation Plan

### Phase 1: Comment-preserving lexer

**File: `pkg/basl/lexer/lexer.go`**

Add `TOKEN_COMMENT` and `TOKEN_BLOCK_COMMENT` token types. Instead of skipping
comments in `skipLineComment`/`skipBlockComment`, emit them as tokens.

Add a `TokenizeWithComments() ([]Token, error)` method that returns all tokens
including comments. The existing `Tokenize()` continues to strip them (parser
doesn't need them).

### Phase 2: Comment attachment in parser

**File: `pkg/basl/ast/ast.go`**

Add `LeadingComments []string` to `Program`, `FnDecl`, `ClassDecl`, `VarDecl`,
`ImportDecl`, `ConstDecl`, `EnumDecl`, `InterfaceDecl`, and statement nodes.

**File: `pkg/basl/parser/parser.go`**

When parsing with comments, collect comment tokens and attach them to the next
AST node parsed.

### Phase 3: AST printer

**New file: `pkg/basl/formatter/formatter.go`**

The core formatter. Takes a `*ast.Program` and produces formatted source as `[]byte`.

```go
type Formatter struct {
    buf    bytes.Buffer
    indent int
}

func Format(prog *ast.Program) []byte
```

Walk the AST, emit each node with proper indentation and spacing.
This is the bulk of the work — one `emit` method per AST node type.

### Phase 4: CLI integration

**File: `cmd/basl/main.go`**

Add `fmt` subcommand:

```go
case "fmt":
    runFmt(args[1:])
```

`runFmt` handles:
- Single file: read → lex (with comments) → parse → format → write back
- `./path/...`: walk directory, format all `.basl` files
- `--check`: compare formatted output to original, exit 1 if different

### Phase 5: Idempotency test

Format every `.basl` file in the repo. Format again. Verify output is identical.
This is the key correctness test — formatting must be idempotent.

```sh
basl fmt ./examples/...
basl fmt --check ./examples/...  # must exit 0
```

## Execution Order

| Step | What | Risk |
|---|---|---|
| 1 | Comment-preserving lexer | Low — additive, doesn't break existing |
| 2 | Comment attachment | Low — new parser mode |
| 3 | AST printer | High — bulk of work, must handle every node |
| 4 | CLI integration | Low |
| 5 | Idempotency test | Catches bugs |

## Not In Scope

- Configuration / style options (intentionally — one style)
- Auto-wrapping long lines
- Removing or adding parentheses in expressions
- Reordering anything other than imports
