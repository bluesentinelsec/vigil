# io

Interactive console input. All functions read from stdin.

```
import "io";
```

## Functions

### io.read_line() -> (string, err)

Reads one line from stdin (up to `\n`). The newline is not included. Carriage returns (`\r`) are trimmed.

- Returns `(line, ok)` on success.
- Returns `("", err("EOF"))` at end of input.

```c
string line, err e = io.read_line();
```

### io.input(string prompt) -> (string, err)

Prints `prompt` to stdout, then reads one line from stdin.

- Returns `(line, ok)` on success.
- Returns `("", err("EOF"))` at end of input.

```c
string name, err e = io.input("Enter name: ");
```

### io.read_i32(string prompt) -> (i32, err)

Prints `prompt`, reads a line, and parses it as an integer.

- Returns `(value, ok)` on success.
- Returns `(0, err("invalid integer"))` if the input is not a valid integer.
- Returns `(0, err("EOF"))` at end of input.

```c
i32 age, err e = io.read_i32("Enter age: ");
```

### io.read_f64(string prompt) -> (f64, err)

Prints `prompt`, reads a line, and parses it as a float.

- Returns `(value, ok)` on success.
- Returns `(0.0, err("invalid number"))` if the input is not a valid number.

```c
f64 price, err e = io.read_f64("Enter price: ");
```

### io.read_string(string prompt) -> (string, err)

Prints `prompt`, reads a line. Equivalent to `io.input`.

```c
string val, err e = io.read_string("Enter value: ");
```

### io.read(i32 count) -> (string, err)

Reads up to `count` bytes from stdin. Returns the data read (may be less than `count` at EOF).

- Returns `(data, ok)` on success, where `data.len() <= count`
- Returns `("", err("EOF"))` at end of input with no data
- Returns `("", err(message))` on other errors
- If EOF is reached after reading some data, returns `(data, ok)` with partial data

Useful for processing large inputs incrementally without buffering everything in memory.

```c
// Read and process stdin in 4KB chunks
while (true) {
    string chunk, err e = io.read(4096);
    if (e != ok) {
        break;  // EOF or error
    }
    // Process chunk
    fmt.print(chunk);
}
```

**Example: Streaming tee**
```c
File f, err e = file.open("output.txt", "w");
while (true) {
    string chunk, err e2 = io.read(4096);
    if (e2 != ok) { break; }
    fmt.print(chunk);      // stdout
    f.write(chunk);        // file
}
f.close();
```

### io.read_all() -> (string, err)

Reads all of stdin into a string. Useful for reading piped input or implementing Unix-style filters.

- Returns `(content, ok)` on success.
- Returns `("", err(message))` on failure.

```c
string content, err e = io.read_all();
if (e != ok) {
    fmt.println("failed to read stdin");
    return 1;
}
fmt.print(content);
```

**Note**: This reads the entire stdin into memory. For large inputs, use `io.read(count)` to process data incrementally. `io.read_line()` strips newlines, so it is not suitable for stream-copy tools like `cat`.
