"""Integration tests for vigil new."""

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
    return [str(Path(__file__).resolve().parent.parent / "build" / "RELEASE" / "vigil")]


class TestVigilNew(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="vigil_new_")

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_new_app_default(self):
        result = subprocess.run(
            [*resolve_vigil_command(), "new", "demo"],
            cwd=self.tmpdir,
            capture_output=True,
            text=True,
            timeout=10,
        )
        project_dir = Path(self.tmpdir) / "demo"
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertTrue((project_dir / "vigil.toml").exists())
        self.assertTrue((project_dir / "main.vigil").exists())
        self.assertTrue((project_dir / "lib").is_dir())
        self.assertTrue((project_dir / "test").is_dir())

    def test_new_prompts_for_name(self):
        result = subprocess.run(
            [*resolve_vigil_command(), "new"],
            cwd=self.tmpdir,
            input="prompted_demo\n",
            capture_output=True,
            text=True,
            timeout=10,
        )
        project_dir = Path(self.tmpdir) / "prompted_demo"
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertTrue(project_dir.exists())
        self.assertIn("created prompted_demo", result.stdout)

    def test_new_rejects_empty_prompt_name(self):
        result = subprocess.run(
            [*resolve_vigil_command(), "new"],
            cwd=self.tmpdir,
            input="\n",
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("project name cannot be empty", result.stderr)

    def test_new_lib_scaffold(self):
        result = subprocess.run(
            [*resolve_vigil_command(), "new", "demo_lib", "--lib"],
            cwd=self.tmpdir,
            capture_output=True,
            text=True,
            timeout=10,
        )
        project_dir = Path(self.tmpdir) / "demo_lib"
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertTrue((project_dir / "lib" / "demo_lib.vigil").exists())
        self.assertTrue((project_dir / "test" / "demo_lib_test.vigil").exists())

    def test_new_scaffold_and_output_dir(self):
        output_dir = Path(self.tmpdir) / "out"
        output_dir.mkdir()

        result = subprocess.run(
            [*resolve_vigil_command(), "new", "demo_scaffold", "--scaffold", "-o", str(output_dir)],
            cwd=self.tmpdir,
            capture_output=True,
            text=True,
            timeout=10,
        )
        project_dir = output_dir / "demo_scaffold"
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertTrue((project_dir / "main.vigil").exists())
        self.assertTrue((project_dir / "lib" / "demo_scaffold.vigil").exists())
        self.assertTrue((project_dir / "test" / "demo_scaffold_test.vigil").exists())

    def test_new_rejects_existing_directory(self):
        project_dir = Path(self.tmpdir) / "taken"
        project_dir.mkdir()

        result = subprocess.run(
            [*resolve_vigil_command(), "new", "taken"],
            cwd=self.tmpdir,
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("already exists", result.stderr)

    def test_new_rejects_name_too_long(self):
        long_name = "a" * 101
        result = subprocess.run(
            [*resolve_vigil_command(), "new", long_name],
            cwd=self.tmpdir,
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("project name too long", result.stderr)


if __name__ == "__main__":
    unittest.main()
