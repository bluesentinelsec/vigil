# test

Built-in test framework. Available only in `_test.basl` files via `import "test";`.

## Running Tests

```sh
basl test                          # all *_test.basl in cwd (recursive)
basl test ./math/                  # all *_test.basl under ./math/ (recursive)
basl test ./math/math_test.basl    # specific file
basl test -run sqrt                # tests whose name contains "sqrt"
basl test -run "sqrt|trig"         # tests matching any substring (|-separated)
basl test -v                       # verbose: print each test name
```

Exit code: 0 if all pass, 1 if any fail.

**Working directory:** Tests run with the working directory set to the test file's directory, so relative paths in tests are relative to the test file.

## Conventions

- Test files end in `_test.basl`.
- Test functions start with `test_`, return `void`.
- Test functions take a single `test.T` parameter.
- Each test runs in a fresh interpreter — no shared state between tests.
- Non-test functions (helpers) in test files are available but not run as tests.

## Example

```c
import "test";
import "math";

fn test_sqrt(test.T t) -> void {
    t.assert(math.sqrt(9.0) == 3.0, "sqrt(9) should be 3");
}

fn test_pow(test.T t) -> void {
    t.assert(math.pow(2.0, 3.0) == 8.0, "2^3 should be 8");
}
```

## Methods

### t.assert(bool condition, string message)

Fails the current test if `condition` is false. The test stops immediately.

```c
fn test_example(test.T t) -> void {
    t.assert(1 + 1 == 2, "basic math");
}
```

### t.fail(string message)

Fails the current test unconditionally. The test stops immediately.

```c
fn test_not_implemented(test.T t) -> void {
    t.fail("TODO: implement this");
}
```

## Output

Default (quiet) — only failures:
```
--- FAIL: test_bad (math_test.basl)
    expected positive result
FAIL    math_test.basl    0.003s
```

Verbose (`-v`):
```
=== RUN   test_sqrt
--- PASS: test_sqrt (0.000s)
=== RUN   test_floor
--- PASS: test_floor (0.000s)
ok      math_test.basl    0.001s
```
