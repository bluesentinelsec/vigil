"""Integration tests for 'vigil doc' command."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


def resolve_vigil_command():
    env_bin = os.environ.get("VIGIL_BIN")
    if env_bin:
        return [env_bin]
    root = Path(__file__).resolve().parent.parent
    release_bin = root / "build" / "RELEASE" / "vigil"
    debug_bin = root / "build" / "vigil"
    if release_bin.exists():
        return [str(release_bin)]
    return [str(debug_bin)]


STDLIB_MODULES = [
    "args",
    "atomic",
    "compress",
    "crypto",
    "csv",
    "ffi",
    "fmt",
    "fs",
    "http",
    "log",
    "math",
    "net",
    "parse",
    "random",
    "readline",
    "regex",
    "test",
    "thread",
    "time",
    "unsafe",
    "url",
    "yaml",
]

STDLIB_SYMBOLS = [
    "args.count",
    "args.at",
    "atomic.exchange",
    "atomic.fetch_or",
    "atomic.fetch_and",
    "atomic.fetch_xor",
    "atomic.fence",
    "readline.history_clear",
    "readline.history_load",
    "readline.history_save",
    "thread.detach",
    "thread.mutex_destroy",
    "thread.wait_timeout",
    "thread.cond_destroy",
    "thread.rwlock_destroy",
    "math.pi",
    "math.remap",
    "unsafe.get_f32",
    "unsafe.alignof",
    "unsafe.cb_alloc",
]


class TestVigilDoc(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_doc_")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _write_file(self, name, content):
        path = Path(self.tmpdir) / name
        path.write_text(content)
        return str(path)

    def _run_doc(self, *args):
        cmd = [*resolve_vigil_command(), "doc", *args]
        return subprocess.run(cmd, capture_output=True, text=True, timeout=10)

    def test_module_view(self):
        """vigil doc <file> shows public symbols."""
        path = self._write_file("sample.vigil",
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
        """vigil doc <file> <symbol> shows specific symbol."""
        path = self._write_file("lookup.vigil",
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
        """vigil doc <file> <missing> exits with error."""
        path = self._write_file("empty.vigil",
            "pub fn hello() -> void {\n"
            "}\n"
        )
        result = self._run_doc(path, "missing")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not found", result.stderr)

    def test_missing_file(self):
        """vigil doc <nonexistent> exits with error."""
        result = self._run_doc("/tmp/nonexistent_vigil_file.vigil")
        self.assertNotEqual(result.returncode, 0)

    def test_help_shows_doc(self):
        """vigil --help should list the doc command."""
        cmd = [*resolve_vigil_command(), "--help"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        output = result.stdout + result.stderr
        self.assertIn("doc", output)

    def test_all_stdlib_modules_render(self):
        """vigil doc <module> should work for every stdlib module."""
        for module in STDLIB_MODULES:
            with self.subTest(module=module):
                result = self._run_doc(module)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(module, result.stdout)

    def test_recently_missing_stdlib_symbols_render(self):
        """vigil doc <module.symbol> should work for historically missing stdlib docs."""
        for symbol in STDLIB_SYMBOLS:
            with self.subTest(symbol=symbol):
                result = self._run_doc(symbol)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(symbol, result.stdout)


if __name__ == "__main__":
    unittest.main()
