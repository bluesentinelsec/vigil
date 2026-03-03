# BASL

**Blazingly Awesome Scripting Language**

BASL is a statically-typed, C-syntax scripting language that prioritizes readability, predictability, and explicitness. It's designed for systems scripting, automation, and rapid prototyping with a comprehensive standard library. Batteries included. Ship self-contained binaries for production distribution. Implemented in Go.

## Features

- **C-like syntax** with modern conveniences (type inference, multi-return, f-strings)
- **Static typing** with runtime enforcement
- **Rich standard library** including HTTP, SQLite, JSON, crypto, compression, and more
- **FFI support** for calling native C libraries
- **Comprehensive tooling**:
  - `basl new` - Create projects with standard layout
  - `basl fmt` - Format code automatically
  - `basl test` - Run tests with built-in framework
  - `basl debug` - Interactive debugger with breakpoints
  - `basl package` - Build standalone executables or library bundles
  - `basl embed` - Embed assets as code
  - `basl doc` - Generate documentation
  - `basl get`, `basl deps` - Dependency management
  - `basl editor` - Install editor integrations

## Quick Start

### Installation

```bash
# Clone the repository
git clone https://github.com/bluesentinelsec/basl.git
cd basl

# Build the binary
make build

# Optionally, move to your PATH
sudo mv basl /usr/local/bin/
```

### Hello World

Create `hello.basl`:

```c
import "fmt";

fn main() -> i32 {
    fmt.println("Hello, World!");
    return 0;
}
```

Run it:

```bash
basl hello.basl
```

All scripts must use the `.basl` file extension.

### Interactive REPL

```bash
basl
>>> import "fmt";
>>> i32 x = 42;
>>> fmt.println(f"x = {x}");
x = 42
```

## Language Overview

### Types

```c
i32 num = 42;
f64 pi = 3.14;
string name = "Alice";
bool flag = true;
array<i32> nums = [1, 2, 3];
map<string, i32> scores = {"alice": 95, "bob": 87};
```

### Functions

```c
fn add(i32 a, i32 b) -> i32 {
    return a + b;
}

// Multi-return for error handling
fn divide(i32 a, i32 b) -> (i32, err) {
    if (b == 0) {
        return (0, err("division by zero", err.arg));
    }
    return (a / b, ok);
}
```

### Control Flow

```c
// if/else
if (x > 10) {
    fmt.println("big");
} else {
    fmt.println("small");
}

// for loop
for (i32 i = 0; i < 10; i++) {
    fmt.println(f"{i}");
}

// for-in
for val in arr {
    fmt.println(val);
}

// switch
switch (x) {
    case 1:
        fmt.println("one");
    case 2, 3:
        fmt.println("two or three");
    default:
        fmt.println("other");
}
```

### String Interpolation

```c
string name = "Alice";
i32 age = 30;
fmt.println(f"Name: {name}, Age: {age}");
fmt.println(f"Next year: {age + 1}");

// Format specifiers
f64 pi = 3.14159;
fmt.println(f"pi={pi:.2f}");  // pi=3.14
```

### Classes

```c
class Person {
    pub string name;
    pub i32 age;
    
    fn init(string name, i32 age) -> void {
        self.name = name;
        self.age = age;
    }
    
    pub fn greet() -> void {
        fmt.println(f"Hello, I'm {self.name}");
    }
}

Person p = Person("Alice", 30);
p.greet();
```

## Standard Library

BASL includes a comprehensive standard library:

- **I/O & Formatting**: `fmt`, `io`, `log`
- **System**: `os`, `file`, `path`, `time`
- **Data Structures**: `strings`, `sort`
- **Serialization**: `json`, `xml`, `csv`, `base64`, `hex`
- **Networking**: `http`, `tcp`, `udp`, `mime`
- **Database**: `sqlite`
- **Cryptography**: `crypto`, `hash`, `rand`
- **Compression**: `compress`, `archive`
- **Utilities**: `regex`, `args`, `test`
- **Advanced**: `thread`, `mutex`, `unsafe`, `ffi`
- **Graphics**: `rl` (Raylib bindings)

See [docs/stdlib/](docs/stdlib/) for complete reference.

## CLI Tools

Full reference: [CLI Documentation](docs/cli.md)

### Run Scripts

```bash
basl script.basl              # Run a script
basl script.basl arg1 arg2    # Pass arguments
```

### Format Code

```bash
basl fmt script.basl          # Format one file
basl fmt ./src/...            # Format all .basl files recursively
basl fmt --check script.basl  # Check formatting (CI mode)
```

### Debug

See [Debugger Guide](docs/debugger.md) for detailed usage.

```bash
basl debug script.basl        # Start interactive debugger
```

Debugger commands: `break`, `continue`, `step`, `next`, `print`, `list`, `quit`

### Project Management

See [Project Structure](docs/project_structure.md) for conventions.

```bash
basl new myapp                # Create new application (with main.basl entry point)
basl new mylib --lib          # Create new library (no main.basl, for reusable code)
basl test                     # Run tests in test/ directory
```

### Package Applications and Libraries

See [Packaging Guide](docs/package_cli.md) for complete documentation.

```bash
basl package                  # Auto-detects: executable or library bundle
```

Applications (with `main.basl`) become standalone executables.
Libraries (without `main.basl`) become distributable directory bundles.

### Embed Assets

See [Embedding Guide](docs/embed.md) for details.

```bash
basl embed logo.png           # Generate BASL module from file
```

### Editor Support

See [Editor Setup](docs/editor_cli.md) for installation.

```bash
basl editor install vim       # Install Vim syntax highlighting
basl editor install vscode    # Install VS Code extension
```

### Documentation

See [Doc Command](docs/doc_cli.md) for details.

```bash
basl doc lib/utils.basl         # Show module documentation
basl doc lib/utils.basl trim    # Show specific symbol
```

### Help

```bash
basl --help                      # Show general help
basl -h                          # Short form
basl help                        # Alternative form
basl help <command>              # Help for specific command
basl help package                # Example: packaging help
basl <command> --help            # Command-specific help
basl fmt --help                  # Example: formatter help
```

## Examples

See the [examples/](examples/) directory for practical examples:

- [hello.basl](examples/hello.basl) - Hello World
- [http_server.basl](examples/http_server.basl) - Simple HTTP server
- [file_operations.basl](examples/file_operations.basl) - File I/O
- [json_parsing.basl](examples/json_parsing.basl) - JSON handling
- [error_handling.basl](examples/error_handling.basl) - Error handling patterns
- [concurrency.basl](examples/concurrency.basl) - Threads and mutexes
- [classes.basl](examples/classes.basl) - Object-oriented programming
- [cli_tool.basl](examples/cli_tool.basl) - Command-line argument parsing

## Documentation

- [CLI Reference](docs/cli.md) - Complete command-line tool reference
- [Language Syntax](docs/syntax.md) - Complete language reference
- [Standard Library](docs/stdlib/) - All stdlib modules
- [Performance](docs/performance.md) - Performance characteristics and benchmarks
- [Debugger](docs/debugger.md) - Debugger usage
- [Packaging](docs/package_cli.md) - Creating executables and library bundles
- [Documentation](docs/doc_cli.md) - Extracting API documentation
- [Dependencies](docs/dependencies.md) - Dependency management
- [Embedding](docs/embed.md) - Embed BASL in Go applications
- [Editor Support](docs/editor_cli.md) - Vim and VS Code setup
- [Project Structure](docs/project_structure.md) - Project layout conventions
- [Maintainer Guide](docs/maintainer_guide.md) - Guide for maintaining BASL

## Development

### Build Basl from Source

```bash
make build        # Build binary
make test         # Run all tests
make fmt          # Format Go code
make clean        # Clean build artifacts
```

### Requirements

- Go 1.21 or later
- Python 3 (for integration tests)

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.
