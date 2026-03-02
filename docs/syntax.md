# BASL Language Syntax

BASL (Blazingly Awesome Scripting Language) is a statically-typed, C-syntax scripting language. It prioritizes readability, predictability, and explicitness. Every program looks the same — there is one way to do anything.

## Program Structure

Every BASL program has a `main` function that returns `i32`:

```c
fn main() -> i32 {
    return 0;
}
```

Statements end with semicolons. Blocks use braces.

## Imports

```c
import "fmt";
import "json";
import "file" as fs;              // alias
import "../shared/utils";         // relative path (alias: utils)
```

Imports resolve to built-in stdlib modules or `.basl` files relative to the script's directory.
Modules export only `pub` declarations.

## Types

### Primitive Types

| Type     | Description                  | Example          |
|----------|------------------------------|------------------|
| `i32`    | 32-bit signed integer        | `42`, `-1`       |
| `i64`    | 64-bit signed integer        | `i64(100000)`    |
| `f64`    | 64-bit float                 | `3.14`, `1e6`    |
| `u8`     | Unsigned byte                | `u8(255)`        |
| `u32`    | 32-bit unsigned integer      | `u32(42)`        |
| `u64`    | 64-bit unsigned integer      | `u64(42)`        |
| `bool`   | Boolean                      | `true`, `false`  |
| `string` | UTF-8 string                 | `"hello"`        |
| `err`    | Error value                  | `err("failed")`  |
| `void`   | No value                     |                  |

### Composite Types

```c
array<i32> nums = [1, 2, 3];
array<string> names = ["alice", "bob"];
map<string, i32> scores = {"alice": 95, "bob": 87};
```

Nested generics work: `array<array<string>>`.

### Function Types

```c
fn callback = some_function;                  // any callable
fn(i32, i32) -> i32 op = add;                // typed signature
fn(string) -> void handler = print_message;   // void return
```

Function signature types check param count, param types, and return type.
Bare `fn` accepts any callable without signature checking.

### Type Enforcement

All type annotations are enforced at runtime. Mismatches produce clear errors:

```
error: line 3: type mismatch — expected i32, received string
```

Enforcement points:
- Variable declarations and assignments
- Function parameters (including function signature types)
- Return statements
- Tuple bindings
- Class field assignments
- Collection mutations (`push`, `set`, index assignment)
- Collection literals (`array<i32>` checks each element, `map<K,V>` checks keys and values)

### Integer Literals

```c
i32 dec = 255;
i32 hex = 0xFF;
i32 bin = 0b11111111;
i32 oct = 0o377;
```

### String Literals

```c
string s = "hello\nworld";     // escape sequences: \n \t \\ \"
string raw = `no \n escapes`;  // backtick: raw/multi-line, no escape processing
string msg = f"hello {name}";  // f-string: expressions in {} are evaluated
```

### Character Literals

Single-character strings can be written with single quotes for clarity:

```c
string ch = 'a';               // equivalent to "a"
string newline = '\n';         // escape sequences work
if (ch == 'x') { ... }         // clearer than ch == "x"

// UTF-8 characters work
string euro = '€';
string emoji = '😀';
```

Character literals are syntactic sugar — they create single-character strings, not a separate type. Supported escapes: `\n`, `\t`, `\r`, `\\`, `\'`.

The formatter preserves character literal syntax for single-character strings, except in map literals where keys are always formatted with double quotes for consistency:

```c
string ch = 'a';               // formatted as 'a'
map<string, i32> m = {'a': 1}; // formatted as {"a": 1}
```

### String Interpolation

F-strings evaluate expressions inside `{}` and convert them to strings:

```c
string name = "Alice";
i32 age = 30;
fmt.println(f"Name: {name}, Age: {age}");
fmt.println(f"Next year: {age + 1}");
fmt.println(f"Upper: {name.to_upper()}");
```

Format specifiers follow the expression after `:`, using Go fmt verbs:

```c
f64 pi = 3.14159;
fmt.println(f"pi={pi:.2f}");     // pi=3.14
fmt.println(f"pi={pi:.4f}");     // pi=3.1416
fmt.println(f"x={42:05d}");      // x=00042
```

Escape sequences (`\n`, `\t`, `\\`, `\"`) work in f-strings. Use `{{` and `}}` for literal braces:

```c
fmt.println(f"value\t{x}");       // tab before value
fmt.println(f"set: {{1, 2, 3}}"); // prints: set: {1, 2, 3}
```

## Variables

```c
i32 x = 10;
string name = "basl";
bool flag = true;
array<i32> nums = [1, 2, 3];
```

All variables must be initialized at declaration. There is no `null`.

### Constants

```c
const i32 MAX = 100;
const string VERSION = "1.0";
```

Constants cannot be reassigned. Works at top-level and inside functions.

## Assignment

```c
x = 42;
x += 5;       // compound: +=, -=, *=, /=, %=
x++;           // increment (statement, not expression)
x--;           // decrement
arr[0] = 99;   // index assignment
m["key"] = 1;  // map key assignment
obj.field = v; // field assignment
```

## Operators

### Arithmetic
`+`, `-`, `*`, `/`, `%`

### Comparison
`==`, `!=`, `<`, `>`, `<=`, `>=`

- Numeric types: standard arithmetic comparison
- Strings: lexicographic (byte-wise) comparison
  - `"a" < "b"` is `true`
  - `"apple" < "banana"` is `true`
  - Useful for character ranges: `ch >= "a" && ch <= "z"`
  - UTF-8 safe: compares byte values, not locale-aware
- Booleans: only `==` and `!=` supported
- `err` type: only `==` and `!=` supported

### Logical
`&&`, `||`, `!`

Short-circuit evaluation: `&&` stops on false, `||` stops on true.

### Ternary Operator
```c
condition ? trueValue : falseValue
```

The condition must be a boolean expression. Returns `trueValue` if condition is true, otherwise `falseValue`.

The ternary operator has the lowest precedence of all expression operators (lower than `||`, `&&`, arithmetic, etc.), so it typically requires parentheses when used as a subexpression.

The operator is right-associative, allowing chained ternaries without parentheses.

```c
i32 max = a > b ? a : b;
string status = age >= 18 ? "adult" : "minor";

// Chained ternary (right-associative, no parentheses needed)
string size = x < 10 ? "small" : x < 100 ? "medium" : "large";

// Ternary as subexpression requires parentheses
i32 result = 10 + (flag ? 1 : 2);
i32 negated = -(flag ? 1 : 2);
```

### Bitwise
`&` (AND), `|` (OR), `^` (XOR), `~` (NOT), `<<` (shift left), `>>` (shift right)

### String Concatenation
```c
string s = "hello" + " " + "world";
s += "!";
```

## Functions

```c
fn add(i32 a, i32 b) -> i32 {
    return a + b;
}

fn greet(string name) -> void {
    fmt.println("hello " + name);
}
```

### Multi-Return

Functions can return multiple values:

```c
fn divide(i32 a, i32 b) -> (i32, err) {
    if (b == 0) {
        return (0, err("division by zero"));
    }
    return (a / b, ok);
}
```

Callers bind all return values:

```c
i32 result, err e = divide(10, 3);
```

Discard with `_`:

```c
i32 result, err _ = divide(10, 3);
```

### Functions as Values

Named functions can be passed as arguments and stored in variables:

```c
fn double(i32 x) -> i32 { return x * 2; }

fn apply(fn f, i32 x) -> i32 {
    return f(x);
}

fn main() -> i32 {
    i32 result = apply(double, 5);  // 10
    fn op = double;                 // store in variable
    return 0;
}
```

There are no anonymous functions or lambdas. Only named functions.

### Local Functions

Functions can be defined inside other functions:

```c
fn main() -> i32 {
    fn helper(i32 x) -> i32 {
        return x * 2;
    }
    i32 r = helper(5);
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
    // ...
}
```

### for (C-style)

```c
for (i32 i = 0; i < 10; i++) {
    fmt.println(fmt.sprintf("%d", i));
}
```

Compound assignment and `++`/`--` work in the post expression.

### for-in

```c
// Arrays
for val in arr {
    fmt.println(val);
}

// Maps — key and value
for key, val in m {
    fmt.println(key);
}

// Maps — value only
for val in m {
    fmt.println(val);
}
```

### switch

The tag and case values are arbitrary expressions, not limited to integers or characters. Strings, booleans, enums, and computed expressions all work:

```c
switch (x) {
    case 1:
        fmt.println("one");
    case 2, 3:
        fmt.println("two or three");
    default:
        fmt.println("other");
}

switch (name) {
    case "alice", "bob":
        fmt.println("known");
    default:
        fmt.println("unknown");
}

switch (x + y) {
    case compute():
        fmt.println("match");
}
```

No fallthrough — each case is independent (Go-style semantics, C-style syntax). Multiple values per case separated by commas.

Matching uses strict type equality — there is no truthiness. A string cannot match an integer, and a non-zero integer does not match `true`. Types must match exactly, then values are compared.

### break / continue

Work in `for`, `while`, and `for-in` loops.

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

## Enums

```c
enum Color {
    Red,      // 0
    Green,    // 1
    Blue      // 2
}

enum HttpStatus {
    Ok = 200,
    NotFound = 404,
    Error = 500
}
```

Access variants with dot notation: `Color.Red`, `HttpStatus.Ok`.

Enum values are `i32`. Use them in switch statements:

```c
switch (status) {
    case HttpStatus.Ok:
        fmt.println("success");
    case HttpStatus.NotFound:
        fmt.println("not found");
}
```

## Interfaces

Interfaces define a contract — a set of method signatures that a class must implement.

```c
interface Drawable {
    fn draw() -> void;
    fn get_name() -> string;
}
```

Classes opt in with `implements`:

```c
class Circle implements Drawable {
    pub string label;

    fn init(string label) -> void {
        self.label = label;
    }

    fn draw() -> void {
        fmt.println("drawing circle");
    }

    fn get_name() -> string {
        return self.label;
    }
}
```

A class can implement multiple interfaces:

```c
class Sprite implements Drawable, Updatable {
    // must have all methods from both interfaces
}
```

Interface types work in variable declarations, function parameters, and arrays:

```c
fn render(Drawable d) -> void {
    d.draw();
}

array<Drawable> objects = [Circle("c1"), Square("s1")];
for obj in objects {
    obj.draw();
}
```

Missing a required method is an error at class registration time.

## Classes

```c
class Point {
    pub i32 x;
    pub i32 y;

    fn init(i32 x, i32 y) -> void {
        self.x = x;
        self.y = y;
    }

    fn distance() -> f64 {
        return math.sqrt(f64(self.x * self.x + self.y * self.y));
    }
}
```

BASL has no class inheritance. Use interfaces for polymorphism and composition for code reuse.

Instantiate by calling the class name:

```c
Point p = Point(3, 4);
f64 d = p.distance();
```

### Module Classes

Classes from modules must use module-qualified type names:

```c
import "models";

// Correct: module-qualified type
models.Point p = models.Point(3, 4);

// Error: unqualified type doesn't match module class
Point p = models.Point(3, 4);  // type mismatch
```

This ensures type safety and prevents ambiguity when multiple modules define classes with the same name.

### Fallible Construction

If `init` returns `err`, construction uses tuple binding:

```c
class Connection {
    fn init(string host) -> err {
        // ...
        return ok;
    }
}

Connection c, err e = Connection("localhost");
if (e != ok) {
    return 1;
}
```

## Error Handling

BASL has no exceptions. Errors are values returned explicitly.

```c
string data, err e = file.read_all("config.txt");
if (e != ok) {
    fmt.println("failed to read file");
    return 1;
}
```

The `err` type has two states: `ok` for success, or `err("message")` for failure. `ok` is a reserved keyword. Stdlib functions return `err` as the last value in multi-return.

### Error Methods

| Method          | Returns | Description                    |
|-----------------|---------|--------------------------------|
| `e.message()`   | `string`| Get error message              |
| `e.is_eof()`    | `bool`  | Check if error is EOF          |

**Note:** `is_eof()` only returns true for EOF errors created by stdlib I/O functions. User-created `err("EOF")` values are not treated as EOF.

```c
string line, err e = io.read_line();
if (e != ok) {
    if (e.is_eof()) {
        fmt.println("end of input");
    } else {
        fmt.eprintln(f"error: {e.message()}");
    }
}

// User-created err("EOF") is NOT treated as EOF
err e2 = err("EOF");
bool is_eof = e2.is_eof();  // false
```

## defer

Defers a function call until the enclosing function returns. LIFO order. Arguments are evaluated eagerly.

```c
File f, err e = file.open("data.txt", "r");
defer f.close();
// f.close() runs when this function returns
```

## Type Conversions

Explicit only — no implicit conversions:

```c
i32 x = 42;
f64 y = f64(x);           // numeric conversion
string s = string(x);     // to string
i32 n, err e = i32("42"); // string to number (can fail)
```

## String Methods

| Method                        | Returns              | Description                    |
|-------------------------------|----------------------|--------------------------------|
| `s.len()`                     | `i32`                | Byte length                    |
| `s.contains(sub)`             | `bool`               | Contains substring             |
| `s.starts_with(prefix)`       | `bool`               | Starts with prefix             |
| `s.ends_with(suffix)`         | `bool`               | Ends with suffix               |
| `s.trim()`                    | `string`             | Trim whitespace                |
| `s.to_upper()`                | `string`             | Uppercase                      |
| `s.to_lower()`                | `string`             | Lowercase                      |
| `s.replace(old, new)`         | `string`             | Replace all occurrences        |
| `s.split(sep)`                | `array<string>`      | Split by separator             |
| `s.index_of(sub)`             | `(i32, bool)`        | Find index of substring        |
| `s.substr(start, len)`        | `(string, err)`      | Extract substring              |
| `s.bytes()`                   | `array<u8>`          | Convert to byte array          |
| `s.char_at(i)`                | `(string, err)`      | Single character at index      |

## Array Methods

| Method                        | Returns              | Description                    |
|-------------------------------|----------------------|--------------------------------|
| `a.len()`                     | `i32`                | Number of elements             |
| `a.push(val)`                 | `void`               | Append element                 |
| `a.pop()`                     | `(T, err)`           | Remove and return last element |
| `a.get(i)`                    | `(T, err)`           | Get element by index           |
| `a.set(i, val)`               | `err`                | Set element by index           |
| `a.slice(start, end)`         | `array<T>`           | Sub-array [start, end)         |
| `a.contains(val)`             | `bool`               | Check if value exists          |

Index access: `a[i]` for reading, `a[i] = val` for writing.

## Map Methods

| Method                        | Returns              | Description                    |
|-------------------------------|----------------------|--------------------------------|
| `m.len()`                     | `i32`                | Number of entries              |
| `m.get(key)`                  | `(T, bool)`          | Get value by key               |
| `m.set(key, val)`             | `err`                | Set key-value pair             |
| `m.remove(key)`               | `(T, bool)`          | Remove and return value        |
| `m.has(key)`                  | `bool`               | Check if key exists            |
| `m.keys()`                    | `array<K>`           | All keys                       |
| `m.values()`                  | `array<V>`           | All values                     |

Index access: `m["key"]` for reading, `m["key"] = val` for writing.

## Unsafe Features

Pointers, null, raw memory, C struct layouts, and FFI callbacks require `import "unsafe"`:

```c
import "unsafe";
import "ffi";

ffi.Lib lib = ffi.load("libSDL3.dylib");
fn sdl_init = ffi.bind(lib, "SDL_Init", "i32", ["i32"]);
```

See `design.md` for full unsafe/FFI documentation.

## Design Principles

- **One way to do anything.** No operator overloading, no multiple inheritance, no implicit conversions.
- **No null** in safe code. Missing values use multi-return: `(T, err)` or `(T, bool)`.
- **No exceptions.** Errors are values.
- **No anonymous functions.** Only named functions, passed by reference.
- **No implicit conversions.** All type conversions are explicit.
- **No truthiness.** `if` requires `bool`. `if x` where x is `i32` is a compile error.
- **Unsafe is gated.** Pointers, FFI, and raw memory require `import "unsafe"`.
