# BASL

**Blazingly Awesome Scripting Language**

This branch begins the BASL rewrite in C.

The repository currently contains a minimal scaffold that proves the new toolchain and CI shape:

- a public C library in `include/basl/`
- a tiny CLI entrypoint in `src/cli/`
- a split public C API surface with runtime, allocator, and status headers
- a GoogleTest-based unit test
- GitHub Actions builds for Linux, macOS, Windows, and Emscripten

This scaffold is intentionally small. It exists to prove that BASL can build as a portable C project before the real runtime, VM, checker, and standard library work begin.

## Quick Start

```bash
make build
make test
```

The CLI currently acts as a tiny smoke test:

```bash
./build/basl
# prints "basl CLI scaffold"
```

## Current Layout

```text
include/basl/   Public headers
src/            Library and CLI sources
tests/          GoogleTest unit tests
.github/        Cross-platform CI
```

## Rewrite Direction

The near-term C rewrite plan is:

1. Establish a cross-platform scaffold with working CI.
2. Implement foundational runtime data structures.
3. Implement the core language runtime and bytecode VM.
4. Rebuild the standard library.
5. Rebuild related tooling such as formatter, debugger, and LSP support.

The existing BASL language reference in `docs/syntax.md` remains the semantic reference during this early scaffold phase.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
