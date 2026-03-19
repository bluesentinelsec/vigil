# Performance Regression Benchmarks

This directory defines the Linux-only performance regression gate used in CI.

## Design

- Benchmarks execute the `vigil` CLI in a fresh subprocess for each run.
- Each benchmark reports elapsed time and peak resident memory (`max RSS`).
- CI compares the current PR against `origin/main` on the same GitHub runner.
- A benchmark only fails the job when it exceeds both:
  - a relative regression threshold
  - an absolute regression floor

That double-threshold rule keeps the signal actionable and avoids failing on tiny,
noisy changes.

## Files

- `manifest.json`: benchmark cases and run settings
- `thresholds.json`: default and per-benchmark regression thresholds
- `cases/`: checked-in VIGIL programs used by the benchmark runner

## Local Usage

Build a release binary, then run:

```sh
python3 scripts/run_benchmarks.py \
  --vigil-bin ./build/vigil \
  --manifest benchmarks/manifest.json \
  --output ./build/benchmarks.json
```

Compare two result sets:

```sh
python3 scripts/compare_benchmarks.py \
  --baseline ./baseline.json \
  --candidate ./build/benchmarks.json \
  --thresholds benchmarks/thresholds.json
```

## Updating Thresholds

If a change intentionally trades performance for correctness or maintainability:

1. Run the benchmark suite before and after the change.
2. Confirm the regression is real and justified.
3. Update `thresholds.json` in the same PR with a short explanation in the PR body.

Do not loosen thresholds to mask flaky benchmarks. Fix or remove flaky cases instead.
