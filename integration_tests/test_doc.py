"""Integration tests for 'basl doc' command."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


def resolve_basl_command():
    env_bin = os.environ.get("BASL_BIN")
    if env_bin:
        return [env_bin]
    return [str(Path(__file__).resolve().parent.parent / "build" / "RELEASE" / "basl")]


class TestBaslDoc(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="basl_doc_")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _write_file(self, name, content):
        path = Path(self.tmpdir) / name
        path.write_text(content)
        return str(path)

    def _run_doc(self, *args):
        cmd = [*resolve_basl_command(), "doc", *args]
        return subprocess.run(cmd, capture_output=True, text=True, timeout=10)

    def test_module_view(self):
        """basl doc <file> shows public symbols."""
        path = self._write_file("sample.basl",
            "// Sample module.\n"
            "\n"
            "// Adds two numbers.\n"
            "pub fn add(i32 a, i32 b) -> i32 {\n"
            "    return a + b;\n"
            "}\n"
            "\n"
            "fn private_fn() -> void {\n"
            "}\n"
        )
        result = self._run_doc(path)
        self.assertEqual(result.returncode, 0)
        self.assertIn("MODULE\n  sample", result.stdout)
        self.assertIn("SUMMARY\n  Sample module.", result.stdout)
        self.assertIn("add(i32 a, i32 b) -> i32", result.stdout)
        self.assertNotIn("private_fn", result.stdout)

    def test_symbol_lookup(self):
        """basl doc <file> <symbol> shows specific symbol."""
        path = self._write_file("lookup.basl",
            "// Adds two numbers.\n"
            "pub fn add(i32 a, i32 b) -> i32 {\n"
            "    return a + b;\n"
            "}\n"
        )
        result = self._run_doc(path, "add")
        self.assertEqual(result.returncode, 0)
        self.assertIn("add(i32 a, i32 b) -> i32", result.stdout)
        self.assertIn("Adds two numbers.", result.stdout)

    def test_missing_symbol(self):
        """basl doc <file> <missing> exits with error."""
        path = self._write_file("empty.basl",
            "pub fn hello() -> void {\n"
            "}\n"
        )
        result = self._run_doc(path, "missing")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not found", result.stderr)

    def test_missing_file(self):
        """basl doc <nonexistent> exits with error."""
        result = self._run_doc("/tmp/nonexistent_basl_file.basl")
        self.assertNotEqual(result.returncode, 0)

    def test_help_shows_doc(self):
        """basl --help should list the doc command."""
        cmd = [*resolve_basl_command(), "--help"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        output = result.stdout + result.stderr
        self.assertIn("doc", output)


if __name__ == "__main__":
    unittest.main()
