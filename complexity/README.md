# Complexity And Readability Gate

This directory defines the Linux complexity/readability gate for VIGIL.

## Policy

- The gate analyzes the entire first-party C codebase:
  - `src/`
  - `include/`
  - `tests/`
- `lizard` enforces hard thresholds for function complexity, length, and parameter
  count.
- The gate compares the current PR against `origin/main` and only fails when:
  - a new function exceeds the thresholds
  - an existing above-threshold function becomes worse
  - the PR introduces new `clang-tidy` readability diagnostics

That keeps the gate actionable on a legacy codebase while still forcing the
overall debt curve downward over time.

## Files

- `thresholds.json`: hard limits used by the `lizard` comparison script
- `debt/`: checked-in snapshot of the current inherited complexity debt

## Local Usage

Prepare a worktree for `origin/main`, then configure both trees with compile
commands enabled:

```sh
git worktree add ../vigil-main origin/main
cmake -S . -B build-complexity -DVIGIL_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake -S ../vigil-main -B ../vigil-main/build-complexity -DVIGIL_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Install the local tooling:

```sh
pip install lizard
```

Run the checks:

```sh
make complexity BASELINE_ROOT=../vigil-main
```

Or run the steps directly:

```sh
python3 scripts/check_complexity.py \
  --candidate-root . \
  --baseline-root ../vigil-main \
  --thresholds complexity/thresholds.json \
  --summary ./build-complexity/lizard-summary.json

scripts/run_clang_tidy.sh . ./build-complexity ./build-complexity/clang-tidy-pr.log
scripts/run_clang_tidy.sh ../vigil-main ../vigil-main/build-complexity ../vigil-main/build-complexity/clang-tidy-main.log

python3 scripts/check_clang_tidy.py \
  --baseline-log ../vigil-main/build-complexity/clang-tidy-main.log \
  --candidate-log ./build-complexity/clang-tidy-pr.log \
  --summary ./build-complexity/clang-tidy-summary.json
```
