#!/usr/bin/env python3
"""Verify that basl_core sources only include C11-standard headers.

Run from the repository root:
    python3 scripts/check_core_portability.py

Exit 0 if clean, 1 if any forbidden include is found.
"""

import re
import sys
from pathlib import Path

# C11 standard headers (plus common freestanding ones).
ALLOWED_HEADERS = {
    "assert.h", "complex.h", "ctype.h", "errno.h", "fenv.h", "float.h",
    "inttypes.h", "iso646.h", "limits.h", "locale.h", "math.h",
    "setjmp.h", "signal.h", "stdalign.h", "stdarg.h", "stdatomic.h",
    "stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h",
    "stdnoreturn.h", "string.h", "tgmath.h", "threads.h", "time.h",
    "uchar.h", "wchar.h", "wctype.h",
}

# Patterns that indicate platform-specific code.
FORBIDDEN_PATTERNS = [
    (r"#\s*include\s*<unistd\.h>", "POSIX unistd.h"),
    (r"#\s*include\s*<sys/", "POSIX sys/ header"),
    (r"#\s*include\s*<dirent\.h>", "POSIX dirent.h"),
    (r"#\s*include\s*<dlfcn\.h>", "POSIX dlfcn.h"),
    (r"#\s*include\s*<pthread\.h>", "POSIX pthread.h"),
    (r"#\s*include\s*<windows\.h>", "Win32 windows.h"),
    (r"#\s*include\s*<io\.h>", "MSVC io.h"),
    (r"#\s*include\s*<direct\.h>", "MSVC direct.h"),
]

INCLUDE_RE = re.compile(r'#\s*include\s*<([^>]+)>')

def check_file(path: Path) -> list[str]:
    errors = []
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        # Check forbidden patterns.
        for pattern, desc in FORBIDDEN_PATTERNS:
            if re.search(pattern, line):
                errors.append(f"{path}:{lineno}: forbidden {desc}: {line.strip()}")

        # Check any angle-bracket include is in the allowed set.
        m = INCLUDE_RE.search(line)
        if m:
            header = m.group(1)
            if header not in ALLOWED_HEADERS:
                errors.append(
                    f"{path}:{lineno}: non-C11 header <{header}>: {line.strip()}"
                )
    return errors


def main() -> int:
    root = Path(__file__).resolve().parent.parent

    # All source files that belong to basl_core.
    core_dirs = [root / "src", root / "include" / "basl"]
    core_globs = ["*.c", "*.h"]
    # Exclude the CLI — it's allowed to have platform code.
    exclude = {root / "src" / "cli", root / "src" / "stdlib"}

    errors: list[str] = []
    for d in core_dirs:
        for g in core_globs:
            for path in sorted(d.rglob(g)):
                if any(path.is_relative_to(e) for e in exclude):
                    continue
                errors.extend(check_file(path))

    if errors:
        print(f"FAIL: {len(errors)} portability violation(s) in basl_core:\n")
        for e in errors:
            print(f"  {e}")
        return 1

    file_count = sum(
        1
        for d in core_dirs
        for g in core_globs
        for p in d.rglob(g)
        if not any(p.is_relative_to(e) for e in exclude)
    )
    print(f"OK: {file_count} core files checked, no portability violations.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
