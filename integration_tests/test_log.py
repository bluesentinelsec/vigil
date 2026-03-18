#!/usr/bin/env python3
"""Integration tests for BASL log module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

BASL_BIN = os.environ.get("BASL_BIN", "./build/basl")


def run_basl(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="basl_log_") as tmpdir:
        path = Path(tmpdir) / "test.basl"
        path.write_text(code)
        result = subprocess.run(
            [BASL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class LogLevelTest(unittest.TestCase):
    def test_set_level_info(self):
        code = '''import "log";
fn main() -> i32 {
    log.set_level("info");
    return 0;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class LogOutputTest(unittest.TestCase):
    def test_info(self):
        code = '''import "log";
fn main() -> i32 {
    log.set_level("info");
    log.info("test message");
    return 0;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertIn("test message", err)

    def test_warn(self):
        code = '''import "log";
fn main() -> i32 {
    log.set_level("warn");
    log.warn("warning message");
    return 0;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertIn("warning message", err)

    def test_error(self):
        code = '''import "log";
fn main() -> i32 {
    log.set_level("error");
    log.error("error message");
    return 0;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertIn("error message", err)

    def test_debug_filtered(self):
        code = '''import "log";
fn main() -> i32 {
    log.set_level("info");
    log.debug("debug message");
    return 0;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertNotIn("debug message", err)

    def test_debug_shown(self):
        code = '''import "log";
fn main() -> i32 {
    log.set_level("debug");
    log.debug("debug message");
    return 0;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")
        self.assertIn("debug message", err)


if __name__ == "__main__":
    unittest.main()
