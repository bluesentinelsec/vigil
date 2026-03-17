#!/usr/bin/env python3
"""Integration tests for BASL random module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

BASL_BIN = os.environ.get("BASL_BIN", "./build/basl")


def run_basl(code: str) -> tuple[int, str, str]:
    """Run BASL code and return (exit_code, stdout, stderr)."""
    with tempfile.TemporaryDirectory(prefix="basl_random_") as tmpdir:
        path = Path(tmpdir) / "test.basl"
        path.write_text(code)
        result = subprocess.run(
            [BASL_BIN, "run", str(path)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class RandomSeedTest(unittest.TestCase):
    """Tests for random.seed()"""

    def test_seed_reproducible(self):
        """Same seed produces same sequence."""
        code = '''import "random";
fn main() -> i32 {
    random.seed(12345);
    i32 a = random.i32();
    random.seed(12345);
    i32 b = random.i32();
    if (a == b) { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class RandomI32Test(unittest.TestCase):
    """Tests for random.i32()"""

    def test_i32_returns_value(self):
        code = '''import "random";
fn main() -> i32 {
    random.seed(42);
    i32 n = random.i32();
    return 0;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class RandomF64Test(unittest.TestCase):
    """Tests for random.f64()"""

    def test_f64_in_range(self):
        """f64 returns value in [0, 1)."""
        code = '''import "random";
fn main() -> i32 {
    random.seed(42);
    f64 x = random.f64();
    if (x >= 0.0 && x < 1.0) { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class RandomRangeTest(unittest.TestCase):
    """Tests for random.range()"""

    def test_range_basic(self):
        """range returns value in [min, max)."""
        code = '''import "random";
fn main() -> i32 {
    random.seed(42);
    i32 r = random.range(10, 20);
    if (r >= 10 && r < 20) { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_range_single_value(self):
        """range with min == max returns min."""
        code = '''import "random";
fn main() -> i32 {
    random.seed(42);
    i32 r = random.range(5, 5);
    if (r == 5) { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
