# Complexity And Readability Gate

This directory defines the Linux complexity/readability gate for VIGIL.

## Policy

- The gate analyzes the entire first-party C codebase:
  - `src/`
  - `include/`
  - `tests/`
- `lizard` enforces hard thresholds for function complexity, length, and parameter
  count.
- New functions must meet a stricter cyclomatic-complexity bar than legacy
  code.
- Existing functions use scoped cyclomatic-complexity thresholds based on:
  - recent git churn
  - explicit path overrides for hot code
- The gate compares the current PR against `origin/main` and only fails when:
  - a new function exceeds the thresholds
  - an existing above-threshold function becomes worse
  - the PR introduces new `clang-tidy` readability diagnostics

That keeps the gate actionable on a legacy codebase while still forcing the
overall debt curve downward over time.

## Files

- `thresholds.json`: hard limits used by the `lizard` comparison script
- `debt/`: checked-in snapshot of the current inherited complexity debt

## Threshold Model

- Default legacy threshold:
  - `ccn <= 20`
- New functions:
  - `ccn <= 10`
- Churn tiers over the last `180` days:
  - `high` churn: `ccn <= 10`
  - `medium` churn: `ccn <= 12`
  - `low` churn: `ccn <= 20`
- Path overrides tighten thresholds for actively edited CLI code and all tests.

Threshold resolution is applied in this order:

1. defaults
2. churn tier
3. path override
4. new-function override

That keeps new code strict while ratcheting maintainability in the parts of the
repo people touch most often.

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
