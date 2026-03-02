# basl-grep

A full-featured grep implementation in BASL demonstrating stdlib capabilities.

## Purpose

This tool validates BASL's standard library functionality through real-world Unix tool implementation. All originally identified limitations have been fixed!

## Features Implemented

✅ **Basic pattern matching** with regular expressions  
✅ **Case-insensitive search** (`-i`, `--ignore-case`)  
✅ **Invert match** (`-v`, `--invert-match`) - show non-matching lines  
✅ **Line numbers** (`-n`, `--line-number`)  
✅ **Count matches** (`-c`, `--count`)  
✅ **List matching files** (`-l`, `--files-with-matches`)  
✅ **Recursive search** (`-r`, `--recursive`) - NOW WORKING!  
✅ **Multiple file support** with filename prefixes  
✅ **Proper exit codes** (0=match found, 1=no match, 2=error)  
✅ **Compiled regex** - Pattern compiled once, reused for all lines

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

# Recursive search
basl main.basl -r "pattern" directory/

# Combine flags
basl main.basl -i -n "pattern" file.txt
```

## Standard Library Validation

This implementation validates:

### regex module ✅
- `regex.compile(pattern)` - Compile regex once
- `re.match(string)` - Reusable compiled regex
- Case-insensitive with `(?i)` flag
- No recompilation overhead

### file module ✅
- `file.read_all(path)` - Read file contents
- `file.read_dir(path)` - List directory (new alias!)
- `file.stat(path)` - Get file/directory info
- `file.FileStat` type with proper namespace

### path module ✅
- `path.join(dir, file)` - Build paths correctly

### os module ✅
- `os.args()` - Command-line arguments
- Returns array without script name

### fmt module ✅
- `fmt.println()` / `fmt.eprintln()` - Output
- F-string interpolation

## Performance

Uses compiled regex objects - pattern is compiled once and reused for all lines. This eliminates the O(n) recompilation overhead that would occur with `regex.match()` on every line.

## BASL Improvements Validated

This implementation validates the fixes for all 5 limitations:

1. ✅ **Type Namespace** - `file.FileStat` works correctly
2. ✅ **Subprocess API** - `os.system()` and enhanced `os.exec()` available
3. ✅ **CLI Parsing** - Short flags and variadic args both work
4. ✅ **Compiled Regex** - `regex.compile()` and `regex.Regex` type
5. ✅ **API Consistency** - `file.read_dir()` alias added

**Note:** This implementation now uses `args.ArgParser` with variadic support. Variadic args are returned as space-separated string, which is split to get individual files.

See [LIMITATIONS.md](LIMITATIONS.md) for detailed analysis.

## Exit Codes

- `0` - Match found
- `1` - No match found
- `2` - Error (missing args, file not found, invalid pattern, etc.)

## Implementation Notes

- Compiles regex once using `regex.compile()`
- Uses `file.FileStat` with proper module qualification
- Uses `file.read_dir()` for directory listing
- Recursive search fully functional
- Pattern reused across all lines (no recompilation)

## Value

This implementation:
1. **Validates stdlib** - All modules work correctly
2. **Demonstrates capabilities** - BASL can implement real Unix tools
3. **Validates fixes** - All 5 limitations have been resolved
4. **Provides example** - Developers can learn from working code
