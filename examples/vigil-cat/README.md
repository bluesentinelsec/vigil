# vigil-cat

A VIGIL implementation of the Unix `cat` command, serving as a capability benchmark for the language.

## Features

✅ Concatenate multiple files to stdout
✅ Support `-` for stdin
✅ Error reporting to stderr
✅ Non-zero exit on errors
✅ Byte-preserving (works with binary files)

## Usage

```bash
# Concatenate files
vigil main.vigil file1.txt file2.txt

# Read from stdin
echo "hello" | vigil main.vigil -

# Mix files and stdin
vigil main.vigil file1.txt - file2.txt

# Error handling
vigil main.vigil /nonexistent
# Outputs to stderr and exits with code 1
```

## Implementation Notes

This implementation validates several VIGIL capabilities:

- **CLI argument handling**: Uses `os.args()` for file arguments
- **File I/O**: Uses `file.read_all()` for reading files
- **Stdin support**: Uses `io.read_all()` for reading stdin
- **Error handling**: Multi-return error values with `err` type
- **Stderr output**: Uses `fmt.eprintln()` for error messages
- **Exit codes**: Returns non-zero on failure

## Stdlib Additions

This implementation required adding two new stdlib functions:

### `io.read_all() -> (string, err)`

Reads all of stdin into a string. Returns `(content, ok)` on success or `("", err(message))` on failure.

### `fmt.eprint(string)` and `fmt.eprintln(string)`

Write to stderr instead of stdout. Useful for error messages in CLI tools.

## Limitations

- Uses `file.read_all()` which loads entire files into memory
- For streaming large files, would need chunked reading API
- Binary files work but are surfaced as strings (VIGIL strings are byte-preserving)

## Testing

```bash
# Run tests
cd examples/vigil-cat
../../vigil test
```

## Benchmark Results

This implementation demonstrates that VIGIL can:
✅ Handle basic Unix-style CLI tools
✅ Process files and stdin
✅ Separate stdout and stderr
✅ Handle errors gracefully
✅ Preserve byte content

Gaps identified and addressed:
- Added `io.read_all()` for raw stdin reading
- Added `fmt.eprint()` / `fmt.eprintln()` for stderr output
