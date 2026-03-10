# BASL Check

Detailed guide to `basl check`, BASL's non-executing static validation command.

`basl check` is designed for terminal and CI workflows where you want fast feedback about obvious structural and semantic problems before running a script or test suite.

## What It Does

`basl check` parses BASL source, builds a static model of the program, and reports diagnostics without executing user code.

That means it can catch many common errors early:

- Missing or broken imports
- Invalid references to symbols, module members, fields, and methods
- Incorrect function and method arity
- Multi-return shape mismatches
- Interface conformance problems
- Straightforward type mismatches
- A range of control-flow and statement-level mistakes

It does **not** run `main()`, evaluate top-level initializers, execute native code, or perform I/O on behalf of the checked program.

## Command Usage

```bash
basl check [--path dir] [file.basl|dir|./dir/...]
```

Examples:

```bash
basl check main.basl
basl check ./lib
basl check ./lib/...
basl check --path ./vendor main.basl
```

## Targets and File Discovery

`basl check` accepts:

- A single `.basl` file
- A directory, which is walked recursively for `.basl` files
- A recursive pattern ending in `/...`, such as `./lib/...`

All matching `.basl` files are checked in sorted order.

If no target is provided:

- When run from a BASL project root (a directory containing `basl.toml`), it defaults to:
  - `main.basl` if present
  - `lib/` if present
  - `test/` if present
- Otherwise it defaults to the current directory (`.`), recursively

If no BASL files are found, the command prints `no BASL files found` and exits successfully.

## Import Resolution

For each checked file, import search paths are resolved the same way script execution resolves file-backed modules:

1. The checked file's directory
2. If the file is inside a BASL project, the project `lib/` directory
3. If the file is inside a BASL project, the project `deps/` directory
4. Any extra directories passed with `--path`

This keeps `basl check` aligned with normal CLI execution and project layout expectations.

## Exit Codes

- `0`: no diagnostics
- `1`: at least one diagnostic, or a file-level checking error occurred
- `2`: invalid command usage (for example, `--path` without a directory)

Diagnostics are printed to `stderr`, one per line, typically in the form:

```text
/abs/path/file.basl:12: return value 1 expects i32, received string
```

## How It Works

The checker is conservative and non-executing. At a high level, it performs these phases:

### 1. Parse Source

Each file is lexed and parsed into an AST. Syntax errors are reported immediately.

### 2. Build the Import Graph

Imports are resolved across:

- Files in the current script directory
- Project `lib/`
- Project `deps/`
- Builtin modules

The checker loads imported modules statically and detects import cycles, reporting the cycle chain when one is found.

### 3. Collect Declarations

Before validating bodies, it collects top-level declarations so names can be checked consistently:

- imports
- functions
- variables and constants
- enums
- interfaces
- classes

This allows forward references within a module and enables duplicate-name checks.

### 4. Validate Modules and Function Bodies

After symbols are known, the checker validates:

- module-level initializers
- class declarations
- interface implementations
- function and method bodies

Validation is scope-aware and tracks local variable types through blocks and common control-flow forms.

### 5. Apply Builtin Knowledge

Builtin stdlib modules are modeled explicitly so `basl check` can validate common real-world scripts, not just user-defined code.

The checker currently models most non-graphics builtin modules, including:

- `fmt`, `os`, `path`, `io`, `file`, `regex`, `args`
- `parse`
- `math`, `strings`, `time`, `log`
- `base64`, `hex`, `hash`, `mime`, `csv`
- `archive`, `compress`, `crypto`
- `json`, `xml`, `http`
- `tcp`, `udp`, `sqlite`
- `thread`, `mutex`, `test`, `ffi`, `unsafe`, `gui`

This includes many builtin object types and methods such as:

- `json.Value`
- `xml.Value`
- `HttpRequest` / `HttpResponse`
- `TcpConn`, `TcpListener`, `UdpConn`
- `SqliteDB`, `SqliteRows`
- `Thread`, `Mutex`
- `gui.App`, `gui.Window`, `gui.Box`, `gui.Label`, `gui.Button`, `gui.Entry`
- `test.T`
- `ffi.Lib`, `ffi.Func`
- `unsafe.Buffer`, `unsafe.Layout`, `unsafe.Struct`, `unsafe.Callback`

## What It Detects

### Parse and File Errors

- Missing files or unreadable files
- Invalid target paths
- Lexer/parser errors

### Import and Module Errors

- Missing modules
- Import cycles, including the cycle chain
- Duplicate import aliases
- Missing module exports

### Declaration Errors

- Duplicate top-level names
- Duplicate class fields
- Duplicate class methods
- Duplicate `init` methods
- Duplicate interface methods

### Identifier and Member Errors

- Unknown identifiers
- Missing module members
- Missing object fields
- Missing object methods
- Invalid member access on primitive types

Examples:

```text
unknown identifier "rows"
module member "exec" not found
string has no member "push"
```

### Call-Site Errors

- Wrong number of arguments
- Wrong argument types in modeled calls
- Passing multi-return expressions where a single argument is required
- Incorrect constructor arity
- Optional-argument misuse on builtin APIs that have bounded optional parameters

Examples:

```text
add expects 2 arguments, got 1
flag expects 4 to 5 arguments, got 6
call arguments must be single values
```

### Return Shape and Tuple Binding Errors

- Returning the wrong number of values
- Returning values of incompatible types
- Binding multi-return values into the wrong number of variables
- Assigning a multi-return expression where a single value is required

Examples:

```text
return expects 2 values, got 1
tuple binding expects 3 values, got 2
variable x expects a single value, but the expression returns 2 values
```

### Type Mismatch Errors

The checker reports conservative, straightforward type mismatches in:

- Variable declarations
- Tuple bindings
- Assignments
- Module-level variable/constant initializers
- Returns
- Many builtin function and method calls

Examples:

```text
type mismatch in variable bad: expected string, received i32
type mismatch in assignment: expected array<string>, received array<i32>
return value 1 expects i32, received string
```

### Interface and Class Validation

- Unknown interfaces in `implements`
- Missing required methods
- Parameter count mismatches
- Parameter type mismatches
- Return type mismatches
- Interface assignability checks in straightforward cases

Examples:

```text
class Person missing method "greet" required by interface Greeter
class Person method greet return 1 has type i32, interface Greeter requires string
```

### Control-Flow and Statement Errors

- Non-`bool` conditions in `if`, `while`, `for`, and ternary expressions
- Non-indexable values used with `[]`
- Wrong index type for arrays or maps
- `for-in` used on values that are not arrays or maps
- Switch case values that do not match the switch tag type
- Non-void functions that may exit without returning

Examples:

```text
if condition must be bool, received string
array index must be i32, received string
for-in expects array or map, received string
function parse may exit without returning 2 values
```

### Operator Misuse

The checker validates common operator shapes:

- `!` expects `bool`
- unary `-` expects a numeric operand
- `&&` and `||` expect `bool`
- arithmetic operators expect compatible numeric operands
- `+` also allows `string + string`
- ordered comparisons report obviously incompatible types

Examples:

```text
operator ! expects bool, received i32
operator + expects matching numeric types or strings, received string and i32
operator && expects bool operands, left side is string
```

### Builtin-Specific Semantic Checks

Some builtins receive extra validation beyond raw arity:

- `thread.spawn(fn, ...)`
  - Checks the spawned function's parameter count and argument types
- `sort.by(array, fn comparator)`
  - Checks comparator shape (`fn(T, T) -> bool`) in straightforward cases
- `log.set_handler(fn handler)`
  - Checks for a `(string, string)` handler shape
- `http.listen(string addr, fn handler)`
  - Checks handler arity and `HttpRequest` parameter shape

This is meant to catch practical mistakes in real scripts that use higher-order stdlib APIs.

## Limitations

`basl check` is intentionally conservative. It is a strong static preflight tool, not a full compiler.

Current limitations include:

- It does not execute code, so value-dependent behavior is not proven
- Type inference is strongest in direct, obvious cases and weaker in dynamic or heavily flow-dependent code
- Some native APIs are modeled approximately rather than perfectly
- `ffi.Func.call(...)` is only loosely typed
- Graphics-oriented `rl` coverage is not the focus of the checker today
- It does not attempt full dataflow analysis, exhaustive reachability proofs, or optimizer-style reasoning

When the checker cannot prove a problem statically, it prefers to avoid inventing incorrect diagnostics.

## When To Use It

`basl check` is most useful:

- Before running a script with side effects
- In CI, to fail fast on structural mistakes
- While refactoring larger BASL projects
- Before packaging, to catch import and signature regressions early
- As a lightweight validation pass over examples and test suites

For runtime behavior, integration checks, and value-dependent logic, continue to use:

- `basl` to run scripts
- `basl test` for test execution
- `basl debug` for interactive troubleshooting
