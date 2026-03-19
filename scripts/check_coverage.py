#!/usr/bin/env python3
"""Check global and changed-line coverage thresholds from gcovr output."""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import subprocess
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", required=True, help="Path to gcovr JSON summary output")
    parser.add_argument("--details", required=True, help="Path to gcovr JSON detailed output")
    parser.add_argument("--thresholds", required=True, help="Path to coverage threshold config")
    parser.add_argument("--base-ref", default="origin/main", help="Git base ref for changed-line diff")
    return parser.parse_args()


def load_json(path: str) -> dict[str, Any]:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def git_lines(cmd: list[str]) -> list[str]:
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return result.stdout.splitlines()


def changed_source_files(base_ref: str) -> list[str]:
    lines = git_lines([
        "git",
        "diff",
        "--name-only",
        "--diff-filter=ACMR",
        f"{base_ref}...HEAD",
        "--",
        "src",
    ])
    return [line for line in lines if line.endswith(".c")]


def changed_lines_for_file(base_ref: str, path: str) -> set[int]:
    lines = git_lines([
        "git",
        "diff",
        "--unified=0",
        "--no-color",
        f"{base_ref}...HEAD",
        "--",
        path,
    ])
    changed: set[int] = set()
    for line in lines:
        if not line.startswith("@@"):
            continue
        parts = line.split(" ")
        new_range = next((part for part in parts if part.startswith("+")), None)
        if new_range is None:
            continue
        start_count = new_range[1:]
        if "," in start_count:
            start_text, count_text = start_count.split(",", 1)
            start = int(start_text)
            count = int(count_text)
        else:
            start = int(start_count)
            count = 1
        for line_number in range(start, start + count):
            changed.add(line_number)
    return changed


def detailed_lines_by_file(details: dict[str, Any]) -> dict[str, dict[int, dict[str, Any]]]:
    result: dict[str, dict[int, dict[str, Any]]] = {}
    for entry in details.get("files", []):
        file_map: dict[int, dict[str, Any]] = {}
        for line in entry.get("lines", []):
            file_map[int(line["line_number"])] = line
        result[entry["file"]] = file_map
    return result


def summary_by_file(summary: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {entry["filename"]: entry for entry in summary.get("files", [])}


def line_threshold_for_file(thresholds: dict[str, Any], path: str) -> float:
    threshold = float(thresholds["changed_lines"]["line_percent"])
    for rule in thresholds.get("rules", []):
        if fnmatch.fnmatch(path, rule["pattern"]):
            threshold = max(threshold, float(rule["line_percent"]))
    return threshold


def write_summary(text: str) -> None:
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(text)
        if not text.endswith("\n"):
            handle.write("\n")


def main() -> int:
    args = parse_args()
    summary = load_json(args.summary)
    details = load_json(args.details)
    thresholds = load_json(args.thresholds)

    overall_line_percent = float(summary["line_percent"])
    overall_threshold = float(thresholds["global"]["line_percent"])

    details_map = detailed_lines_by_file(details)
    summary_map = summary_by_file(summary)

    report_lines = [
        "## Coverage Report",
        "",
        f"- global line coverage: `{overall_line_percent:.1f}%`",
        f"- required global line coverage: `{overall_threshold:.1f}%`",
        "",
    ]

    failures: list[str] = []
    if overall_line_percent < overall_threshold:
        failures.append(
            f"global line coverage {overall_line_percent:.1f}% is below {overall_threshold:.1f}%"
        )

    changed_files = changed_source_files(args.base_ref)
    if changed_files:
        report_lines.extend([
            "| changed file | covered lines | executable changed lines | percent | required | result |",
            "| --- | --- | --- | --- | --- | --- |",
        ])
    else:
        report_lines.append("No changed `src/*.c` files detected.")

    skipped_files: list[str] = []
    for path in changed_files:
        threshold = line_threshold_for_file(thresholds, path)
        file_lines = details_map.get(path)
        if not file_lines:
            skipped_files.append(path)
            continue

        changed_lines = changed_lines_for_file(args.base_ref, path)
        executable = []
        covered = 0
        for line_number in sorted(changed_lines):
            line = file_lines.get(line_number)
            if line is None or line.get("gcovr/noncode"):
                continue
            executable.append(line_number)
            if int(line.get("count", 0)) > 0:
                covered += 1

        if not executable:
            report_lines.append(
                f"| `{path}` | n/a | 0 | n/a | {threshold:.1f}% | SKIP |"
            )
            continue

        percent = (covered / len(executable)) * 100.0
        result = "PASS"
        if percent < threshold:
            failures.append(
                f"{path} changed-line coverage {percent:.1f}% is below {threshold:.1f}% "
                f"({covered}/{len(executable)} executable changed lines covered)"
            )
            result = "FAIL"

        report_lines.append(
            f"| `{path}` | {covered} | {len(executable)} | {percent:.1f}% | {threshold:.1f}% | {result} |"
        )

    if skipped_files:
        report_lines.extend([
            "",
            "### Files Without Coverage Data",
            "",
        ])
        for path in skipped_files:
            summary_entry = summary_map.get(path)
            if summary_entry is not None:
                detail = f"summary reported {summary_entry['line_percent']:.1f}% but no line map was present"
            else:
                detail = "not instrumented on this platform or not part of the filtered coverage set"
            report_lines.append(f"- `{path}`: {detail}")

    if failures:
        report_lines.extend([
            "",
            "### Failures",
            "",
        ])
        for failure in failures:
            report_lines.append(f"- {failure}")
    else:
        report_lines.extend([
            "",
            "Coverage thresholds satisfied.",
        ])

    markdown = "\n".join(report_lines) + "\n"
    print(markdown, end="")
    write_summary(markdown)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
