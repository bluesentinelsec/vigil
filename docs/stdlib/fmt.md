# fmt

Formatted output. All functions operate on BASL's `.String()` representation of values.

```
import "fmt";
```

## Functions

### fmt.print(val) -> err

Prints the string representation of `val` to stdout. No trailing newline.

- Accepts exactly 1 argument.
- Calls `.String()` on the value internally.
- Returns `ok` on success.
- Error if argument count ≠ 1: `"fmt.print: expected 1 argument, got N"`.

```c
fmt.print("hello");       // prints: hello
fmt.print(42);             // prints: 42
fmt.print(true);           // prints: true
```

### fmt.println(val) -> err

Same as `fmt.print` but appends `\n` after the value.

- Accepts exactly 1 argument.
- Error if argument count ≠ 1: `"fmt.println: expected 1 argument, got N"`.

```c
fmt.println("hello");     // prints: hello\n
```

### fmt.sprintf(string format, ...args) -> string

Returns a formatted string using Go-style format verbs (`%d`, `%s`, `%f`, etc.).

- First argument must be a string format.
- Remaining arguments are variadic and passed to Go's `fmt.Sprintf`.
- Error if first arg is missing or not a string: `"fmt.sprintf: expected (string format, ...args)"`.

```c
string s = fmt.sprintf("x=%d y=%s", 10, "hi");  // "x=10 y=hi"
string f = fmt.sprintf("%.2f", 3.14159);          // "3.14"
```

### fmt.dollar(val) -> string

Formats a numeric value as a US dollar string.

- Accepts exactly 1 argument.
- `f64` → `$%.2f` (e.g., `9.99` → `"$9.99"`)
- `i32` → `$%d.00` (e.g., `5` → `"$5.00"`)
- `i64` → `$%d.00` (e.g., `i64(100)` → `"$100.00"`)
- Error if not numeric: `"fmt.dollar: expected numeric type, received TYPE"`.

```c
string a = fmt.dollar(9.99);      // "$9.99"
string b = fmt.dollar(5);         // "$5.00"
string c = fmt.dollar(i64(100));  // "$100.00"
```
