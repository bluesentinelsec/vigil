# VIGIL Agent Guidance

## Project Intent

- VIGIL is the "VIGIL Scripting Language", implemented as a bytecode interpreter and scripting language runtime.
- Optimize for explicitness, predictability, portability, batteries-included tooling, and easy distribution of CLI apps, graphical programs, and libraries.
- VIGIL should stay easy to build on modern systems with a C compiler. Avoid casual dependency growth.

## Engineering Rules

- Write plain, readable C11. Prefer simple control flow over clever abstractions.
- Keep the core interpreter, compiler, checker, and runtime portable. Do not add platform headers or OS-specific logic to core files; isolate that in `src/platform/`.
- Use centralized cleanup with `goto` in C functions that acquire multiple resources.
- Fail fast with clear diagnostics instead of hidden fallback behavior.
- Preserve explicit APIs and ownership. Match existing allocator and runtime conventions.
- If VIGIL needs a reusable capability, consider whether it belongs as a generalized stdlib or platform-layer building block.

## Testing And Validation

- Code changes should usually include tests unless the change is docs-only or purely mechanical.
- Prefer table-style coverage when multiple inputs exercise the same rule.
- Common validation commands:
  - `make build`
  - `make test`
  - `ctest --test-dir build --output-on-failure`
- Integration tests depend on `VIGIL_BIN`; the CMake test targets already wire that up.

## Repository Map

- `include/vigil/`: public C API
- `src/`: interpreter, compiler, checker, runtime, CLI, stdlib, and platform code
- `src/internal/`: private internal headers
- `tests/`: C unit tests using the local `vigil_test.h` harness
- `integration_tests/`: Python integration coverage for CLI behavior
- `examples/`: VIGIL programs and package-layout examples
- `docs/`: language, structure, and portability references

## Documentation

- Keep these references aligned with behavior:
  - `docs/syntax.md`
  - `docs/project_structure.md`
  - `docs/stdlib-portability.md`
- If code changes cause drift, update the relevant docs in the same change.

## Workflow

- Keep changes scoped and reviewable.
- Use git commands as part of content-editing work, e.g., use a feature branch per task and target `main` unless told to target a particular release.
- You may use the 'gh' github client for convenience
