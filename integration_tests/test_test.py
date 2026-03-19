"""Integration tests for vigil test."""

import os
import subprocess
import tempfile
import unittest

VIGIL_BIN = os.environ.get("VIGIL_BIN", "vigil")
# Resolve to absolute path so cwd changes don't break it.
if not os.path.isabs(VIGIL_BIN):
    VIGIL_BIN = os.path.abspath(VIGIL_BIN)


def run_test(*args, cwd=None):
    return subprocess.run(
        [VIGIL_BIN, "test"] + list(args),
        capture_output=True, text=True, timeout=30, cwd=cwd
    )


class TestVigilTest(unittest.TestCase):

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def _write(self, relpath, content):
        path = os.path.join(self.tmpdir, relpath)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(content)
        return path

    def test_passing(self):
        self._write("pass_test.vigil",
            'import "test";\n'
            'fn test_ok(test.T t) -> void {\n'
            '    t.assert(true, "should pass");\n'
            '}\n')
        r = run_test(os.path.join(self.tmpdir, "pass_test.vigil"))
        self.assertEqual(r.returncode, 0)
        self.assertIn("PASS: 1 passed", r.stdout)

    def test_failing(self):
        self._write("fail_test.vigil",
            'import "test";\n'
            'fn test_bad(test.T t) -> void {\n'
            '    t.assert(false, "expected failure");\n'
            '}\n')
        r = run_test(os.path.join(self.tmpdir, "fail_test.vigil"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("FAIL: test_bad", r.stdout)
        self.assertIn("expected failure", r.stdout)

    def test_fail_method(self):
        self._write("tfail_test.vigil",
            'import "test";\n'
            'fn test_explicit(test.T t) -> void {\n'
            '    t.fail("boom");\n'
            '}\n')
        r = run_test(os.path.join(self.tmpdir, "tfail_test.vigil"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("boom", r.stdout)

    def test_verbose(self):
        self._write("v_test.vigil",
            'import "test";\n'
            'fn test_one(test.T t) -> void {\n'
            '    t.assert(true, "ok");\n'
            '}\n')
        r = run_test(os.path.join(self.tmpdir, "v_test.vigil"), "-v")
        self.assertEqual(r.returncode, 0)
        self.assertIn("=== RUN   test_one", r.stdout)
        self.assertIn("--- PASS: test_one", r.stdout)

    def test_run_filter(self):
        self._write("filter_test.vigil",
            'import "test";\n'
            'fn test_alpha(test.T t) -> void { t.assert(true, "ok"); }\n'
            'fn test_beta(test.T t) -> void { t.fail("should not run"); }\n')
        r = run_test(os.path.join(self.tmpdir, "filter_test.vigil"),
                     "-run", "alpha", "-v")
        self.assertEqual(r.returncode, 0)
        self.assertIn("test_alpha", r.stdout)
        self.assertNotIn("test_beta", r.stdout)

    def test_directory_discovery(self):
        self._write("sub/a_test.vigil",
            'import "test";\n'
            'fn test_a(test.T t) -> void { t.assert(true, "ok"); }\n')
        self._write("sub/b_test.vigil",
            'import "test";\n'
            'fn test_b(test.T t) -> void { t.assert(true, "ok"); }\n')
        r = run_test(os.path.join(self.tmpdir, "sub"), "-v")
        self.assertEqual(r.returncode, 0)
        self.assertIn("PASS: 2 passed", r.stdout)

    def test_no_test_files(self):
        self._write("regular.vigil", 'fn main() -> i32 { return 0; }\n')
        r = run_test(self.tmpdir)
        self.assertEqual(r.returncode, 0)
        self.assertIn("no test files found", r.stdout)

    def test_project_default_target(self):
        self._write("vigil.toml", '[project]\nname = "myproj"\n')
        self._write("test/my_test.vigil",
            'import "test";\n'
            'fn test_proj(test.T t) -> void { t.assert(true, "ok"); }\n')
        r = run_test(cwd=self.tmpdir)
        self.assertEqual(r.returncode, 0)
        self.assertIn("PASS: 1 passed", r.stdout)

    def test_imported_helper_module(self):
        self._write("helper.vigil",
            'pub fn message() -> string {\n'
            '    return "ok";\n'
            '}\n')
        self._write("import_test.vigil",
            'import "test";\n'
            'import "helper";\n'
            'fn test_import(test.T t) -> void {\n'
            '    t.assert(helper.message() == "ok", "import should resolve");\n'
            '}\n')
        r = run_test(os.path.join(self.tmpdir, "import_test.vigil"))
        self.assertEqual(r.returncode, 0)
        self.assertIn("PASS: 1 passed", r.stdout)

    def test_mixed_pass_fail(self):
        self._write("mix_test.vigil",
            'import "test";\n'
            'fn test_good(test.T t) -> void { t.assert(true, "ok"); }\n'
            'fn test_bad(test.T t) -> void { t.assert(false, "nope"); }\n')
        r = run_test(os.path.join(self.tmpdir, "mix_test.vigil"), "-v")
        self.assertEqual(r.returncode, 1)
        self.assertIn("--- PASS: test_good", r.stdout)
        self.assertIn("--- FAIL: test_bad", r.stdout)
        self.assertIn("1 passed, 1 failed", r.stdout)

    def test_help(self):
        r = run_test("--help")
        self.assertEqual(r.returncode, 0)
        self.assertIn("Usage:", r.stdout)


if __name__ == "__main__":
    unittest.main()
