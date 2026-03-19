# Coverage Gate

This directory defines the Linux coverage gate for VIGIL.

## Policy

- Coverage is collected from the real instrumented `vigil` binary and `vigil_tests`.
- The gate enforces:
  - a global line coverage floor for instrumented `src/` code
  - a changed-line coverage floor for modified `src/*.c` files
- The changed-line floor is stricter than the global floor because new behavior
  should be covered even if legacy code still needs work.

## Files

- `thresholds.json`: global and changed-line coverage thresholds
- `test_surface.json`: required test ownership for stdlib modules

## Notes

- The quantitative gate is Linux-only.
- Cross-platform files that do not produce coverage data on Linux are reported as
  unavailable rather than failing the job outright.

## Local Usage

Install the local coverage dependency and run:

```sh
pip install gcovr pexpect
make coverage
```
