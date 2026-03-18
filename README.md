# VIGIL

**Blazingly Awesome Scripting Language**

VIGIL is a statically typed, bytecode-compiled scripting language designed for building CLI tools, graphical programs, and libraries. It favors explicit behavior, batteries-included tooling, and easy distribution.

```vigil
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
vigil run hello.vigil
```

Create a new project:

```bash
vigil new myapp
cd myapp
vigil run main.vigil
```

## Tooling

| Command         | Description                                    |
|-----------------|------------------------------------------------|
| `vigil run`      | Run a VIGIL script                              |
| `vigil check`    | Type-check without running                     |
| `vigil test`     | Run tests                                      |
| `vigil fmt`      | Format source files                            |
| `vigil doc`      | Show documentation for modules or source files |
| `vigil debug`    | Debug a script                                 |
| `vigil new`      | Create a new project                           |
| `vigil package`  | Package a program as a standalone binary       |
| `vigil repl`     | Start interactive REPL                         |
| `vigil lsp`      | Start Language Server Protocol server          |
| `vigil embed`    | Embed files as VIGIL source code                |

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
include/vigil/       Public C API headers
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
