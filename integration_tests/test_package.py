"""Integration tests for 'basl package' command and args module."""

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


class TestBaslArgs(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="basl_args_")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_args_count_and_at(self):
        """args.count() and args.at() return script arguments."""
        script = Path(self.tmpdir) / "test.basl"
        script.write_text(
            'import "args";\n'
            'import "fmt";\n'
            'fn main() -> i32 {\n'
            '    fmt.println(f"{args.count()}");\n'
            '    return args.count();\n'
            '}\n'
        )
        result = subprocess.run(
            [*resolve_basl_command(), "run", str(script), "a", "b", "c"],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 3)
        self.assertIn("3", result.stdout)

    def test_args_no_args(self):
        """args.count() returns 0 when no script args."""
        script = Path(self.tmpdir) / "test.basl"
        script.write_text(
            'import "args";\n'
            'fn main() -> i32 {\n'
            '    return args.count();\n'
            '}\n'
        )
        result = subprocess.run(
            [*resolve_basl_command(), "run", str(script)],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 0)


class TestBaslPackage(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="basl_pkg_")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_package_and_run(self):
        if sys.platform == "win32":
            self.skipTest("Packaged binary execution unreliable on Windows CI")
        """Package a script and run the standalone binary."""
        script = Path(self.tmpdir) / "hello.basl"
        script.write_text(
            'import "fmt";\n'
            'fn main() -> i32 {\n'
            '    fmt.println("packaged!");\n'
            '    return 42;\n'
            '}\n'
        )
        out_bin = Path(self.tmpdir) / "hello_app"
        result = subprocess.run(
            [*resolve_basl_command(), "package", str(script), "-o", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertTrue(out_bin.exists())

        # Run the packaged binary.
        result = subprocess.run(
            [str(out_bin)], capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 42)
        self.assertIn("packaged!", result.stdout)

    def test_package_with_args(self):
        if sys.platform == "win32":
            self.skipTest("Packaged binary execution unreliable on Windows CI")
        """Packaged binary passes CLI args to script."""
        script = Path(self.tmpdir) / "argtest.basl"
        script.write_text(
            'import "args";\n'
            'fn main() -> i32 {\n'
            '    return args.count();\n'
            '}\n'
        )
        out_bin = Path(self.tmpdir) / "argtest_app"
        subprocess.run(
            [*resolve_basl_command(), "package", str(script), "-o", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        result = subprocess.run(
            [str(out_bin), "x", "y"],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 2)

    def test_package_inspect(self):
        """basl package --inspect shows bundled files."""
        script = Path(self.tmpdir) / "inspect.basl"
        script.write_text('fn main() -> i32 { return 0; }\n')
        out_bin = Path(self.tmpdir) / "inspect_app"
        subprocess.run(
            [*resolve_basl_command(), "package", str(script), "-o", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        result = subprocess.run(
            [*resolve_basl_command(), "package", "--inspect", str(out_bin)],
            capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("entry.basl", result.stdout)

    def test_package_encrypted(self):
        if sys.platform == "win32":
            self.skipTest("Packaged binary execution unreliable on Windows CI")
        """Encrypted package runs correctly."""
        script = Path(self.tmpdir) / "secret.basl"
        script.write_text(
            'import "fmt";\n'
            'fn main() -> i32 {\n'
            '    fmt.println("secret!");\n'
            '    return 7;\n'
            '}\n'
        )
        out_bin = Path(self.tmpdir) / "secret_app"
        subprocess.run(
            [*resolve_basl_command(), "package", str(script),
             "-o", str(out_bin), "--key", "mypassword"],
            capture_output=True, text=True, timeout=10
        )
        result = subprocess.run(
            [str(out_bin)], capture_output=True, text=True, timeout=10
        )
        self.assertEqual(result.returncode, 7)
        self.assertIn("secret!", result.stdout)

        # Verify source is not visible in binary.
        with open(out_bin, "rb") as f:
            binary_data = f.read()
        self.assertNotIn(b"secret!", binary_data[-1000:])


if __name__ == "__main__":
    unittest.main()
