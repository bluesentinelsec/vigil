# basl-grep

A grep implementation in BASL demonstrating regex, file I/O, and CLI parsing.

## Purpose

This tool validates BASL's standard library functionality and exposes language limitations for Unix-style tool development. See [LIMITATIONS.md](LIMITATIONS.md) for detailed analysis of gaps discovered.

## Features Implemented

✅ **Basic pattern matching** with regular expressions  
✅ **Case-insensitive search** (`-i`, `--ignore-case`)  
✅ **Invert match** (`-v`, `--invert-match`) - show non-matching lines  
✅ **Line numbers** (`-n`, `--line-number`)  
✅ **Count matches** (`-c`, `--count`)  
✅ **List matching files** (`-l`, `--files-with-matches`)  
✅ **Multiple file support** with filename prefixes  
✅ **Proper exit codes** (0=match found, 1=no match, 2=error)

## Features NOT Implemented

❌ **Recursive search** (`-r`) - Blocked by type namespace mismatch (see LIMITATIONS.md #1)  
❌ **Stdin input** - Not implemented  
❌ **Context lines** (`-A`, `-B`, `-C`) - Not implemented  
❌ **Extended regex** (`-E`) - Not implemented  
❌ **Fixed strings** (`-F`) - Not implemented  
❌ **Color output** - Not implemented  
❌ **Binary file handling** - Not implemented

## Usage

```bash
# Basic search
basl main.basl "pattern" file.txt

# Case-insensitive
basl main.basl -i "PATTERN" file.txt

# With line numbers
basl main.basl -n "pattern" file.txt

# Count matches
basl main.basl -c "pattern" file.txt

# Invert match (show non-matching lines)
basl main.basl -v "pattern" file.txt

# List files with matches
basl main.basl -l "pattern" file1.txt file2.txt

# Combine flags
basl main.basl -i -n "pattern" file.txt
```

## Standard Library Validation

This implementation tests:

- ✅ `regex.match()` - Pattern matching works
- ✅ `file.read_all()` - File reading works
- ✅ `os.args()` - Command-line args work (returns array without script name)
- ✅ `fmt.println()` / `fmt.eprintln()` - Output works
- ✅ F-string interpolation works
- ✅ String `.split()` method works

## Known Issues

1. **Recursive search disabled** - `file.stat()` returns `FileStat` but type annotation requires `file.FileStat` (runtime type mismatch)
2. **Manual flag parsing** - `args.ArgParser` doesn't support short flags (`-i`) or variadic positionals (`[FILE...]`)
3. **Performance issue** - `regex.match()` recompiles pattern on every call (no compiled regex objects)
4. **No integration tests** - BASL lacks subprocess API for black-box CLI testing

See [LIMITATIONS.md](LIMITATIONS.md) for detailed analysis.

## Exit Codes

- `0` - Match found
- `1` - No match found
- `2` - Error (missing args, file not found, etc.)

## Implementation Notes

- Uses `regex.match()` directly (no compile step available)
- Case-insensitive via `(?i)` regex prefix
- Manual `os.args()` parsing due to `args` module limitations
- Pattern recompiles on every line (performance bottleneck)

## Value

This implementation:
1. **Validates stdlib functionality** - Tests regex, file, os, fmt modules
2. **Exposes language gaps** - Documents real limitations for Unix-tool development
3. **Provides practical example** - Shows what's possible and what's missing
4. **Guides future work** - Clear list of needed stdlib improvements
