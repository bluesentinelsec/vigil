# args

Command-line argument parsing with flags and positional arguments.

```c
import "args";
```

## Quick Example

```c
import "args";
import "fmt";

fn main() -> i32 {
    // Create parser
    args.ArgParser p = args.parser("mytool", "A simple CLI tool");
    
    // Define flags
    p.flag("verbose", "bool", "false", "Enable verbose output");
    p.flag("output", "string", "out.txt", "Output file path");
    p.flag("count", "i32", "1", "Number of iterations");
    
    // Define positional arguments
    p.arg("input", "string", "Input file path");
    
    // Parse command-line arguments
    map<string, string> result, err e = p.parse();
    if (e != ok) {
        fmt.println(f"Error: {e}");
        return 1;
    }
    
    // Access parsed values (all returned as strings)
    string verbose = result["verbose"];  // "true" or "false"
    string output = result["output"];
    string count = result["count"];
    string input = result["input"];
    
    fmt.println(f"Input: {input}");
    fmt.println(f"Output: {output}");
    fmt.println(f"Count: {count}");
    fmt.println(f"Verbose: {verbose}");
    
    return 0;
}
```

Run with: `basl mytool.basl --verbose --output result.txt --count 5 data.txt`

## API Reference

### args.parser(string name, string description) -> ArgParser

Creates a new argument parser.

**Parameters:**
- `name`: Program name (shown in help/error messages)
- `description`: Brief description of the program

**Returns:** `ArgParser` object

```c
args.ArgParser p = args.parser("myapp", "A tool that does things");
```

## args.ArgParser Type

The `ArgParser` type is returned by `args.parser()` and provides methods for defining and parsing arguments.

**Important:** You must declare the variable type as `ArgParser`:

```c
args.ArgParser p = args.parser("app", "desc");  // Correct
```

### p.flag(string name, string type, string default, string help[, string short]) -> err

Defines a named flag (`--name` or `-s`).

**Parameters:**
- `name`: Flag name (used as `--name` on command line)
- `type`: Type hint - `"bool"`, `"string"`, `"i32"`, `"f64"`, etc.
- `default`: Default value as a string (e.g., `"false"`, `"0"`, `""`)
- `help`: Help text describing the flag
- `short` (optional): Single-character short flag (e.g., `"v"` for `-v`)

**Returns:** `ok` on success, `err` on failure

**Bool flags:** Presence sets value to `"true"`, absence uses default. No argument consumed.

**Other flags:** Consume the next command-line argument as the value.

```c
p.flag("verbose", "bool", "false", "Enable verbose output", "v");
p.flag("output", "string", "out.txt", "Output file path", "o");
p.flag("count", "i32", "10", "Number of iterations", "c");
```

### p.arg(string name, string type, string help[, bool variadic]) -> err

Defines a positional argument.

**Parameters:**
- `name`: Argument name (used as key in result map)
- `type`: Type hint - `"string"`, `"i32"`, etc.
- `help`: Help text describing the argument
- `variadic` (optional): If `true`, collects all remaining args as space-separated string

**Returns:** `ok` on success, `err` on failure

Positional arguments are matched in order after all flags are processed.

**Variadic arguments:** When `variadic` is `true`, all remaining positional arguments are collected into a single newline-separated string (`\n`). Call `.split("\n")` to get individual values. Newline is used because command-line arguments cannot contain literal newlines, ensuring proper handling of filenames with spaces.

```c
p.arg("input", "string", "Input file path");
p.arg("files", "string", "Files to process", true);  // Variadic
```

### p.parse() -> (map\<string, string\>, err)

Parses the script's command-line arguments (from `os.args`).

**Returns:**
- `map<string, string>`: Map with flag/arg names as keys, values as strings
- `err`: `ok` on success, error message on failure

**Important notes:**
- All values are returned as strings, regardless of declared type
- Flag defaults are applied for flags not present on command line
- Missing positional arguments default to `""`
- Use the name (not type hint) as the map key

```c
map<string, string> result, err e = p.parse();
if (e != ok) {
    fmt.println(f"Parse error: {e}");
    return 1;
}

string verbose = result["verbose"];  // Access by flag name
string input = result["input"];      // Access by arg name
```

## Parsing Rules

**Flags:**
- Long form: `--name` (e.g., `--verbose`, `--output file.txt`)
- Short form: `-s` (e.g., `-v`, `-o file.txt`)
- Bool flags: `--verbose` or `-v` sets value to `"true"` (no argument consumed)
- Other flags: `--output file.txt` or `-o file.txt` consumes next argument as value
- Flags can appear anywhere on command line
- Unknown flags return error

**Positional arguments:**
- Matched in order after flags are extracted
- Missing positional arguments default to `""`
- Variadic arguments collect all remaining args as space-separated string

**Example command line:**
```bash
basl script.basl -v -c 5 input.txt --output out.txt file2.txt file3.txt
```

With variadic files:
```c
p.flag("verbose", "bool", "false", "Verbose", "v");
p.flag("count", "string", "1", "Count", "c");
p.flag("output", "string", "out.txt", "Output", "o");
p.arg("input", "string", "Input file");
p.arg("files", "string", "Additional files", true);  // Variadic
```

Parsed as:
- `verbose`: `"true"` (short bool flag)
- `count`: `"5"` (short flag with value)
- `output`: `"out.txt"` (long flag with value)
- `input`: `"input.txt"` (first positional)
- `files`: `"file2.txt\nfile3.txt"` (variadic, newline-separated)

To get individual files:
```c
array<string> fileList = result["files"].split("\n");
// fileList[0] = "file2.txt"
// fileList[1] = "file3.txt"
```

## Type Conversion

All values are returned as strings. Convert as needed:

```c
map<string, string> result, err e = p.parse();

// Convert to appropriate types
bool verbose = result["verbose"] == "true";
i32 count = i32(result["count"]);  // May need error handling
string input = result["input"];     // Already a string
```

## Complete Example

```c
import "args";
import "fmt";
import "os";

fn main() -> i32 {
    // Create parser
    args.ArgParser p = args.parser("grep", "Search for patterns in files");
    
    // Define flags
    p.flag("ignore-case", "bool", "false", "Case-insensitive search");
    p.flag("count", "bool", "false", "Only print count of matches");
    p.flag("line-number", "bool", "false", "Show line numbers");
    
    // Define positional arguments
    p.arg("pattern", "string", "Pattern to search for");
    p.arg("file", "string", "File to search in");
    
    // Parse
    map<string, string> result, err e = p.parse();
    if (e != ok) {
        fmt.println(f"Error: {e}");
        return 1;
    }
    
    // Extract values
    string pattern = result["pattern"];
    string file = result["file"];
    bool ignoreCase = result["ignore-case"] == "true";
    bool showCount = result["count"] == "true";
    bool showLineNum = result["line-number"] == "true";
    
    // Validate required arguments
    if (pattern == "" || file == "") {
        fmt.println("Error: pattern and file are required");
        return 1;
    }
    
    fmt.println(f"Searching for '{pattern}' in {file}");
    fmt.println(f"Options: ignore-case={ignoreCase}, count={showCount}, line-number={showLineNum}");
    
    return 0;
}
```

Run with:
```bash
basl grep.basl --ignore-case --line-number "error" log.txt
```

