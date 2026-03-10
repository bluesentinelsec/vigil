# gui API Freeze (`v0.1.3`)

This document freezes the BASL `gui` public API for the `release/v0.1.3` line.

Purpose:
- Keep the API stable while Linux and Windows backends are onboarded.
- Prevent accidental renames/signature drift while refining backend internals.
- Give application authors a reliable contract for non-trivial desktop apps.

## Scope

Frozen surface for `v0.1.3` includes:
- Module name: `import "gui";`
- Top-level constructors and helpers in [gui.md](./gui.md)
- All exported `gui.*Opts` types and their documented fields
- All widget classes and their documented methods
- Dialog semantics documented in [gui.md](./gui.md)

Backend implementation details are not part of the freeze contract.

## Stability Guarantees

For `release/v0.1.3`, the following are stable:
- Existing function names and method names
- Existing parameter counts/order and return shapes
- Existing option field names and types
- Existing class names
- Explicit `err`-return behavior (no exceptions, no implicit success/failure)

Behavioral guarantees:
- `gui.supported()` and `gui.backend()` are side-effect free capability queries.
- Constructor/runtime failures are reported as `err` values.
- `open_file`, `save_file`, and `open_directory` return `""` with `err == ok` on user cancel.

## Change Policy

Breaking changes for `v0.1.3` (not allowed without version bump):
- Renaming/removing functions, methods, classes, or options fields
- Changing parameter order/count or return tuple shapes
- Changing dialog cancel behavior to return non-`ok` errors

Allowed non-breaking changes:
- Adding new functions/methods/fields (additive only)
- Improving error messages/kinds where contract semantics stay intact
- Backend performance/robustness fixes that preserve API behavior

## Enforcement

The API freeze is enforced by checker snapshot test coverage:
- [`pkg/basl/checker/gui_api_freeze_test.go`](/Users/michaellong/projects/basl/pkg/basl/checker/gui_api_freeze_test.go)

Any API drift in the checker model will fail CI and require an intentional freeze update.
