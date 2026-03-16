#!/usr/bin/env python3
"""Integration tests for the BASL REPL with readline functionality.

Uses pexpect (or wexpect on Windows) to test interactive features.
"""

import os
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

# Try pexpect (POSIX) or wexpect (Windows)
pexpect = None
if os.name == "nt":
    try:
        import wexpect as pexpect
    except ImportError:
        pass
else:
    try:
        import pexpect
    except ImportError:
        pass

HAS_EXPECT = pexpect is not None


def get_basl_bin() -> str:
    """Get path to the BASL binary."""
    configured = os.environ.get("BASL_BIN")
    if configured:
        return configured
    if os.name == "nt":
        native = REPO_ROOT / "build" / "Release" / "basl.exe"
    else:
        native = REPO_ROOT / "build" / "basl"
    if native.exists():
        return str(native)
    raise FileNotFoundError("BASL binary not found")


@unittest.skipUnless(HAS_EXPECT, "pexpect/wexpect not available")
class TestReplInteractive(unittest.TestCase):
    """Test REPL interactive features using pexpect."""

    def setUp(self):
        self.basl = get_basl_bin()

    def spawn_repl(self, timeout=10):
        """Spawn a REPL process."""
        child = pexpect.spawn(self.basl, ["repl"], timeout=timeout, encoding="utf-8")
        child.expect(">>>")  # Wait for first prompt
        return child

    def send_line(self, child, text):
        """Send a line with carriage return (raw mode expects CR, not LF)."""
        child.send(text + "\r")

    def test_basic_expression(self):
        """Test basic expression evaluation."""
        child = self.spawn_repl()
        self.send_line(child, "1 + 1")
        child.expect("2")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_multiline_function(self):
        """Test multi-line input with brackets."""
        child = self.spawn_repl()
        child.send("fn add(i32 a, i32 b) -> i32 {\r")
        child.expect(r"\r\n\r\.\.\. ")  # Continuation prompt after newline
        child.send("return a + b;\r")
        child.expect(r"\r\n\r\.\.\. ")
        child.send("}\r")
        child.expect(r"\r\n\r>>> ")  # Back to main prompt
        child.send("add(3, 4)\r")
        child.expect(r"\r\n\r>>> ")
        child.send(":quit\r")
        child.expect(pexpect.EOF)

    def test_history_up_arrow(self):
        """Test up arrow recalls previous command."""
        child = self.spawn_repl()
        self.send_line(child, "42")
        child.expect("42")
        child.expect(">>>")
        self.send_line(child, "100")
        child.expect("100")
        child.expect(">>>")
        # Press up arrow twice to get back to "42"
        child.send("\x1b[A")  # Up arrow
        child.send("\x1b[A")  # Up arrow again
        self.send_line(child, "")    # Enter to execute
        child.expect("42")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_history_down_arrow(self):
        """Test down arrow navigates forward in history."""
        child = self.spawn_repl()
        self.send_line(child, "1")
        child.expect(r"\r\n1\r\n")
        child.expect(">>>")
        self.send_line(child, "2")
        child.expect(r"\r\n2\r\n")
        child.expect(">>>")
        self.send_line(child, "3")
        child.expect(r"\r\n3\r\n")
        child.expect(">>>")
        # Go up three times to get to "1", then down once to get to "2"
        child.send("\x1b[A")  # Up -> 3
        child.send("\x1b[A")  # Up -> 2
        child.send("\x1b[A")  # Up -> 1
        child.send("\x1b[B")  # Down -> 2
        self.send_line(child, "")
        child.expect(r"\r\n2\r\n")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_a_home(self):
        """Test Ctrl-A moves to start of line."""
        child = self.spawn_repl()
        child.send("23")
        child.send("\x01")  # Ctrl-A
        child.send("1")     # Insert at beginning
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_e_end(self):
        """Test Ctrl-E moves to end of line."""
        child = self.spawn_repl()
        child.send("12")
        child.send("\x01")  # Ctrl-A (go to start)
        child.send("\x05")  # Ctrl-E (go to end)
        child.send("3")     # Append at end
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_k_kill_to_end(self):
        """Test Ctrl-K kills to end of line."""
        child = self.spawn_repl()
        child.send("12345")
        child.send("\x01")  # Ctrl-A
        child.send("\x06")  # Ctrl-F (forward one char)
        child.send("\x06")  # Ctrl-F
        child.send("\x0b")  # Ctrl-K (kill to end)
        self.send_line(child, "")
        child.expect(r"\r\n12\r\n")  # Result on its own line
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_u_kill_to_start(self):
        """Test Ctrl-U kills to start of line."""
        child = self.spawn_repl()
        child.send("12345")
        child.send("\x01")  # Ctrl-A
        child.send("\x06")  # Ctrl-F
        child.send("\x06")  # Ctrl-F
        child.send("\x15")  # Ctrl-U (kill to start)
        self.send_line(child, "")
        child.expect(r"\r\n345\r\n")  # Result on its own line
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_backspace(self):
        """Test backspace deletes character before cursor."""
        child = self.spawn_repl()
        child.send("124")
        child.send("\x7f")  # Backspace
        child.send("3")
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_left_right_arrows(self):
        """Test left/right arrow cursor movement."""
        child = self.spawn_repl()
        child.send("13")
        child.send("\x1b[D")  # Left arrow
        child.send("2")       # Insert in middle
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_special_commands(self):
        """Test :help and :clear commands."""
        child = self.spawn_repl()
        self.send_line(child, ":help")
        child.expect(":quit")  # Help text mentions :quit
        child.expect(">>>")
        self.send_line(child, "i32 x = 42")
        child.expect(">>>")
        self.send_line(child, ":clear")
        child.expect("State cleared")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_exit_command(self):
        """Test exit() quits the REPL."""
        child = self.spawn_repl()
        self.send_line(child, "exit()")
        child.expect(pexpect.EOF)

    def test_redefinition(self):
        """Test function redefinition works in REPL."""
        child = self.spawn_repl()
        # Define function
        child.send("fn foo() -> i32 { return 42; }\r")
        child.expect(r"\r\n\r>>> ")  # Newline after command, then prompt
        # Call function
        child.send("foo()\r")
        child.expect(r"\r\n\r>>> ")  # Result + newline + prompt
        # Redefine function
        child.send("fn foo() -> i32 { return 99; }\r")
        child.expect(r"\r\n\r>>> ")
        # Call again
        child.send("foo()\r")
        child.expect(r"\r\n\r>>> ")
        # Quit
        child.send(":quit\r")
        child.expect(pexpect.EOF)

    def test_ans_variable(self):
        """Test __ans holds last expression result."""
        child = self.spawn_repl()
        self.send_line(child, "42")
        child.expect("42")
        child.expect(">>>")
        self.send_line(child, "__ans")
        child.expect("42")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_error_recovery(self):
        """Test REPL recovers from errors."""
        child = self.spawn_repl()
        self.send_line(child, "undefined_var")
        child.expect("error")
        child.expect(">>>")
        self.send_line(child, "1 + 1")
        child.expect("2")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)


if __name__ == "__main__":
    unittest.main()
