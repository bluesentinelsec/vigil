# BASL Syntax

BASL is a statically typed, C-style scripting language with explicit control flow, explicit errors, and a small set of consistent language forms. This document describes the language as implemented today.

## Source Files

- BASL source files use the `.basl` extension.
- Statements end with `;`.
- Blocks use `{ ... }`.
- Programs typically expose a `main` entrypoint:

```c
fn main() -> i32 {
    return 0;
}
```

`main` is conventional for runnable programs. Library modules export `pub` declarations instead.

## Comments

```c
// line comment

/*
block comment
*/
```

## Imports

```c
import "fmt";
import "json";
import "file" as fs;
import "../shared/utils";
```

- Imports resolve to stdlib modules or other `.basl` modules.
- `as` sets the local module name explicitly.
- Without `as`, the alias is the last path component.
- Only `pub` declarations are exported from a module.

Typical resolution behavior:
- Built-in stdlib modules are always available by name.
- Script execution searches from the current script or project root.
- BASL projects also search conventional `lib/` and `deps/` locations.

## Top-Level Declarations

Top-level BASL files may contain:

- `import`
- `fn`
- `class`
- `interface`
- `enum`
- `const`
- typed variable declarations

Most top-level declarations can be exported with `pub`:

```c
pub fn greet() -> void {}
pub class Person {}
pub interface Greeter {}
pub enum Status { Ok, Err }
pub const string VERSION = "1.0";
pub i32 answer = 42;
```

## Types

### Primitive Types

| Type | Notes |
|---|---|
| `bool` | `true`, `false` |
| `i32` | 32-bit signed integer |
| `i64` | 64-bit signed integer |
| `f64` | 64-bit float |
| `u8` | unsigned byte |
| `u32` | 32-bit unsigned integer |
| `u64` | 64-bit unsigned integer |
| `string` | UTF-8 string |
| `err` | explicit error value |
| `void` | no value |

There is no `null`.

### Composite Types

```c
array<i32> nums = [1, 2, 3];
map<string, i32> scores = {"alice": 95, "bob": 87};
array<array<string>> table = [["a"], ["b"]];
```

### Function Types

```c
fn worker(i32 x) -> void {}
fn add(i32 a, i32 b) -> i32 { return a + b; }
fn log_name(string name) -> void {}

fn main() -> i32 {
    fn(i32, i32) -> i32 op = add;
    fn(string) handler = log_name;
    return 0;
}
```

- `fn(...) -> ...` enforces parameter and return shape.
- Indirect calls require a concrete function signature.

### Module-Qualified Types

Classes imported from another module use module-qualified names:

```c
import "models";

models.Point p = models.Point(3, 4);
```

## Literals

### Numeric Literals

```c
i32 dec = 255;
i32 hex = 0xFF;
i32 bin = 0b11111111;
i32 oct = 0o377;
f64 pi = 3.14159;
f64 large = 1e6;
```

### String Literals

```c
string normal = "hello\nworld";
string raw = `no escapes here`;
string name = "Alice";
string msg = f"hello {name}";
```

### Character Literals

Single-quoted character literals are single-character strings:

```c
string ch = 'a';
string newline = '\n';
string euro = '€';
```

They are syntax sugar for one-character `string` values, not a separate type.

### F-Strings

F-strings support interpolation and optional format specifiers:

```c
string name = "Alice";
i32 age = 30;
f64 pi = 3.14159;

fmt.println(f"Name: {name}, Age: {age}");
fmt.println(f"Next year: {age + 1}");
fmt.println(f"pi={pi:.2f}");
fmt.println(f"literal braces: {{ok}}");
```

### Array and Map Literals

```c
array<string> names = ["alice", "bob"];
map<string, i32> counts = {"a": 1, "b": 2};
```

## Variables and Constants

### Variable Declarations

All variables are typed and initialized at declaration:

```c
i32 x = 10;
string name = "basl";
bool ready = true;
```

### Constants

```c
const i32 MAX = 100;
const string VERSION = "1.0";
```

Constants can appear at top level and inside functions.

### Tuple Bindings

Functions may return multiple values, which are bound explicitly:

```c
i32 result, err e = divide(10, 2);
i32 value, err _ = divide(10, 2);
```

`_` discards a value.

## Assignment

```c
x = 42;
x += 5;
x -= 1;
x *= 2;
x /= 3;
x %= 2;
x++;
x--;
arr[0] = 99;
m["key"] = 1;
obj.field = 7;
```

`++` and `--` are statements, not expressions.

## Expressions and Operators

### Arithmetic

`+`, `-`, `*`, `/`, `%`

### Bitwise

`&`, `|`, `^`, `~`, `<<`, `>>`

### Comparison

`==`, `!=`, `<`, `>`, `<=`, `>=`

### Logical

`&&`, `||`, `!`

Logical operators short-circuit.

### Ternary

```c
i32 max = a > b ? a : b;
string size = x < 10 ? "small" : x < 100 ? "medium" : "large";
i32 delta = 10 + (flag ? 1 : 2);
```

- The condition must be `bool`.
- The ternary operator is right-associative.
- It has the lowest precedence.

### Member, Index, and Call Expressions

```c
fmt.println("hi");
person.name;
arr[0];
m["answer"];
worker(42);
```

### Type Conversions

Conversions are explicit:

```c
i32 x = 42;
f64 y = f64(x);
string s = string(x);
```

For parsing strings into numeric or boolean values, use the `parse` module:

```c
import "parse";

fn main() -> i32 {
    i32 n, err e = parse.i32("42");
    return 0;
}
```

### Type Rules

BASL does not perform implicit numeric coercion. Mixed numeric operations must be cast explicitly:

```c
i32 a = 10;
i64 b = i64(20);

if (i64(a) < b) {
    fmt.println("ok");
}
```

## Functions

### Function Declarations

```c
fn add(i32 a, i32 b) -> i32 {
    return a + b;
}

fn greet(string name) -> void {
    fmt.println("hello " + name);
}
```

### Multiple Return Values

```c
fn divide(i32 a, i32 b) -> (i32, err) {
    if (b == 0) {
        return (0, err("division by zero", err.arg));
    }
    return (a / b, ok);
}
```

### Anonymous Functions and Closures

```c
fn main() -> i32 {
    i32 factor = 10;

    fn scale = fn(i32 x) -> i32 {
        return x * factor;
    };

    fn() -> void {
        fmt.println("iife");
    }();

    return 0;
}
```

Anonymous functions capture surrounding variables.

### Local Functions

Named local functions are also supported:

```c
fn main() -> i32 {
    fn helper(i32 x) -> i32 {
        return x * 2;
    }

    i32 result = helper(5);
    return 0;
}
```

## Control Flow

### if / else if / else

```c
if (x > 10) {
    fmt.println("big");
} else if (x > 5) {
    fmt.println("medium");
} else {
    fmt.println("small");
}
```

Parentheses around the condition are required.

### while

```c
while (running) {
    tick();
}
```

### C-Style for

```c
for (i32 i = 0; i < 10; i++) {
    fmt.println(string(i));
}
```

### for-in

```c
for val in arr {
    fmt.println(val);
}

for key, val in m {
    fmt.println(key + "=" + string(val));
}
```

### switch

```c
switch (x) {
    case 1:
        fmt.println("one");
    case 2, 3:
        fmt.println("two or three");
    default:
        fmt.println("other");
}
```

- Cases may contain multiple values.
- There is no fallthrough.
- Matching uses explicit value equality, not truthiness.

### break and continue

```c
for (i32 i = 0; i < 100; i++) {
    if (i == 50) {
        break;
    }
    if (i % 2 == 0) {
        continue;
    }
}
```

### defer

`defer` schedules a call to run when the enclosing function returns.

```c
fn cleanup() -> void {}

fn main() -> i32 {
    defer cleanup();
    return 0;
}
```

- Deferred calls run in LIFO order.
- Arguments are evaluated eagerly.

### guard

`guard` is shorthand for “bind values, then immediately handle the error case”.

```c
guard string data, err e = file.read_all("config.txt") {
    fmt.eprintln(f"read failed: {e.message()}");
    return 1;
}
```

Rules:
- `guard` must end with an `err` binding.
- The final `err` binding must be named, not `_`.
- The body runs only when the error is not `ok`.

Equivalent expanded form:

```c
string data, err e = file.read_all("config.txt");
if (e != ok) {
    fmt.eprintln(f"read failed: {e.message()}");
    return 1;
}
```

## Errors

BASL has no exceptions. Errors are values.

### Success and Failure

```c
return ok;
return err("file not found", err.not_found);
```

Stdlib fallible APIs usually return `err` as the final value in a multi-return result.

### Inspecting Errors

```c
if (e != ok) {
    fmt.eprintln(e.message());
    fmt.eprintln(e.kind());
}
```

### Standard Error Kinds

Common built-in error kinds include:

- `err.not_found`
- `err.permission`
- `err.exists`
- `err.eof`
- `err.io`
- `err.parse`
- `err.bounds`
- `err.type`
- `err.arg`
- `err.timeout`
- `err.closed`
- `err.state`

### Routing by Kind

```c
if (e != ok) {
    switch (e.kind()) {
        case err.not_found:
            fmt.println("missing");
        case err.permission:
            fmt.println("denied");
        default:
            fmt.eprintln(f"error: {e.message()}");
    }
}
```

## Classes

```c
class Person {
    pub string name;
    pub i32 age;

    fn init(string name, i32 age) -> void {
        self.name = name;
        self.age = age;
    }

    pub fn greet() -> void {
        fmt.println(f"Hello, I'm {self.name}");
    }
}
```

### Construction

Instantiate a class by calling its name:

```c
Person p = Person("Alice", 30);
p.greet();
```

If `init` returns `err`, construction becomes fallible:

```c
class Connection {
    fn init(string host) -> err {
        return ok;
    }
}

fn main() -> i32 {
    Connection c, err e = Connection("localhost");
    return 0;
}
```

### Notes

- `self` refers to the current instance.
- BASL has no class inheritance.
- Use interfaces plus composition for polymorphism.

## Interfaces

```c
interface Drawable {
    fn draw() -> void;
    fn name() -> string;
}

class Circle implements Drawable {
    pub string label;

    fn init(string label) -> void {
        self.label = label;
    }

    fn draw() -> void {
        fmt.println("drawing");
    }

    fn name() -> string {
        return self.label;
    }
}
```

- Classes may implement multiple interfaces.
- Interface conformance is explicit via `implements`.

## Enums

```c
enum Color {
    Red,
    Green,
    Blue
}

enum HttpStatus {
    Ok = 200,
    NotFound = 404,
    Error = 500
}
```

Use enum members with dot syntax and the enum's own type:

```c
Color c = Color.Red;
```

Enum values are `i32`-backed internally and work naturally in `switch`:

```c
switch (c) {
    case Color.Red:
        // ...
    case Color.Green:
        // ...
    default:
        // ...
}
```

## Common Built-In Collection and String Methods

These are heavily used and part of everyday BASL style.

### String

| Method | Returns |
|---|---|
| `s.len()` | `i32` |
| `s.contains(sub)` | `bool` |
| `s.starts_with(prefix)` | `bool` |
| `s.ends_with(suffix)` | `bool` |
| `s.trim()` | `string` |
| `s.to_upper()` | `string` |
| `s.to_lower()` | `string` |
| `s.replace(old, new)` | `string` |
| `s.split(sep)` | `array<string>` |
| `s.index_of(sub)` | `(i32, bool)` |
| `s.substr(start, len)` | `(string, err)` |
| `s.bytes()` | `array<u8>` |
| `s.char_at(i)` | `(string, err)` |
| `s.trim_left()` | `string` |
| `s.trim_right()` | `string` |
| `s.reverse()` | `string` |
| `s.is_empty()` | `bool` |
| `s.repeat(n)` | `string` |
| `s.count(sub)` | `i32` |
| `s.last_index_of(sub)` | `(i32, bool)` |
| `s.trim_prefix(prefix)` | `string` |
| `s.trim_suffix(suffix)` | `string` |

### Array

| Method | Returns |
|---|---|
| `a.len()` | `i32` |
| `a.push(val)` | `void` |
| `a.pop()` | `(T, err)` |
| `a.get(i)` | `(T, err)` |
| `a.set(i, val)` | `err` |
| `a.slice(start, end)` | `array<T>` |
| `a.contains(val)` | `bool` |

### Map

| Method | Returns |
|---|---|
| `m.len()` | `i32` |
| `m.get(key)` | `(T, bool)` |
| `m.set(key, val)` | `err` |
| `m.remove(key)` | `(T, bool)` |
| `m.has(key)` | `bool` |
| `m.keys()` | `array<K>` |
| `m.values()` | `array<V>` |

Index syntax is also supported:

```c
arr[0];
m["key"];
arr[0] = 1;
m["key"] = 2;
```

## Language Rules and Conventions

- No implicit conversions.
- No hidden error propagation.
- No truthiness: conditions must be `bool`.
- No `null`.
- Errors are explicit values.
- Prefer the BASL formatter for canonical layout: `basl fmt`.

For stdlib APIs, see [docs/stdlib/README.md](stdlib/README.md). For CLI behavior, see [docs/cli.md](cli.md).
