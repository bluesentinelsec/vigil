#!/usr/bin/env python3
"""Integration tests for the BASL debugger using pexpect."""

from __future__ import annotations

import os
import platform
import tempfile
import textwrap
import unittest
from pathlib import Path

import pexpect

if platform.system() == "Windows":
    from pexpect.popen_spawn import PopenSpawn

REPO_ROOT = Path(__file__).resolve().parents[1]
BASL_BIN = str(REPO_ROOT / "basl")

TIMEOUT = 5
IS_WINDOWS = platform.system() == "Windows"


class BaslDebuggerTests(unittest.TestCase):
    """Tests for `basl debug` interactive debugger."""

    @classmethod
    def setUpClass(cls) -> None:
        if not Path(BASL_BIN).exists():
            raise FileNotFoundError(f"missing interpreter binary: {BASL_BIN}")

    def _write_script(self, td: str, source: str, name: str = "main.basl") -> str:
        path = os.path.join(td, name)
        with open(path, "w") as f:
            f.write(textwrap.dedent(source).strip() + "\n")
        return path

    def _spawn(self, script: str, *extra_args: str):
        args = [BASL_BIN, "debug", *extra_args, script]
        if IS_WINDOWS:
            # Windows uses PopenSpawn instead of spawn
            child = PopenSpawn(" ".join(args), timeout=TIMEOUT, encoding="utf-8")
        else:
            child = pexpect.spawn(args[0], args[1:], timeout=TIMEOUT, encoding="utf-8")
        return child

    # ── step from start ──────────────────────────────────────────────

    def test_step_from_start(self):
        """With no breakpoints, debugger steps from the first statement."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                import "fmt";
                fn main() -> i32 {
                    string x = "hello";
                    fmt.println(x);
                    return 0;
                }
            """)
            child = self._spawn(script)
            child.expect("no breakpoints set")
            child.expect(r"\(basl\)")
            # Should be at line 3 (string x = "hello")
            child.sendline("n")
            child.expect(r"\(basl\)")
            child.sendline("c")
            child.expect("program exited normally")
            child.wait()

    # ── breakpoint hit ───────────────────────────────────────────────

    def test_breakpoint_hit(self):
        """Debugger stops at a breakpoint set via -b."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                import "fmt";
                fn main() -> i32 {
                    string x = "hello";
                    fmt.println(x);
                    return 0;
                }
            """)
            child = self._spawn(script, "-b", "4")
            child.expect("breakpoints: line 4")
            child.expect(r"→.*:4")
            child.expect(r"\(basl\)")
            child.sendline("c")
            child.expect("program exited normally")
            child.wait()

    # ── print variable ───────────────────────────────────────────────

    def test_print_variable(self):
        """The p command prints variable values."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                fn main() -> i32 {
                    i32 x = 42;
                    string name = "basl";
                    return 0;
                }
            """)
            child = self._spawn(script, "-b", "4")
            child.expect(r"\(basl\)")
            child.sendline("p x")
            child.expect("x = 42")
            child.sendline("p name")
            child.expect('name = "basl"')
            child.sendline("c")
            child.expect("program exited normally")
            child.wait()

    # ── locals command ───────────────────────────────────────────────

    def test_locals(self):
        """The locals command lists all local variables."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                fn main() -> i32 {
                    i32 a = 1;
                    i32 b = 2;
                    return 0;
                }
            """)
            child = self._spawn(script, "-b", "4")
            child.expect(r"\(basl\)")
            child.sendline("locals")
            child.expect("a = 1")
            child.expect("b = 2")
            child.sendline("c")
            child.wait()

    # ── step into ────────────────────────────────────────────────────

    def test_step_into(self):
        """The s command steps into function calls."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                fn add(i32 a, i32 b) -> i32 {
                    return a + b;
                }
                fn main() -> i32 {
                    i32 result = add(1, 2);
                    return 0;
                }
            """)
            child = self._spawn(script, "-b", "5")
            child.expect(r"\(basl\)")
            # Step into add()
            child.sendline("s")
            # Should be inside add, at the return statement
            child.expect(r"→.*:2")
            child.sendline("p a")
            child.expect("a = 1")
            child.sendline("c")
            child.expect("program exited normally")
            child.wait()

    # ── step over ────────────────────────────────────────────────────

    def test_step_over(self):
        """The n command steps over function calls."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                fn add(i32 a, i32 b) -> i32 {
                    return a + b;
                }
                fn main() -> i32 {
                    i32 result = add(1, 2);
                    return result;
                }
            """)
            child = self._spawn(script, "-b", "5")
            child.expect(r"\(basl\)")
            # Step over add() — should land on next line in main
            child.sendline("n")
            child.expect(r"→.*:6")
            child.sendline("p result")
            child.expect("result = 3")
            child.sendline("c")
            child.wait()

    # ── backtrace ────────────────────────────────────────────────────

    def test_backtrace(self):
        """The bt command shows the call stack."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                fn inner() -> void {
                    i32 x = 1;
                    return;
                }
                fn main() -> i32 {
                    inner();
                    return 0;
                }
            """)
            child = self._spawn(script, "-b", "2")
            child.expect(r"\(basl\)")
            child.sendline("bt")
            child.expect("inner")
            child.expect("main")
            child.sendline("c")
            child.wait()

    # ── set and delete breakpoints interactively ─────────────────────

    def test_interactive_breakpoints(self):
        """Breakpoints can be set and deleted during a debug session."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                fn main() -> i32 {
                    i32 a = 1;
                    i32 b = 2;
                    i32 c = 3;
                    return 0;
                }
            """)
            child = self._spawn(script)
            child.expect(r"\(basl\)")
            # Set breakpoint at line 4
            child.sendline("b 4")
            child.expect("breakpoint set at line 4")
            # List breakpoints
            child.sendline("b")
            child.expect("line 4")
            # Continue to breakpoint
            child.sendline("c")
            child.expect(r"→.*:4")
            # Delete it
            child.sendline("d 4")
            child.expect("breakpoint removed at line 4")
            child.sendline("c")
            child.expect("program exited normally")
            child.wait()

    # ── quit command ─────────────────────────────────────────────────

    def test_quit(self):
        """The q command exits the debugger."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                fn main() -> i32 {
                    i32 x = 1;
                    return 0;
                }
            """)
            child = self._spawn(script)
            child.expect(r"\(basl\)")
            child.sendline("q")
            child.expect("session ended")
            child.wait()

    # ── list command ─────────────────────────────────────────────────

    def test_list_source(self):
        """The l command lists source code around a line."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                fn main() -> i32 {
                    i32 x = 1;
                    return 0;
                }
            """)
            child = self._spawn(script)
            child.expect(r"\(basl\)")
            child.sendline("l 2")
            child.expect("fn main")
            child.sendline("c")
            child.expect("program exited normally")
            child.wait()

    # ── help command ─────────────────────────────────────────────────

    def test_help(self):
        """The h command shows help text."""
        with tempfile.TemporaryDirectory() as td:
            script = self._write_script(td, """
                fn main() -> i32 {
                    return 0;
                }
            """)
            child = self._spawn(script)
            child.expect(r"\(basl\)")
            child.sendline("h")
            child.expect("set breakpoint")
            child.expect("continue")
            child.sendline("c")
            child.wait()


if __name__ == "__main__":
    unittest.main()
