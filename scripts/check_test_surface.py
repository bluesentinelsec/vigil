#!/usr/bin/env python3
"""Ensure key stdlib modules have an assigned test surface."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        default="coverage/test_surface.json",
        help="Path to test surface manifest",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path.cwd()
    manifest_path = repo_root / args.manifest
    with manifest_path.open("r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    failures: list[str] = []
    for module_name, test_paths in sorted(manifest["stdlib_modules"].items()):
        module_path = repo_root / "src" / "stdlib" / f"{module_name}.c"
        if not module_path.exists():
            failures.append(f"missing stdlib module source: {module_path.relative_to(repo_root)}")
            continue
        if not test_paths:
            failures.append(f"{module_name} has no assigned tests in {args.manifest}")
            continue
        for test_path in test_paths:
            path = repo_root / test_path
            if not path.exists():
                failures.append(f"{module_name} references missing test file: {test_path}")

    if failures:
        print("test surface audit failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("test surface audit passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
