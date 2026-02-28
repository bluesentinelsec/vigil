# ffi

Foreign Function Interface for calling native shared libraries (`.so`, `.dylib`, `.dll`).

```
import "ffi";
```

## Functions

### ffi.load(string path) -> (ffi.Lib, err)

Loads a shared library from the given path.

- Returns `(lib, ok)` on success.
- Returns `(void, err(message))` on failure (file not found, invalid library).

```c
ffi.Lib lib, err e = ffi.load("./libexample.so");
```

### ffi.bind(ffi.Lib lib, string name, string ret_type, ...string param_types) -> (ffi.Func, err)

Binds a symbol from the loaded library to a callable function.

- `name`: the C function name.
- `ret_type`: return type as a string (e.g., `"i32"`, `"void"`, `"ptr"`).
- `param_types`: parameter types as strings.
- Returns `(func, ok)` on success.
- Returns `(void, err(message))` on failure (symbol not found).

```c
ffi.Func add, err e = ffi.bind(lib, "add", "i32", "i32", "i32");
```

## ffi.Lib Methods

### lib.close() -> err

Closes the shared library handle.

## ffi.Func

Bound functions are called directly with BASL values. The FFI layer handles type conversion between BASL and C types.

```c
ffi.Lib lib, err e1 = ffi.load("./libmath.so");
ffi.Func add, err e2 = ffi.bind(lib, "add", "i32", "i32", "i32");
i32 result = add(2, 3);  // calls native add(2, 3)
lib.close();
```
