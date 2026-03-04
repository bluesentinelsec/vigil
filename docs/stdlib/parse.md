# parse

Explicit, fallible parsing for converting strings into typed values.

```c
import "parse";
```

Use this module when you need to parse text input. Type conversions like `i32(x)` do not parse strings.

If you are updating older BASL code, replace `i32(text)` with `parse.i32(text)`, `u32(text)` with `parse.u32(text)`, and so on.

## Functions

### parse.i32(string value) -> (i32, err)

Parses a base-10 signed 32-bit integer.

- Returns `(value, ok)` on success.
- Returns `(0, err(message, err.parse))` on failure.

```c
i32 n, err e = parse.i32("42");
```

### parse.i64(string value) -> (i64, err)

Parses a base-10 signed 64-bit integer.

- Returns `(value, ok)` on success.
- Returns `(0, err(message, err.parse))` on failure.

```c
i64 n, err e = parse.i64("9000000000");
```

### parse.f64(string value) -> (f64, err)

Parses a 64-bit floating-point number.

- Returns `(value, ok)` on success.
- Returns `(0, err(message, err.parse))` on failure.

```c
f64 ratio, err e = parse.f64("3.14");
```

### parse.u8(string value) -> (u8, err)

Parses a base-10 unsigned 8-bit integer.

- Returns `(value, ok)` on success.
- Returns `(0, err(message, err.parse))` on failure.

```c
u8 small, err e = parse.u8("255");
```

### parse.u32(string value) -> (u32, err)

Parses a base-10 unsigned 32-bit integer.

- Returns `(value, ok)` on success.
- Returns `(0, err(message, err.parse))` on failure.

```c
u32 count, err e = parse.u32("100");
```

### parse.u64(string value) -> (u64, err)

Parses a base-10 unsigned 64-bit integer.

- Returns `(value, ok)` on success.
- Returns `(0, err(message, err.parse))` on failure.

```c
u64 total, err e = parse.u64("18446744073709551615");
```

### parse.bool(string value) -> (bool, err)

Parses `"true"` or `"false"`.

- Returns `(value, ok)` on success.
- Returns `(false, err(message, err.parse))` on failure.

```c
bool enabled, err e = parse.bool("true");
```
