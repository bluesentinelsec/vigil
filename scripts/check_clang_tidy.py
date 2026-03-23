#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path


DIAG_RE = re.compile(
    r"^(?P<path>.+?):(?P<line>\d+):(?P<column>\d+): "
    r"(?P<severity>warning|error): (?P<message>.+?) \[(?P<check>[^\]]+)\]$"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare clang-tidy diagnostics for the full codebase against origin/main."
    )
    parser.add_argument("--baseline-log", required=True)
    parser.add_argument("--candidate-log", required=True)
    parser.add_argument("--summary", required=True)
    return parser.parse_args()


def normalize_path(raw_path: str) -> str:
    for marker in ("src/", "include/", "tests/"):
        index = raw_path.find(marker)
        if index != -1:
            return raw_path[index:]
    return raw_path


COGNITIVE_RE = re.compile(
    r"function '(?P<func>[^']+)' has cognitive complexity of \d+"
)


def stable_message(message: str) -> str:
    """Normalize cognitive-complexity messages so that a value change is not a new diagnostic."""
    m = COGNITIVE_RE.search(message)
    if m:
        return f"function '{m.group('func')}' has cognitive complexity above threshold"
    return message


def load_diagnostics(path: Path) -> dict[str, dict]:
    diagnostics = {}
    for line in path.read_text().splitlines():
        match = DIAG_RE.match(line.strip())
        if match is None:
            continue

        record = match.groupdict()
        normalized = {
            "path": normalize_path(record["path"]),
            "line": int(record["line"]),
            "column": int(record["column"]),
            "severity": record["severity"],
            "message": record["message"],
            "check": record["check"],
        }
        key = "|".join(
            (
                normalized["path"],
                normalized["severity"],
                normalized["check"],
                stable_message(normalized["message"]),
            )
        )
        diagnostics[key] = normalized
    return diagnostics


def print_summary(summary: dict) -> None:
    print("## Clang-Tidy Readability Report")
    print()
    print(f"Baseline diagnostics: {summary['baseline_count']}")
    print(f"Candidate diagnostics: {summary['candidate_count']}")
    print(f"New diagnostics: {summary['new_count']}")
    print(f"Resolved diagnostics: {summary['resolved_count']}")
    print()

    if summary["new_diagnostics"]:
        print("New diagnostics:")
        for item in summary["new_diagnostics"][:50]:
            print(
                f"- {item['path']}:{item['line']}:{item['column']}: "
                f"{item['message']} [{item['check']}]"
            )
        if summary["new_count"] > 50:
            print(f"- ... {summary['new_count'] - 50} more diagnostics")
        print()

    if summary["resolved_diagnostics"]:
        print("Resolved diagnostics:")
        for item in summary["resolved_diagnostics"][:20]:
            print(
                f"- {item['path']}:{item['line']}:{item['column']}: "
                f"{item['message']} [{item['check']}]"
            )
        if summary["resolved_count"] > 20:
            print(f"- ... {summary['resolved_count'] - 20} more resolved diagnostics")


def main() -> int:
    args = parse_args()
    baseline = load_diagnostics(Path(args.baseline_log))
    candidate = load_diagnostics(Path(args.candidate_log))

    new_keys = sorted(set(candidate) - set(baseline))
    resolved_keys = sorted(set(baseline) - set(candidate))

    summary = {
        "baseline_count": len(baseline),
        "candidate_count": len(candidate),
        "new_count": len(new_keys),
        "resolved_count": len(resolved_keys),
        "new_diagnostics": [candidate[key] for key in new_keys],
        "resolved_diagnostics": [baseline[key] for key in resolved_keys],
    }

    summary_path = Path(args.summary)
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print_summary(summary)
    return 1 if new_keys else 0


if __name__ == "__main__":
    raise SystemExit(main())
