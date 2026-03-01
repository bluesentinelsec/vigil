# basl-wc

A BASL implementation of the Unix `wc` (word count) utility.

## Usage

```bash
basl main.basl [OPTIONS] [FILE...]
```

Count lines, words, and bytes in files.

### Options

- `-l`, `--lines` - Print line count only
- `-w`, `--words` - Print word count only  
- `-c`, `--bytes` - Print byte count only

If no options are specified, all three counts are shown.

### Arguments

- `FILE...` - One or more files to process
- `-` - Read from stdin
- No arguments - Read from stdin

### Examples

```bash
# Count all metrics for a file
basl main.basl file.txt

# Count only lines
basl main.basl -l file.txt

# Count multiple files (shows total)
basl main.basl file1.txt file2.txt

# Read from stdin
cat file.txt | basl main.basl

# Mix files and stdin
basl main.basl file1.txt - file2.txt < input.txt
```

## Project Structure

```
basl-wc/
├── main.basl              # CLI entrypoint (argument parsing, error handling)
├── lib/
│   ├── counter.basl       # Core counting logic
│   └── processor.basl     # File and stdin processing
└── test/
    ├── wc_test.basl       # Integration tests
    └── lib/
        └── counter_test.basl  # Unit tests for counter logic
```

## Testing

```bash
basl test                  # Run all tests (16 tests)
basl test test/lib/        # Run unit tests only (9 tests)
basl test test/wc_test.basl  # Run integration tests only (7 tests)
```

Tests validate:
- Counter logic (empty, single/multiple lines, whitespace handling)
- Single file processing
- Multiple files with totals
- Flag handling (`-l`)
- Empty files
- Missing file error handling
- Stdin via `-` argument
- Stdin via no arguments
- Error propagation
- Path-independent execution

## Implementation Notes

### Architecture

The implementation is split into three layers:

1. **main.basl** - Lean CLI entrypoint that handles argument parsing and orchestrates processing
2. **lib/processor.basl** - File and stdin I/O with error propagation
3. **lib/counter.basl** - Pure counting logic, fully unit tested

This separation enables:
- Unit testing of counting logic without I/O
- Clear error handling boundaries
- Reusable components

### Counting Logic

- **Lines**: Count of newline characters (`\n`)
- **Words**: Sequences of non-whitespace characters (Unix `wc` semantics)
  - Whitespace: space, tab, newline, carriage return
  - Everything else (including punctuation and UTF-8) is part of a word
- **Bytes**: `string.len()` which returns byte length (UTF-8)

### Error Handling

All functions properly propagate errors:
- `counter.count_content()` returns `(i32, i32, i32, err)`
- `processor.process_file()` and `process_stdin()` return `(i32, i32, i32, err)`
- `main()` checks all errors and returns exit code 1 on failure
- Stdin read failures are reported to stderr and cause non-zero exit

## Gaps Identified

This implementation revealed several areas where BASL could improve:

1. **No chunked streaming**: Uses `file.read_all()` which loads entire file into memory. For large files, BASL would benefit from chunked reading or line-by-line iteration.

2. **args.ArgParser hangs**: The `args` module's `ArgParser` was found to hang during execution, requiring manual argument parsing. This suggests the args module needs fixes or better documentation.

3. **Limited string iteration**: Character-by-character access requires `substr(i, 1)` in a loop. A character iterator or byte access would be more efficient.

4. **No Unicode character counting**: `string.len()` returns bytes. True character counting (Unicode code points) would require additional stdlib support.

Despite these gaps, BASL successfully implements a functional `wc` utility that handles common use cases correctly with proper error handling.
