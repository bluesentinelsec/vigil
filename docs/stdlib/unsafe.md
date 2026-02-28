# unsafe

Low-level memory operations. The `unsafe` module must be explicitly imported — its functions are not available in safe code.

```
import "unsafe";
```

## Constants

### unsafe.null

The null pointer value. Prints as `"null"`. Only available in unsafe code.

```c
fmt.print(string(unsafe.null));  // "null"
```

## Functions

### unsafe.alloc(i32 size) -> unsafe.Buffer

Allocates a byte buffer of the given size (managed by Go's GC, not C malloc).

- `size` must be > 0.
- Error if size ≤ 0: `"unsafe.alloc: size must be > 0"`.
- Error if arg is not `i32`: `"unsafe.alloc: expected i32 size"`.

```c
unsafe.Buffer buf = unsafe.alloc(256);
```

### unsafe.layout(...string types) -> unsafe.Layout

Creates a struct layout descriptor from a list of type names. Used for structured binary data.

Supported types and sizes:
| Type | Size (bytes) |
|------|-------------|
| `"u8"` | 1 |
| `"i32"`, `"u32"`, `"f32"` | 4 |
| `"i64"`, `"u64"`, `"f64"` | 8 |
| `"ptr"` | 8 |

- Fields are laid out sequentially with no padding.
- Error if a type is unsupported: `"unsafe.layout: unsupported type \"NAME\""`.

```c
unsafe.Layout layout = unsafe.layout("i32", "i32", "f64");
```

### unsafe.callback(fn func, string ret_type, ...string param_types) -> unsafe.Callback

Wraps a BASL function as a C function pointer. Used for FFI callbacks.

- Returns an `unsafe.Callback` object.
- Error if first arg is not a function or second arg is not a string.

## unsafe.Buffer Methods

### buf.len() -> i32

Returns the buffer size in bytes.

### buf.get(i32 index) -> u8

Returns the byte at the given index. Bounds-checked.

- Error if out of bounds: `"unsafe.Buffer.get: index N out of bounds"`.

### buf.set(i32 index, u8|i32 value)

Sets the byte at the given index. Accepts `u8` or `i32` (truncated to byte). Bounds-checked.

- Error if out of bounds: `"unsafe.Buffer.set: index N out of bounds"`.

### buf.get_u32(i32 byte_offset) -> u32

Reads a little-endian `u32` at the given byte offset. Bounds-checked (needs 4 bytes).

### buf.ptr() -> unsafe.Ptr

Returns a raw pointer to the buffer's underlying data.

```c
unsafe.Buffer buf = unsafe.alloc(4);
buf.set(0, 65);
buf.set(1, 66);
u8 a = buf.get(0);   // 65
i32 len = buf.len();  // 4
```

## unsafe.Layout Methods

### layout.new() -> unsafe.Struct

Creates a new struct instance from the layout. The struct's memory is zero-initialized.

## unsafe.Struct Methods

### s.get(i32 field_index) -> value

Returns the value of the field at the given index.

### s.set(i32 field_index, value)

Sets the value of the field at the given index.

### s.ptr() -> unsafe.Ptr

Returns a raw pointer to the struct's underlying memory.

```c
unsafe.Layout layout = unsafe.layout("i32", "i32");
unsafe.Struct s = layout.new();
s.set(0, 42);
s.set(1, 99);
i32 a = s.get(0);  // 42
i32 b = s.get(1);  // 99
```

## unsafe.Callback Methods

### cb.ptr() -> unsafe.Ptr

Returns the C function pointer.

### cb.free()

Releases the callback slot. Must be called when the callback is no longer needed.
