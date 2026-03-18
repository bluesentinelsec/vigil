#!/usr/bin/env python3
"""Integration tests for BASL csv module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

BASL_BIN = os.environ.get("BASL_BIN", "./build/basl")


def run_basl(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="basl_csv_") as tmpdir:
        path = Path(tmpdir) / "test.basl"
        path.write_text(code)
        result = subprocess.run(
            [BASL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class CsvParseRowTest(unittest.TestCase):
    def test_parse_row_simple(self):
        code = r'''import "csv";
fn main() -> i32 {
    array<string> row = csv.parse_row("a,b,c");
    if (row[0] == "a" && row[1] == "b" && row[2] == "c") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_parse_row_quoted(self):
        code = r'''import "csv";
fn main() -> i32 {
    array<string> row = csv.parse_row("\"hello, world\",b");
    if (row[0] == "hello, world") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_parse_row_escaped_quote(self):
        code = r'''import "csv";
fn main() -> i32 {
    array<string> row = csv.parse_row("\"say \"\"hi\"\"\",b");
    if (row[0] == "say \"hi\"") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class CsvStringifyRowTest(unittest.TestCase):
    def test_stringify_row_simple(self):
        code = r'''import "csv";
fn main() -> i32 {
    array<string> row = ["x", "y", "z"];
    string out = csv.stringify_row(row);
    if (out == "x,y,z") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_stringify_row_with_comma(self):
        code = r'''import "csv";
fn main() -> i32 {
    array<string> row = ["hello, world", "b"];
    string out = csv.stringify_row(row);
    if (out == "\"hello, world\",b") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_stringify_row_with_quote(self):
        code = r'''import "csv";
fn main() -> i32 {
    array<string> row = ["say \"hi\"", "b"];
    string out = csv.stringify_row(row);
    if (out == "\"say \"\"hi\"\"\",b") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class CsvRoundtripTest(unittest.TestCase):
    def test_roundtrip(self):
        code = r'''import "csv";
fn main() -> i32 {
    array<string> row = ["a", "b", "c"];
    string line = csv.stringify_row(row);
    array<string> parsed = csv.parse_row(line);
    if (parsed[0] == "a" && parsed[1] == "b" && parsed[2] == "c") { return 0; }
    return 1;
}'''
        rc, out, err = run_basl(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()
