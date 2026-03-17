"""Integration tests for basl get (package manager)."""

import os
import subprocess
import tempfile
import unittest

BASL_BIN = os.environ.get("BASL_BIN", "basl")


def run_basl(*args, cwd=None):
    return subprocess.run(
        [BASL_BIN] + list(args),
        capture_output=True,
        text=True,
        cwd=cwd,
    )


class TestPkgGet(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        # Create a test project
        result = run_basl("new", "testproj", cwd=self.tmpdir)
        self.assertEqual(result.returncode, 0)
        self.project_dir = os.path.join(self.tmpdir, "testproj")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_get_help(self):
        result = run_basl("get", "--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("basl get", result.stdout)
        self.assertIn("github.com/user/repo", result.stdout)

    def test_get_no_project(self):
        """basl get outside a project should fail."""
        result = run_basl("get", "github.com/test/repo", cwd=self.tmpdir)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not in a BASL project", result.stderr)

    def test_get_sync_empty(self):
        """basl get with no deps should succeed."""
        result = run_basl("get", cwd=self.project_dir)
        self.assertEqual(result.returncode, 0)
        self.assertIn("syncing", result.stdout)

    def test_get_creates_deps_dir(self):
        """basl get should create deps/ directory."""
        # Add a dep manually to basl.toml
        toml_path = os.path.join(self.project_dir, "basl.toml")
        with open(toml_path, "a") as f:
            f.write('\n[deps]\n"github.com/bluesentinelsec/basl" = "main"\n')
        
        result = run_basl("get", cwd=self.project_dir)
        self.assertEqual(result.returncode, 0, result.stderr)
        
        deps_dir = os.path.join(self.project_dir, "deps")
        self.assertTrue(os.path.isdir(deps_dir))

    def test_get_creates_lock_file(self):
        """basl get should create basl.lock."""
        result = run_basl("get", "github.com/bluesentinelsec/basl@main", cwd=self.project_dir)
        self.assertEqual(result.returncode, 0, result.stderr)
        
        lock_path = os.path.join(self.project_dir, "basl.lock")
        self.assertTrue(os.path.isfile(lock_path))
        
        with open(lock_path) as f:
            content = f.read()
        self.assertIn("github.com/bluesentinelsec/basl", content)
        self.assertIn("commit", content)

    def test_get_updates_toml(self):
        """basl get should add dep to basl.toml."""
        result = run_basl("get", "github.com/bluesentinelsec/basl@main", cwd=self.project_dir)
        self.assertEqual(result.returncode, 0, result.stderr)
        
        toml_path = os.path.join(self.project_dir, "basl.toml")
        with open(toml_path) as f:
            content = f.read()
        self.assertIn("github.com/bluesentinelsec/basl", content)


if __name__ == "__main__":
    unittest.main()
