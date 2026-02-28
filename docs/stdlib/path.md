# path

File path manipulation. Uses the OS-native path separator.

```
import "path";
```

## Functions

### path.join(...string parts) -> string

Joins path segments using the OS path separator. Cleans the result.

- All arguments must be strings.
- Error if any arg is not a string: `"path.join: arg N must be string"`.

```c
string p = path.join("a", "b", "c");  // "a/b/c" (Unix) or "a\b\c" (Windows)
```

### path.dir(string p) -> string

Returns the directory portion of a path.

```c
string d = path.dir("/a/b/c.txt");  // "/a/b"
```

### path.base(string p) -> string

Returns the last element of a path (the filename).

```c
string b = path.base("/a/b/c.txt");  // "c.txt"
```

### path.ext(string p) -> string

Returns the file extension, including the leading dot.

```c
string e = path.ext("/a/b/c.txt");  // ".txt"
string f = path.ext("noext");        // ""
```

### path.abs(string p) -> (string, err)

Returns the absolute path.

- Returns `(absolute_path, ok)` on success.
- Returns `("", err(message))` on failure.

```c
string abs, err e = path.abs("relative/path");
```
