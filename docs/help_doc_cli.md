# BASL `doc` CLI Design

Design proposal for a `basl doc` subcommand that displays developer documentation for BASL source files and modules.

The goal is closer to `go doc` than `--help`: given a BASL script or module, show its public API in a clear, structured way.

## Summary

Add a new CLI subcommand:

```sh
basl doc <file.basl>
basl doc <file.basl> <symbol>
```

This command parses a BASL source file and displays:

1. public top-level symbols
2. public classes, interfaces, enums, constants, variables, and functions
3. public members on public classes
4. associated developer-facing documentation comments

This is not runtime CLI help for a script. It is source-level API documentation.

## Goals

1. Show public API from a BASL file in a predictable format
2. Make BASL modules self-documenting for other developers
3. Follow a simple convention similar to `go doc`
4. Avoid executing BASL code
5. Keep the first version narrow and easy to implement

## Non-Goals

1. Full generated HTML docs
2. Cross-package website generation
3. Evaluating code to infer docs
4. Rich doxygen-style tag parsing in the MVP
5. Private/internal symbol documentation by default

## CLI Shape

### Basic file docs

```sh
basl doc ./examples/modtest/mymath.basl
```

Shows a module overview and all public symbols.

### Focused symbol docs

```sh
basl doc ./examples/modtest/mymath.basl add
basl doc ./examples/modtest/point.basl Point
basl doc ./examples/modtest/point.basl Point.distance
```

Shows one symbol or one member in detail.

### Optional future forms

Not required in MVP, but the design should leave room for:

```sh
basl doc ./dir/module.basl --json
basl doc ./dir/module.basl --all
```

## Input Model

The initial implementation should document a single BASL file.

That file may represent:

1. a reusable module
2. a script with exports
3. a library entrypoint

The command should parse the file directly, not resolve and walk transitive imports in the MVP.

## What Counts as Public

BASL already uses `pub` for exports. `basl doc` should document only public API by default.

### Include

1. `pub fn`
2. `pub class`
3. `pub interface`
4. `pub enum`
5. `pub const`
6. `pub` top-level variables
7. `pub` fields on a `pub class`
8. `pub` methods on a `pub class`

### Exclude

1. non-`pub` top-level declarations
2. non-`pub` class fields
3. non-`pub` class methods
4. local functions
5. implementation details inside method bodies

## Output Organization

The output should be logical and easy to scan.

Recommended section order:

1. File/module heading
2. Module summary
3. Constants
4. Variables
5. Enums
6. Interfaces
7. Classes
8. Functions

This ordering matches how developers usually consume an API:

1. configuration/constants first
2. types next
3. executable entrypoints last

## Example Output

For:

```c
// Geometry primitives and helpers.
pub class Point {
    // X coordinate in world space.
    pub i32 x;

    // Y coordinate in world space.
    pub i32 y;

    // Build a point from x and y.
    pub fn init(i32 x, i32 y) -> void {
        self.x = x;
        self.y = y;
    }

    // Returns Manhattan distance from origin.
    pub fn manhattan() -> i32 {
        return self.x + self.y;
    }
}

// Adds two numbers.
pub fn add(i32 a, i32 b) -> i32 {
    return a + b;
}
```

Suggested output:

```text
MODULE
  point

SUMMARY
  Geometry primitives and helpers.

CLASSES
  Point

    Geometry primitives and helpers.

    Fields
      x i32
        X coordinate in world space.
      y i32
        Y coordinate in world space.

    Methods
      init(i32 x, i32 y) -> void
        Build a point from x and y.
      manhattan() -> i32
        Returns Manhattan distance from origin.

FUNCTIONS
  add(i32 a, i32 b) -> i32
    Adds two numbers.
```

For a focused symbol query:

```sh
basl doc ./point.basl Point.manhattan
```

Suggested output:

```text
Point.manhattan() -> i32

Returns Manhattan distance from origin.
```

## Documentation Convention

The MVP should use a simple comment convention inspired by `go doc`.

### Declaration comments

A declaration is documented by consecutive `//` line comments immediately above it.

Example:

```c
// Adds two numbers.
// Returns the arithmetic sum of a and b.
pub fn add(i32 a, i32 b) -> i32 {
    return a + b;
}
```

Rules:

1. comments must be directly adjacent to the declaration
2. blank line breaks association
3. use only leading comments, not trailing comments
4. the first line is the short summary
5. additional lines are longer detail

### Class member comments

The same rule applies inside class bodies:

1. `//` block immediately above a `pub` field
2. `//` block immediately above a `pub fn` method

### File/module comment

The file summary should come from the first top-of-file contiguous `//` comment block before any declaration.

That block becomes the module summary.

## Style Guidance for Authors

To keep docs useful and consistent, encourage:

1. first sentence should be short and declarative
2. describe behavior, not implementation
3. use present tense
4. document error-return behavior where relevant
5. document units and invariants for fields

Recommended style:

```c
// Parses a decimal string into an i32.
// Returns err("...") if the input is not a valid integer.
pub fn parse_i32(string s) -> (i32, err) {
    // ...
}
```

## Supported Symbols

### Functions

Render:

1. name
2. full signature
3. doc summary/details

Example:

```text
add(i32 a, i32 b) -> i32
```

### Constants and Variables

Render:

1. name
2. declared type
3. kind (`const` or `var`)
4. doc summary/details

Do not evaluate initializer expressions for the MVP.

### Enums

Render:

1. enum name
2. doc summary/details
3. variant list
4. explicit values if present in source

Example:

```text
HttpStatus
  Ok = 200
  NotFound = 404
  Error = 500
```

### Interfaces

Render:

1. interface name
2. doc summary/details
3. required method signatures

### Classes

Render:

1. class name
2. implemented interfaces
3. doc summary/details
4. public fields
5. public methods

Important:

1. show only `pub` fields and methods
2. keep members grouped under the class
3. preserve source order

## Symbol Lookup Rules

### `basl doc <file>`

Show all public symbols in grouped sections.

### `basl doc <file> <symbol>`

Supported forms:

1. top-level symbol name: `add`
2. class name: `Point`
3. class member: `Point.manhattan`

Lookup should be exact and case-sensitive.

If the symbol is not found:

```text
error[doc]: symbol "Point.foo" not found in ./point.basl
```

## Parsing Strategy

This feature should not execute code.

Implementation should:

1. read the source file
2. lex with comments preserved (`TokenizeWithComments`)
3. lex again without comments for parsing
4. parse the AST
5. associate comment blocks with declarations by line position
6. render docs from the AST plus comment metadata

This reuses the existing parser and lexer and avoids unsafe evaluation.

## Comment Association Model

Because BASL already has comment tokens, the implementation can build a lightweight mapper:

1. collect all line-comment tokens
2. group contiguous comment lines into blocks
3. attach a block to the nearest following declaration/member if:
   1. there is no blank line between them
   2. no unrelated token/declaration intervenes

This is sufficient for MVP and matches common source-doc expectations.

## Suggested Internal Model

Recommended internal representation:

```go
type DocFile struct {
    ModuleSummary string
    Constants     []DocValue
    Variables     []DocValue
    Enums         []DocEnum
    Interfaces    []DocInterface
    Classes       []DocClass
    Functions     []DocFunc
}
```

And:

```go
type DocFunc struct {
    Name    string
    Sig     string
    Summary string
    Detail  []string
}
```

Equivalent small structs can be used for values, enums, interfaces, classes, fields, and methods.

## Rendering Rules

### Default text output

Text should be optimized for terminal reading.

Rules:

1. no ANSI color in MVP
2. consistent section headers
3. preserve declaration order inside each section
4. omit empty sections
5. use compact indentation

### Optional JSON output

Not required in MVP, but worth designing for later.

Machine-readable output would help:

1. editor integrations
2. static site generation later
3. tests

## Error Handling

Use `error[doc]:` for all command-specific failures.

Examples:

1. file not found
2. parse error
3. no public symbols
4. symbol not found

Examples:

```text
error[doc]: no public symbols found in ./internal.basl
error[doc]: symbol "add" not found in ./math.basl
```

## MVP Scope

The first version should implement only:

1. single-file input
2. public top-level symbol listing
3. public class member listing
4. symbol-focused lookup
5. leading `//` comment extraction
6. terminal text output

That is enough to be genuinely useful for developers without overbuilding.

## Future Extensions

Once the MVP works, extend carefully:

1. support documenting imported modules by import path
2. support package/module directory docs
3. support JSON output
4. support inherited docs if BASL ever gains inheritance
5. support richer structured tags like:
   1. `Returns:`
   2. `Errors:`
   3. `Example:`
6. support generated docs for the stdlib itself

## Recommendation

Implement `basl doc` as a static source documentation tool:

1. `basl doc <file.basl>` shows the file's public API
2. `basl doc <file.basl> <symbol>` shows a focused symbol view
3. use `pub` as the visibility filter
4. use adjacent `//` comments as the doc convention
5. group output by API kind for readability

This is small, coherent, aligned with BASL's current language model, and sufficient for real developer-facing documentation.
