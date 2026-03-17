#!/usr/bin/env python3
"""Integration tests for BASL fs module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

BASL_BIN = os.environ.get("BASL_BIN", "./build/basl")


def run_basl(code: str) -> tuple[int, str, str]:
    """Run BASL code and return (exit_code, stdout, stderr)."""
    with tempfile.TemporaryDirectory(prefix="basl_fs_") as tmpdir:
        path = Path(tmpdir) / "test.basl"
        path.write_text(code)
        result = subprocess.run(
            [BASL_BIN, "run", str(path)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class FsPathTest(unittest.TestCase):
    """Tests for path operations"""

    def test_join(self):
        code = '''import "fs";
fn main() -> i32 {
    if (fs.join("a", "b") == "a/b") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_clean(self):
        code = '''import "fs";
fn main() -> i32 {
    if (fs.clean("a/./b/../c") == "a/c") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_dir(self):
        code = '''import "fs";
fn main() -> i32 {
    if (fs.dir("/foo/bar.txt") == "/foo") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_base(self):
        code = '''import "fs";
fn main() -> i32 {
    if (fs.base("/foo/bar.txt") == "bar.txt") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_ext(self):
        code = '''import "fs";
fn main() -> i32 {
    if (fs.ext("file.txt") == ".txt") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class FsFileTest(unittest.TestCase):
    """Tests for file operations"""

    def test_write_read(self):
        code = '''import "fs";
fn main() -> i32 {
    fs.write("/tmp/basl_test_wr.txt", "hello");
    if (fs.read("/tmp/basl_test_wr.txt") == "hello") {
        fs.remove("/tmp/basl_test_wr.txt");
        return 0;
    }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_exists(self):
        code = '''import "fs";
fn main() -> i32 {
    fs.write("/tmp/basl_test_ex.txt", "x");
    if (fs.exists("/tmp/basl_test_ex.txt")) {
        fs.remove("/tmp/basl_test_ex.txt");
        if (!fs.exists("/tmp/basl_test_ex.txt")) { return 0; }
    }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_copy(self):
        code = '''import "fs";
fn main() -> i32 {
    fs.write("/tmp/basl_test_cp1.txt", "copy");
    fs.copy("/tmp/basl_test_cp1.txt", "/tmp/basl_test_cp2.txt");
    if (fs.read("/tmp/basl_test_cp2.txt") == "copy") {
        fs.remove("/tmp/basl_test_cp1.txt");
        fs.remove("/tmp/basl_test_cp2.txt");
        return 0;
    }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class FsDirTest(unittest.TestCase):
    """Tests for directory operations"""

    def test_mkdir(self):
        code = '''import "fs";
fn main() -> i32 {
    fs.mkdir("/tmp/basl_test_mkdir");
    if (fs.is_dir("/tmp/basl_test_mkdir")) {
        fs.remove("/tmp/basl_test_mkdir");
        return 0;
    }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_mkdir_all(self):
        code = '''import "fs";
fn main() -> i32 {
    fs.mkdir_all("/tmp/basl_test_mkdirall/a/b");
    if (fs.is_dir("/tmp/basl_test_mkdirall/a/b")) {
        fs.remove("/tmp/basl_test_mkdirall/a/b");
        fs.remove("/tmp/basl_test_mkdirall/a");
        fs.remove("/tmp/basl_test_mkdirall");
        return 0;
    }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class FsLocationTest(unittest.TestCase):
    """Tests for standard locations"""

    def test_home_dir(self):
        code = '''import "fs";
fn main() -> i32 {
    string home = fs.home_dir();
    if (home.len() > 0) { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_temp_dir(self):
        code = '''import "fs";
fn main() -> i32 {
    string tmp = fs.temp_dir();
    if (tmp.len() > 0) { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_cwd(self):
        code = '''import "fs";
fn main() -> i32 {
    string cwd = fs.cwd();
    if (cwd.len() > 0) { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
