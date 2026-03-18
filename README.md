# BASL

**Blazingly Awesome Scripting Language**

BASL is a statically typed, bytecode-compiled scripting language designed for building CLI tools, graphical programs, and libraries. It favors explicit behavior, batteries-included tooling, and easy distribution.

```basl
import "fmt";

fn main() -> i32 {
    fmt.println("Hello, World!");
    return 0;
}
```

## Quick Start

```bash
make build
make test
```

Run a program:

```bash
basl run hello.basl
```

Create a new project:

```bash
basl new myapp
cd myapp
basl run main.basl
```

## Tooling

| Command         | Description                                    |
|-----------------|------------------------------------------------|
| `basl run`      | Run a BASL script                              |
| `basl check`    | Type-check without running                     |
| `basl test`     | Run tests                                      |
| `basl fmt`      | Format source files                            |
| `basl doc`      | Show documentation for modules or source files |
| `basl debug`    | Debug a script                                 |
| `basl new`      | Create a new project                           |
| `basl package`  | Package a program as a standalone binary       |
| `basl repl`     | Start interactive REPL                         |
| `basl lsp`      | Start Language Server Protocol server          |
| `basl embed`    | Embed files as BASL source code                |

## Language Highlights

- Static typing with type inference for locals
- First-class functions and closures
- Classes, interfaces, and enums
- Multi-return values and explicit error handling (`guard`)
- `defer` for cleanup
- Built-in concurrency primitives
- Standard library: `fmt`, `math`, `os`, `fs`, `net`, `json`, `time`, `crypto`, and more
- Portable across Linux, macOS, Windows, and WebAssembly

## Repository Layout

```
include/basl/       Public C API headers
src/                Compiler, VM, runtime, CLI, stdlib, platform layer
tests/              Unit tests
integration_tests/  CLI integration tests
examples/           Example programs
docs/               Language and project documentation
```

## Documentation

- [Syntax Reference](docs/syntax.md)
- [Project Structure](docs/project_structure.md)
- [Stdlib Portability](docs/stdlib-portability.md)

## License

Apache License 2.0 — see [LICENSE](LICENSE) for details.
