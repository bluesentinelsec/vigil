# BASL CLI Reference

Complete reference for all `basl` command-line tools.

## Quick Reference

```bash
basl [script.basl]              # Run a script or start REPL
basl fmt [files...]             # Format code
basl check [targets...]         # Static validation without execution
basl debug [script.basl]        # Start debugger
basl new <name> [--lib]         # Create new project
basl test [path]                # Run tests
basl package [options]          # Package executable or library
basl embed <files...>           # Embed assets
basl doc <module>               # Show documentation
basl editor <vim|vscode>        # Install editor support
basl help [topic]               # Show help
basl --version                  # Show version
```

## Running Scripts

### Execute a script

```bash
basl script.basl
basl script.basl arg1 arg2      # Pass arguments
```

All scripts must use the `.basl` file extension. Arguments are available via `os.args()` in the script.

### Interactive REPL

```bash
basl
```

Start an interactive Read-Eval-Print Loop. Type BASL statements and see results immediately.

```bash
>>> i32 x = 42;
>>> fmt.println(f"x = {x}");
x = 42
>>> x + 10;
52
```

## Code Formatting

### Format files

```bash
basl fmt script.basl            # Format one file (in-place)
basl fmt file1.basl file2.basl  # Format multiple files
basl fmt ./src/...              # Format all .basl files recursively
```

### Check formatting (CI mode)

```bash
basl fmt --check script.basl    # Exit 1 if not formatted
```

Use in CI pipelines to enforce consistent formatting.

## Static Checking

### Validate code without running it

```bash
basl check main.basl
basl check ./lib/...
basl check --path ./vendor main.basl
```

`basl check` parses BASL source and reports diagnostics without executing user code.

It validates imports, declarations, calls, return shapes, interface conformance, and a broad set of common semantic mistakes across most non-graphics stdlib modules.

In a BASL project root, `basl check` defaults to checking `main.basl` plus the `lib/` and `test/` directories when no explicit target is given.

See [check.md](check.md) for the full guide, detailed behavior, and the complete list of issues it detects.

## Debugging

### Start debugger

```bash
basl debug script.basl
```

### Set breakpoints at startup

```bash
basl debug -b 10 script.basl              # Break at line 10
basl debug -b 5 -b 12 -b 20 script.basl   # Multiple breakpoints
```

### Debugger commands

- `break <line>` or `b <line>` - Set breakpoint
- `continue` or `c` - Continue execution
- `step` or `s` - Step into function
- `next` or `n` - Step over function
- `print <expr>` or `p <expr>` - Evaluate expression
- `list` or `l` - Show source around current line
- `quit` or `q` - Exit debugger

See [debugger.md](debugger.md) for detailed usage.

## Project Management

### Create new project

```bash
basl new myapp              # Create application project
basl new mylib --lib        # Create library project
```

Application projects include `main.basl` at the root. Library projects have code in `lib/` only.

Project structure:
```
myapp/
├── basl.toml              # Project manifest
├── main.basl              # Entry point (apps only)
├── lib/                   # Library modules
├── test/                  # Test files
└── deps/                  # Dependencies (gitignored)
```

See [project_structure.md](project_structure.md) for details.

### Run tests

```bash
basl test                          # All *_test.basl in current directory
basl test ./math/                  # All tests under ./math/
basl test ./math/math_test.basl    # Specific test file
basl test -run sqrt                # Tests matching "sqrt"
```

Test files must:
- End with `_test.basl`
- Import `"t"` module
- Define functions starting with `test_`

Example test:
```c
import "t";

fn test_addition() -> void {
    t.assert(2 + 2 == 4, "addition works");
}
```

See [stdlib/test.md](stdlib/test.md) for test framework details.

## Packaging

### Package applications or libraries

```bash
basl package                        # Auto-detect: executable or library
basl package ./myapp                # Package specific project
basl package script.basl            # Package single script
basl package -o dist/myapp          # Custom output path
```

**Applications** (with `main.basl`):
- Creates a single-file executable
- Contains the BASL interpreter + your source
- No external dependencies needed

**Libraries** (without `main.basl`):
- Creates a distributable directory bundle
- Contains library source files from `lib/`
- Users drop into their `deps/` directory

See [package_cli.md](package_cli.md) for complete documentation.

### Inspect packaged binary

```bash
basl package --inspect ./myapp
```

Shows what files are bundled in a packaged executable.

### How it works

`basl package` creates a self-contained executable by:
1. Copying the current `basl` binary
2. Appending a zip archive with your BASL source files
3. Adding metadata so the binary runs your app on startup

The packaged app runs on the same platform (no cross-compilation).

See [package_cli.md](package_cli.md) for complete documentation.

## Asset Embedding

### Embed files as BASL code

```bash
basl embed logo.png                     # → logo_png.basl
basl embed config.json -o cfg.basl      # Custom output name
basl embed logo.png icon.ico -o assets.basl  # Multiple files
```

### Options

- `--compress` - Compress data with gzip (default for files > 1KB)
- `--no-compress` - Disable compression

### Generated code

Creates a BASL module with:
- `data()` function returning the file contents as a string
- `size()` function returning the original file size

Example usage:
```c
import "logo_png";

fn main() -> i32 {
    string png_data = logo_png.data();
    i32 size = logo_png.size();
    fmt.println(f"Logo size: {size} bytes");
    return 0;
}
```

See [embed.md](embed.md) for details.

## Documentation

### Show documentation

See [Doc Command](doc_cli.md) for complete guide.

```bash
basl doc lib/utils.basl         # Show all public symbols
basl doc lib/utils.basl trim    # Show specific symbol
```

Extracts documentation from comments in BASL source files.

### Show help

```bash
basl help                   # General help
basl help package           # Help for specific command
basl help imports           # Help for specific topic
```

## Editor Support

### Install editor plugins

```bash
basl editor vim             # Install Vim syntax highlighting
basl editor vscode          # Install VS Code extension
```

Vim: Copies syntax files to `~/.vim/`
VS Code: Copies extension to `~/.vscode/extensions/`

See [editor_cli.md](editor_cli.md) for manual installation.

## Version Information

```bash
basl --version              # Show BASL version
```

## Environment Variables

- `BASL_PATH` - Additional directories to search for imports (colon-separated)

## Exit Codes

- `0` - Success
- `1` - Runtime error or failed assertion
- `2` - Syntax error or compilation error
- Other - System error

## Common Workflows

### Development workflow

```bash
# Create project
basl new myapp
cd myapp

# Write code in main.basl and lib/

# Run during development
basl main.basl

# Format code
basl fmt ./...

# Run tests
basl test

# Package for distribution
basl package -o dist/myapp
```

### Single-file script workflow

```bash
# Write script.basl

# Run it
basl script.basl

# Format it
basl fmt script.basl

# Debug it
basl debug script.basl

# Package it
basl package script.basl -o myscript
```

### CI/CD workflow

```bash
# Check formatting
basl fmt --check ./...

# Run tests
basl test

# Build package
basl package -o dist/myapp
```
