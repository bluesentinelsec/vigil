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

## Implementation Notes

This implementation validates several BASL capabilities:

- **File I/O**: Uses `file.read_all()` for reading files
- **Stdin handling**: Uses `io.read_all()` for stdin
- **String processing**: Character-by-character iteration with `substr()`
- **CLI argument parsing**: Manual flag and file argument parsing
- **Error handling**: Proper stderr output and exit codes
- **Cross-platform**: Works on Unix and Windows

### Counting Logic

- **Lines**: Count of newline characters (`\n`)
- **Words**: Sequences of non-whitespace characters (space, tab, newline)
- **Bytes**: `string.len()` which returns byte length (UTF-8)

## Testing

```bash
basl test
```

Tests validate:
- Single file processing
- Multiple files with totals
- Flag handling (`-l`)
- Empty files
- Path-independent execution (works from repo root or example directory)

## Gaps Identified

This implementation revealed:

1. **No chunked streaming**: Uses `file.read_all()` which loads entire file into memory. For large files, BASL would benefit from chunked reading or line-by-line iteration.

2. **Manual flag parsing**: The `args` module's `ArgParser` was found to hang during execution, requiring manual argument parsing. This suggests the args module may need fixes or better documentation.

3. **Limited string iteration**: Character-by-character access requires `substr(i, 1)` in a loop. A character iterator or byte access would be more efficient.

4. **No Unicode character counting**: `string.len()` returns bytes. True character counting (Unicode code points) would require additional stdlib support.

Despite these gaps, BASL successfully implements a functional `wc` utility that handles the common use cases correctly.
