#!/usr/bin/env python3
"""Integration test for 'basl debug' — DAP protocol over stdio.

Spawns 'basl debug <file>' and exchanges DAP messages via
Content-Length framed JSON on stdin/stdout.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def resolve_basl_command() -> list[str]:
    configured_bin = os.environ.get("BASL_BIN")
    native_bin = REPO_ROOT / "build" / ("basl.exe" if os.name == "nt" else "basl")
    wasm_bin = REPO_ROOT / "build" / "basl.js"

    if configured_bin:
        p = Path(configured_bin)
        if p.suffix == ".js":
            return ["node", str(p)]
        return [str(p)]
    if native_bin.exists():
        return [str(native_bin)]
    if wasm_bin.exists():
        return ["node", str(wasm_bin)]
    raise FileNotFoundError("Cannot find basl binary")


class DAPClient:
    """Minimal DAP client that talks Content-Length framed JSON over pipes."""

    def __init__(self, proc: subprocess.Popen):
        self.proc = proc
        self.seq = 1
        self.pending_events: list[dict] = []

    def send(self, command: str, arguments: dict | None = None) -> int:
        """Send a DAP request. Returns the seq number."""
        msg: dict = {
            "seq": self.seq,
            "type": "request",
            "command": command,
        }
        if arguments:
            msg["arguments"] = arguments
        body = json.dumps(msg).encode("utf-8")
        header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
        self.proc.stdin.write(header + body)
        self.proc.stdin.flush()
        seq = self.seq
        self.seq += 1
        return seq

    def recv(self) -> dict:
        """Read one DAP message (response or event)."""
        content_length = 0
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise EOFError("DAP server closed stdout")
            line = line.decode("utf-8", errors="replace").strip()
            if line == "":
                break
            if line.lower().startswith("content-length:"):
                content_length = int(line.split(":", 1)[1].strip())
        if content_length == 0:
            raise ValueError("Missing Content-Length header")
        body = self.proc.stdout.read(content_length)
        return json.loads(body)

    def recv_response(self, command: str) -> dict:
        """Read messages until we get a response for the given command."""
        while True:
            msg = self.recv()
            if msg.get("type") == "response" and msg.get("command") == command:
                return msg
            if msg.get("type") == "event":
                self.pending_events.append(msg)

    def recv_event(self, event_name: str) -> dict:
        """Read messages until we get the named event."""
        # Check pending events first.
        for i, evt in enumerate(self.pending_events):
            if evt.get("event") == event_name:
                return self.pending_events.pop(i)
        while True:
            msg = self.recv()
            if msg.get("type") == "event" and msg.get("event") == event_name:
                return msg
            if msg.get("type") == "event":
                self.pending_events.append(msg)


class TestBaslDebug(unittest.TestCase):
    """Test the 'basl debug' DAP server."""

    def _start_debug(self, script_content: str) -> tuple[DAPClient, Path]:
        """Write a script to a temp file and start basl debug."""
        self.tmpdir = tempfile.mkdtemp(prefix="basl_debug_")
        script_path = Path(self.tmpdir) / "test.basl"
        script_path.write_text(script_content)

        cmd = [*resolve_basl_command(), "debug", str(script_path)]
        proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.proc = proc
        return DAPClient(proc), script_path

    def tearDown(self):
        if hasattr(self, "proc") and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        if hasattr(self, "tmpdir"):
            import shutil
            shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_initialize_and_disconnect(self):
        """Basic DAP handshake: initialize → initialized event → disconnect."""
        client, _ = self._start_debug(
            'import "fmt";\nfn main() -> i32 {\n    return 0;\n}\n'
        )

        # Initialize.
        client.send("initialize", {
            "clientID": "test",
            "adapterID": "basl",
        })
        resp = client.recv_response("initialize")
        self.assertTrue(resp.get("success"), f"initialize failed: {resp}")
        self.assertIn("body", resp)

        # Wait for initialized event.
        evt = client.recv_event("initialized")
        self.assertEqual(evt["event"], "initialized")

        # Disconnect.
        client.send("disconnect")
        resp = client.recv_response("disconnect")
        self.assertTrue(resp.get("success"), f"disconnect failed: {resp}")

        self.proc.wait(timeout=5)
        self.assertEqual(self.proc.returncode, 0)

    def test_launch_and_terminate(self):
        """Launch a program, let it run to completion."""
        client, script_path = self._start_debug(
            'import "fmt";\nfn main() -> i32 {\n    fmt.println("debug test");\n    return 0;\n}\n'
        )

        # Initialize.
        client.send("initialize", {"clientID": "test", "adapterID": "basl"})
        client.recv_response("initialize")
        client.recv_event("initialized")

        # Launch.
        client.send("launch", {"program": str(script_path)})
        resp = client.recv_response("launch")
        self.assertTrue(resp.get("success"), f"launch failed: {resp}")

        # Program should run and terminate.
        evt = client.recv_event("terminated")
        self.assertEqual(evt["event"], "terminated")

        # Disconnect.
        client.send("disconnect")
        client.recv_response("disconnect")
        self.proc.wait(timeout=5)

    def test_debug_missing_file(self):
        """basl debug with nonexistent file should exit with error."""
        cmd = [*resolve_basl_command(), "debug", "/nonexistent/file.basl"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)

    def test_help_shows_debug(self):
        """basl --help should list the debug command."""
        cmd = [*resolve_basl_command()]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        output = result.stdout + result.stderr
        self.assertIn("debug", output)


if __name__ == "__main__":
    unittest.main()
