# basl-grep

A full-featured implementation of `grep` in BASL, demonstrating regex, file I/O, and command-line argument parsing.

## Features

- ✅ Pattern matching with regular expressions
- ✅ Case-insensitive search (`--ignore-case`)
- ✅ Invert match (`--invert-match`)
- ✅ Line numbers (`--line-number`)
- ✅ Count matches (`--count`)
- ✅ List matching files (`--files-with-matches`)
- ✅ List non-matching files (`--files-without-match`)
- ✅ Recursive directory search (`--recursive`)
- ✅ Context lines (`--before-context`, `--after-context`, `--context`)
- ✅ Multiple file support
- ✅ Stdin support
- ✅ Quiet mode (`--quiet`)
- ✅ Filename control (`--no-filename`, `--with-filename`)

## Usage

### Basic Pattern Matching
```bash
basl main.basl "pattern" file.txt
```

### Case-Insensitive Search
```bash
basl main.basl --ignore-case "pattern" file.txt
```

### Show Line Numbers
```bash
basl main.basl --line-number "pattern" file.txt
```

### Count Matches
```bash
basl main.basl --count "pattern" file.txt
```

### Invert Match (Show Non-Matching Lines)
```bash
basl main.basl --invert-match "pattern" file.txt
```

### List Files With Matches
```bash
basl main.basl --files-with-matches "pattern" *.txt
```

### Recursive Search
```bash
basl main.basl --recursive "pattern" directory/
```

### Context Lines
```bash
# Show 2 lines before and after each match
basl main.basl --context 2 "pattern" file.txt

# Show 3 lines before each match
basl main.basl --before-context 3 "pattern" file.txt

# Show 1 line after each match
basl main.basl --after-context 1 "pattern" file.txt
```

### Search Multiple Files
```bash
basl main.basl "pattern" file1.txt file2.txt file3.txt
```

### Read from Stdin
```bash
cat file.txt | basl main.basl "pattern"
echo "hello world" | basl main.basl "hello"
```

### Quiet Mode (Exit Status Only)
```bash
basl main.basl --quiet "pattern" file.txt
echo $?  # 0 if match found, 1 if not
```

## Exit Status

- `0` - Match found
- `1` - No match found
- `2` - Error occurred

## Examples

### Find all TODO comments in source files
```bash
basl main.basl --recursive "TODO" src/
```

### Count occurrences of "error" in log files
```bash
basl main.basl --count "error" *.log
```

### Find files containing "import" but not "export"
```bash
basl main.basl --files-with-matches "import" *.js | \
  xargs basl main.basl --files-without-match "export"
```

### Search with context
```bash
basl main.basl --context 3 "function main" *.basl
```

### Case-insensitive search for "error" with line numbers
```bash
basl main.basl --ignore-case --line-number "error" app.log
```

## Standard Library Usage

This implementation demonstrates the following BASL stdlib modules:

- **args**: Command-line argument parsing with flags and positional arguments
- **regex**: Pattern matching with regular expressions
- **file**: File reading, directory traversal, file stats
- **io**: Reading from stdin line by line
- **strings**: String manipulation (split, starts_with, etc.)
- **path**: Path joining for recursive search
- **fmt**: Formatted output to stdout and stderr
- **os**: Command-line arguments and system interaction

## Testing

Run the test suite:

```bash
cd test
basl grep_test.basl
```

Tests cover:
- Basic pattern matching
- Case-insensitive search
- Invert match
- Line numbers
- Count mode
- Multiple files
- Files with matches
- Context lines
- Recursive search

## Implementation Notes

### Regex Support
Uses BASL's `regex` module which supports standard regex syntax. Case-insensitive matching is achieved with the `(?i)` flag.

### Context Lines
Maintains a circular buffer for before-context and a counter for after-context. Handles overlapping context regions correctly.

### Recursive Search
Traverses directories recursively, skipping `.` and `..` entries. Always prints filenames in recursive mode.

### Stdin Handling
Detects when no files are provided and reads from stdin. Handles EOF correctly.

### Performance
Reads entire files into memory for simplicity. For very large files, consider streaming line-by-line.

## Comparison with GNU grep

This implementation covers the most commonly used grep features:

**Supported:**
- Basic pattern matching
- Case-insensitive (`-i`)
- Invert match (`-v`)
- Line numbers (`-n`)
- Count (`-c`)
- Files with matches (`-l`)
- Recursive (`-r`)
- Context lines (`-A`, `-B`, `-C`)
- Multiple files
- Stdin input

**Not Implemented:**
- Extended regex (`-E`)
- Fixed strings (`-F`)
- Perl regex (`-P`)
- Binary file handling (`-a`, `-I`)
- Exclude patterns (`--exclude`)
- Color output (`--color`)
- Max count (`-m`)

## License

Part of the BASL examples collection.
