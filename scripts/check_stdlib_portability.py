#!/usr/bin/env python3
"""Verify stdlib portability layering rules.

Run from the repository root:
    python3 scripts/check_stdlib_portability.py

This check enforces two rules:
1. `src/stdlib/` must not include platform headers directly.
2. Portability-sensitive stdlib modules that were migrated to runtime/platform
   allocation paths must not regress to raw malloc-family allocation.
"""

from pathlib import Path
import re
import sys


FORBIDDEN_INCLUDE_PATTERNS = [
    (r"#\s*include\s*<unistd\.h>", "POSIX unistd.h"),
    (r"#\s*include\s*<sys/", "POSIX sys/ header"),
    (r"#\s*include\s*<dirent\.h>", "POSIX dirent.h"),
    (r"#\s*include\s*<dlfcn\.h>", "POSIX dlfcn.h"),
    (r"#\s*include\s*<pthread\.h>", "POSIX pthread.h"),
    (r"#\s*include\s*<windows\.h>", "Win32 windows.h"),
    (r"#\s*include\s*<io\.h>", "MSVC io.h"),
    (r"#\s*include\s*<direct\.h>", "MSVC direct.h"),
    (r"#\s*include\s*<winsock2\.h>", "WinSock2"),
    (r"#\s*include\s*<ws2tcpip\.h>", "WinSock TCP/IP"),
    (r"#\s*include\s*<netinet/in\.h>", "POSIX netinet/in.h"),
    (r"#\s*include\s*<arpa/inet\.h>", "POSIX arpa/inet.h"),
    (r"#\s*include\s*<netdb\.h>", "POSIX netdb.h"),
]

RAW_ALLOC_RE = re.compile(r"\b(?:malloc|calloc|realloc)\s*\(")
FILES_WITHOUT_RAW_ALLOC = {"fs.c", "net.c", "time.c"}


def check_stdlib_includes(path: Path) -> list[str]:
    errors: list[str] = []
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        for pattern, desc in FORBIDDEN_INCLUDE_PATTERNS:
            if re.search(pattern, line):
                errors.append(f"{path}:{lineno}: forbidden {desc}: {line.strip()}")
    return errors


def check_raw_alloc(path: Path) -> list[str]:
    errors: list[str] = []
    if path.name not in FILES_WITHOUT_RAW_ALLOC:
        return errors
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        if RAW_ALLOC_RE.search(line):
            errors.append(f"{path}:{lineno}: raw heap allocation is forbidden here: {line.strip()}")
    return errors


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    stdlib_dir = root / "src" / "stdlib"

    errors: list[str] = []
    files = sorted(stdlib_dir.rglob("*.c"))
    for path in files:
        errors.extend(check_stdlib_includes(path))
        errors.extend(check_raw_alloc(path))

    if errors:
        print(f"FAIL: {len(errors)} stdlib portability violation(s):\n")
        for error in errors:
            print(f"  {error}")
        return 1

    print(f"OK: {len(files)} stdlib source files checked, no portability violations.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
