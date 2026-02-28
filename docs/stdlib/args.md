# args

Command-line argument parsing with flags and positional arguments.

```
import "args";
```

## Functions

### args.parser(string name, string description) -> ArgParser

Creates a new argument parser.

```c
ArgParser p = args.parser("myapp", "A tool that does things");
```

## ArgParser Methods

### p.flag(string name, string type, string default, string help) -> err

Defines a named flag (`--name`).

- `type`: `"bool"`, `"string"`, `"i32"`, etc.
- `default`: default value as a string.
- Bool flags don't consume the next argument — their presence sets them to `"true"`.

```c
p.flag("verbose", "bool", "false", "Enable verbose output");
p.flag("output", "string", "out.txt", "Output file path");
```

### p.arg(string name, string type, string help) -> err

Defines a positional argument.

```c
p.arg("input", "string", "Input file");
```

### p.parse() -> (map\<string, string\>, err)

Parses the script's command-line arguments (from `os.args`).

- Returns a `map<string, string>` with flag names and positional arg names as keys.
- Flag defaults are applied for any flags not present on the command line.
- All values are strings regardless of declared type.
- Returns `(result, ok)` on success.

```c
ArgParser p = args.parser("app", "desc");
p.flag("verbose", "bool", "false", "verbose mode");
p.arg("file", "string", "input file");
map<string, string> result, err e = p.parse();
string verbose = result["verbose"];  // "false" or "true"
string file = result["file"];        // positional arg value
```

## Parsing Rules

- Flags are prefixed with `--` (e.g., `--verbose`, `--output file.txt`).
- Bool flags: `--verbose` sets value to `"true"` (no following argument consumed).
- Non-bool flags: `--output file.txt` consumes the next argument as the value.
- Positional arguments are matched in order after flags are extracted.
- Missing positional arguments default to `""`.
