# args

Professional command-line argument parsing with flags and positional arguments.

```c
import "args";
```

## Quick Example

```c
import "args";
import "fmt";

fn main() -> i32 {
    args.ArgParser p = args.parser("mytool", "A simple CLI tool");
    
    p.flag("verbose", "bool", "false", "Enable verbose output", "v");
    p.flag("output", "string", "out.txt", "Output file path", "o");
    p.arg("input", "string", "Input file path");
    p.arg("files", "string", "Additional files", true);  // Variadic
    
    args.Result result, err e = p.parse_result();
    if (e != ok) {
        fmt.println(f"Error: {e}");
        return 1;
    }
    
    bool verbose = result.get_bool("verbose");
    string output = result.get_string("output");
    string input = result.get_string("input");
    array<string> files = result.get_list("files");  // Native array!
    
    fmt.println(f"Input: {input}");
    fmt.println(f"Output: {output}");
    fmt.println(f"Verbose: {verbose}");
    fmt.println(f"Files: {files.len()}");
    
    return 0;
}
```

Run with: `basl mytool.basl -v -o result.txt data.txt file1.txt file2.txt`

## API Reference

### args.parser(string name, string description) -> args.ArgParser

Creates a new argument parser.

```c
args.ArgParser p = args.parser("myapp", "A tool that does things");
```

## args.ArgParser Type

### p.flag(string name, string type, string default, string help[, string short]) -> err

Defines a named flag with **enforced type validation**.

**Parameters:**
- `name`: Flag name (used as `--name`)
- `type`: Type - `"bool"`, `"string"`, `"i32"`, `"i64"`, `"u32"`, `"u64"`, `"f64"`
- `default`: Default value as string (validated against type)
- `help`: Help text
- `short` (optional): Single-character short flag (e.g., `"v"` for `-v`)

**Type Validation:**
- Default values are validated at flag declaration time
- Flag values are validated at parse time
- Invalid values return error with clear message
- Supported types: `bool`, `string`, `i32`, `i64`, `u32`, `u64`, `f64`

**Bool flags:** Presence sets to `"true"`, no argument consumed.
**Other flags:** Consume next argument as value, validated against declared type.

```c
p.flag("verbose", "bool", "false", "Enable verbose output", "v");
p.flag("count", "i32", "10", "Number of items", "c");
p.flag("ratio", "f64", "1.5", "Scaling ratio");

// Invalid default caught immediately:
err e = p.flag("count", "i32", "abc", "count");  // Returns error
```

### p.arg(string name, string type, string help[, bool variadic]) -> err

Defines a positional argument.

**Parameters:**
- `name`: Argument name
- `type`: Type hint
- `help`: Help text
- `variadic` (optional): If `true`, collects all remaining args as array

```c
p.arg("input", "string", "Input file");
p.arg("files", "string", "Files to process", true);  // Variadic
```

### p.parse_result() -> (args.Result, err)

**Recommended API.** Parses arguments and returns structured result with typed getters.

```c
args.Result result, err e = p.parse_result();
if (e != ok) {
    fmt.println(f"Error: {e}");
    return 1;
}
```

**Features:**
- Unknown flags return error
- Missing values for non-bool flags return error
- Supports `--` end-of-options marker
- Variadic args returned as native `array<string>`

### p.parse() -> (map<string, string>, err)

**Legacy API.** Returns map with all values as strings.

Variadic args are newline-separated string. Call `.split("\n")` to get array.

```c
map<string, string> result, err e = p.parse();
array<string> files = result["files"].split("\n");
```

## args.Result Type

Returned by `parse_result()`. Provides typed getters.

### result.get_string(string name) -> string

Get string value (flag or positional arg).

```c
string pattern = result.get_string("pattern");
string output = result.get_string("output");
```

### result.get_bool(string name) -> bool

Get boolean value (for bool flags).

```c
bool verbose = result.get_bool("verbose");
bool recursive = result.get_bool("recursive");
```

### result.get_i32(string name) -> i32

Get i32 value (for i32 flags). Validates and parses the stored string.

```c
i32 count = result.get_i32("count");
```

### result.get_i64(string name) -> i64

Get i64 value (for i64 flags). Validates and parses the stored string.

```c
i64 size = result.get_i64("size");
```

### result.get_f64(string name) -> f64

Get f64 value (for f64 flags). Validates and parses the stored string.

```c
f64 ratio = result.get_f64("ratio");
```

### result.get_list(string name) -> array<string>

Get array of strings (for variadic positional args).

```c
array<string> files = result.get_list("files");
for (i32 i = 0; i < files.len(); i++) {
    fmt.println(files[i]);
}
```

## Parsing Rules

**Flags:**
- Long form: `--name` (e.g., `--verbose`, `--output file.txt`)
- Short form: `-s` (e.g., `-v`, `-o file.txt`)
- Bool flags: `--verbose` or `-v` sets to `true` (no argument consumed)
- Other flags: `--output file.txt` or `-o file.txt` consumes next argument
- Unknown flags return error
- Missing values for non-bool flags return error

**Positional arguments:**
- Matched in order after flags are extracted
- Variadic arguments collect all remaining args as `array<string>`

**End-of-options marker:**
- `--` stops flag parsing
- All subsequent args treated as positional
- Essential for filenames starting with `-`

**Example:**
```bash
basl script.basl -v -o out.txt input.txt -- -weird.txt
```

With:
```c
p.flag("verbose", "bool", "false", "Verbose", "v");
p.flag("output", "string", "out.txt", "Output", "o");
p.arg("files", "string", "Files", true);
```

Parsed as:
- `verbose`: `true`
- `output`: `"out.txt"`
- `files`: `["input.txt", "-weird.txt"]`

## Complete Example

```c
import "args";
import "fmt";

fn main() -> i32 {
    args.ArgParser p = args.parser("grep", "Search for patterns");
    
    p.flag("ignore-case", "bool", "false", "Case-insensitive", "i");
    p.flag("line-number", "bool", "false", "Show line numbers", "n");
    p.flag("context", "i32", "0", "Context lines", "C");
    p.arg("pattern", "string", "Pattern to search");
    p.arg("files", "string", "Files to search", true);
    
    args.Result result, err e = p.parse_result();
    if (e != ok) {
        fmt.println(f"Error: {e}");
        return 1;
    }
    
    string pattern = result.get_string("pattern");
    bool ignoreCase = result.get_bool("ignore-case");
    bool lineNumber = result.get_bool("line-number");
    i32 context = result.get_i32("context");
    array<string> files = result.get_list("files");
    
    fmt.println(f"Pattern: {pattern}");
    fmt.println(f"Ignore case: {ignoreCase}");
    fmt.println(f"Line numbers: {lineNumber}");
    fmt.println(f"Context: {context}");
    fmt.println(f"Files: {files.len()}");
    
    return 0;
}
```

## Type Validation

The parser **enforces declared types** at both declaration and parse time:

**At flag declaration:**
```c
// This fails immediately - invalid default
err e = p.flag("count", "i32", "abc", "count");
if (e != ok) {
    fmt.println(f"Error: {e}");  // "flag --count expects i32, got: abc"
}
```

**At parse time:**
```c
// User runs: --count not_a_number
args.Result result, err e = p.parse_result();
if (e != ok) {
    fmt.println(f"Error: {e}");  // "flag --count expects i32, got: not_a_number"
}
```

**Supported validated types:**
- `i32` - 32-bit signed integer
- `i64` - 64-bit signed integer
- `u32` - 32-bit unsigned integer
- `u64` - 64-bit unsigned integer
- `f64` - 64-bit floating point
- `bool` - Boolean (no validation needed)
- `string` - String (no validation needed)

## Features

✅ Short flags (`-i`, `-n`, `-v`)  
✅ Long flags (`--ignore-case`)  
✅ Bool flags (no value consumed)  
✅ Value flags (consumes next arg)  
✅ **Type validation** (i32, i64, f64, u32, u64)  
✅ **Numeric getters** (get_i32, get_i64, get_f64)  
✅ Variadic positionals (native `array<string>`)  
✅ Unknown flag validation  
✅ Missing value validation  
✅ `--` end-of-options support  
✅ Handles filenames with spaces  
✅ Handles filenames starting with `-`  
