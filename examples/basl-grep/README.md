# basl-grep

A full-featured grep implementation in BASL demonstrating professional CLI parsing, compiled regex, and file I/O.

## Features Implemented

✅ **Basic pattern matching** with regular expressions  
✅ **Case-insensitive search** (`-i`, `--ignore-case`)  
✅ **Invert match** (`-v`, `--invert-match`)  
✅ **Line numbers** (`-n`, `--line-number`)  
✅ **Count matches** (`-c`, `--count`)  
✅ **List matching files** (`-l`, `--files-with-matches`)  
✅ **Recursive search** (`-r`, `--recursive`)  
✅ **Multiple file support** (variadic, handles spaces)  
✅ **Proper exit codes** (0=match found, 1=no match, 2=error)  
✅ **Compiled regex** - Pattern compiled once, reused  
✅ **Professional args parsing** - Uses `args.Result` with typed getters  

## Usage

```bash
# Basic search
basl main.basl "pattern" file.txt

# Filenames with spaces work correctly
basl main.basl "hello" "/tmp/my file.txt"

# Short flags with multiple files
basl main.basl -i -n "PATTERN" file1.txt file2.txt file3.txt

# Recursive search
basl main.basl -r "pattern" directory/

# Files starting with - (use --)
basl main.basl "pattern" -- -weird.txt

# All features combined
basl main.basl -i -n -c "pattern" *.txt
```

## Implementation Highlights

**Professional Args Parsing:**
```basl
args.Result result, err e = parser.parse_result();
array<string> files = result.get_list("files");  // Native array!
bool verbose = result.get_bool("verbose");
```

**Compiled Regex:**
```basl
regex.Regex re, err e = regex.compile(pattern);
// Pattern compiled once, reused for all lines
```

**Type Namespacing:**
```basl
file.FileStat stat, err e = file.stat(filepath);
```

## BASL Features Validated

This implementation validates all 5 stdlib improvements:

1. ✅ **Type Namespace** - `file.FileStat`, `args.ArgParser`, `args.Result`
2. ✅ **Subprocess API** - `os.system()` and `os.exec()` with exit codes
3. ✅ **CLI Parsing** - Professional `args.Result` with native array variadics
4. ✅ **Compiled Regex** - `regex.compile()` and `regex.Regex` type
5. ✅ **API Consistency** - `file.read_dir()` alias

See [LIMITATIONS.md](LIMITATIONS.md) for detailed analysis.

## Exit Codes

- `0` - Match found
- `1` - No match found
- `2` - Error (missing args, file not found, invalid pattern, etc.)

## Value

This implementation demonstrates:
1. **Professional CLI parsing** - Clean, typed API
2. **Performance** - Compiled regex, no recompilation overhead
3. **Robustness** - Handles spaces, special characters, edge cases
4. **Production-ready** - All features work correctly
