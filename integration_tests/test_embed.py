"""Integration tests for basl embed."""

import os
import subprocess
import sys
import tempfile
import unittest

BASL_BIN = os.environ.get("BASL_BIN", "basl")


def run_embed(*args):
    return subprocess.run(
        [BASL_BIN, "embed"] + list(args),
        capture_output=True, text=True, timeout=10
    )


class TestBaslEmbed(unittest.TestCase):

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def _write(self, name, content):
        path = os.path.join(self.tmpdir, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(content.encode("utf-8"))
        return path

    def test_single_file(self):
        src = self._write("hello.txt", "Hello!")
        out = os.path.join(self.tmpdir, "hello.basl")
        result = run_embed(src, "-o", out)
        self.assertEqual(result.returncode, 0)
        with open(out) as f:
            text = f.read()
        self.assertIn('import "base64"', text)
        self.assertIn('pub string name = "hello.txt"', text)
        self.assertIn("pub i32 size = 6", text)
        self.assertIn("pub fn bytes()", text)
        self.assertIn("base64.decode(_b64)", text)

    def test_multi_file(self):
        a = self._write("a.txt", "AAA")
        b = self._write("b.txt", "BBB")
        out = os.path.join(self.tmpdir, "assets.basl")
        result = run_embed(a, b, "-o", out)
        self.assertEqual(result.returncode, 0)
        with open(out) as f:
            text = f.read()
        self.assertIn("pub fn get(string path)", text)
        self.assertIn("pub fn list()", text)
        self.assertIn("pub i32 count = 2", text)
        self.assertIn('"a.txt"', text)
        self.assertIn('"b.txt"', text)

    def test_directory(self):
        subdir = os.path.join(self.tmpdir, "assets")
        os.makedirs(os.path.join(subdir, "sub"))
        with open(os.path.join(subdir, "root.txt"), "w") as f:
            f.write("root")
        with open(os.path.join(subdir, "sub", "nested.txt"), "w") as f:
            f.write("nested")
        out = os.path.join(self.tmpdir, "dir.basl")
        result = run_embed(subdir, "-o", out)
        self.assertEqual(result.returncode, 0)
        with open(out) as f:
            text = f.read()
        self.assertIn("pub fn get(string path)", text)
        self.assertIn("sub/nested.txt", text)
        self.assertIn("root.txt", text)

    def test_base64_correctness(self):
        import base64
        src = self._write("data.bin", "test data 123")
        out = os.path.join(self.tmpdir, "data.basl")
        result = run_embed(src, "-o", out)
        self.assertEqual(result.returncode, 0)
        with open(out) as f:
            text = f.read()
        # Extract the base64 string
        for line in text.split("\n"):
            if line.startswith("string _b64"):
                b64_str = line.split('"')[1]
                decoded = base64.b64decode(b64_str).decode("utf-8")
                self.assertEqual(decoded, "test data 123")
                break
        else:
            self.fail("No _b64 variable found")

    def test_no_args(self):
        result = run_embed()
        self.assertEqual(result.returncode, 2)

    def test_missing_file(self):
        result = run_embed("/nonexistent/file.txt", "-o", "/tmp/out.basl")
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
