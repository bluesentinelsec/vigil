"""Integration tests for vigil debug."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


def resolve_vigil_command():
    env_bin = os.environ.get("VIGIL_BIN")
    if env_bin:
        return [env_bin]

    repo_root = Path(__file__).resolve().parent.parent
    release_bin = repo_root / "build" / "RELEASE" / "vigil"
    if release_bin.exists():
        return [str(release_bin)]
    return [str(repo_root / "build" / "vigil")]


class TestVigilDebug(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_debug_")

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _write(self, relpath, content):
        path = Path(self.tmpdir) / relpath
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def _run_debug(self, command_input):
        script = self._write(
            "main.vigil",
            "fn main() -> i32 {\n"
            "    i32 value = 1;\n"
            "    return value;\n"
            "}\n",
        )

        return subprocess.run(
            [*resolve_vigil_command(), "debug", "--interactive", str(script)],
            cwd=self.tmpdir,
            input=command_input,
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_debug_interactive_help_and_quit(self):
        result = self._run_debug("help\nq\n")

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("VIGIL Interactive Debugger", result.stdout)
        self.assertIn("Stepped:", result.stdout)
        self.assertIn("Debugger commands:", result.stdout)

    def test_debug_interactive_breakpoint_and_missing_variable(self):
        result = self._run_debug("b main\np missing\nd 0\nq\n")

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("set on function 'main'", result.stdout)
        self.assertIn("Variable 'missing' not found in current scope", result.stdout)
        self.assertIn("Breakpoint 0 deleted", result.stdout)


if __name__ == "__main__":
    unittest.main()
