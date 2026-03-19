#!/usr/bin/env python3

import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path


CSV_FIELDS = [
    "nloc",
    "ccn",
    "token",
    "param",
    "length",
    "location",
    "file",
    "function_name",
    "long_name",
    "start_line",
    "end_line",
]
METRICS = ("ccn", "length", "param")
SCAN_DIRS = ("src", "include", "tests")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare lizard metrics for the full codebase against origin/main."
    )
    parser.add_argument("--candidate-root", required=True)
    parser.add_argument("--baseline-root", required=True)
    parser.add_argument("--thresholds", required=True)
    parser.add_argument("--summary", required=True)
    parser.add_argument("--candidate-report")
    parser.add_argument("--baseline-report")
    return parser.parse_args()


def load_thresholds(path: Path) -> dict[str, int]:
    data = json.loads(path.read_text())
    defaults = data.get("defaults", {})
    return {metric: int(defaults[metric]) for metric in METRICS}


def run_lizard(root: Path) -> list[dict[str, str]]:
    cmd = [
        sys.executable,
        "-m",
        "lizard",
        *SCAN_DIRS,
        "-l",
        "c",
        "--csv",
    ]
    result = subprocess.run(
        cmd,
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    reader = csv.DictReader(result.stdout.splitlines(), fieldnames=CSV_FIELDS)
    rows = []
    for row in reader:
        if not row.get("file"):
            continue
        rows.append(row)
    return rows


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        for row in rows:
            writer.writerow(row)


def row_key(row: dict[str, str]) -> tuple[str, str]:
    return row["file"], row["function_name"]


def metric_map(row: dict[str, str]) -> dict[str, int]:
    return {metric: int(row[metric]) for metric in METRICS}


def summarize_rows(
    candidate_rows: list[dict[str, str]],
    baseline_rows: list[dict[str, str]],
    thresholds: dict[str, int],
) -> dict:
    baseline_map = {row_key(row): row for row in baseline_rows}
    inherited = []
    regressions = []

    for row in candidate_rows:
        actual = metric_map(row)
        exceeded = {metric: value for metric, value in actual.items() if value > thresholds[metric]}
        if not exceeded:
            continue

        baseline_row = baseline_map.get(row_key(row))
        inherited_metrics = {}
        regression_metrics = {}

        for metric, value in exceeded.items():
            baseline_value = None
            if baseline_row is not None:
                baseline_value = int(baseline_row[metric])

            if baseline_value is not None and baseline_value > thresholds[metric] and value <= baseline_value:
                inherited_metrics[metric] = {
                    "candidate": value,
                    "baseline": baseline_value,
                    "threshold": thresholds[metric],
                }
            else:
                regression_metrics[metric] = {
                    "candidate": value,
                    "baseline": baseline_value,
                    "threshold": thresholds[metric],
                }

        if inherited_metrics:
            inherited.append(
                {
                    "file": row["file"],
                    "function": row["function_name"],
                    "start_line": int(row["start_line"]),
                    "metrics": inherited_metrics,
                }
            )
        if regression_metrics:
            regressions.append(
                {
                    "file": row["file"],
                    "function": row["function_name"],
                    "start_line": int(row["start_line"]),
                    "metrics": regression_metrics,
                }
            )

    inherited.sort(key=lambda item: (item["file"], item["function"]))
    regressions.sort(key=lambda item: (item["file"], item["function"]))

    baseline_debt = 0
    for row in baseline_rows:
        actual = metric_map(row)
        if any(actual[metric] > thresholds[metric] for metric in METRICS):
            baseline_debt += 1

    return {
        "thresholds": thresholds,
        "candidate_function_count": len(candidate_rows),
        "baseline_function_count": len(baseline_rows),
        "baseline_debt_count": baseline_debt,
        "candidate_inherited_debt_count": len(inherited),
        "regression_count": len(regressions),
        "inherited_debt": inherited,
        "regressions": regressions,
    }


def print_summary(summary: dict) -> None:
    print("## Complexity Report")
    print()
    print(
        f"Functions analyzed: candidate={summary['candidate_function_count']}, "
        f"baseline={summary['baseline_function_count']}"
    )
    print(
        f"Inherited debt: {summary['candidate_inherited_debt_count']} functions above thresholds"
    )
    print(f"Regressions: {summary['regression_count']}")
    print()

    if summary["regressions"]:
        print("New or worsened threshold violations:")
        for item in summary["regressions"]:
            metrics = ", ".join(
                f"{metric}={values['candidate']} (baseline={values['baseline']}, limit={values['threshold']})"
                for metric, values in item["metrics"].items()
            )
            print(f"- {item['file']}:{item['start_line']} {item['function']}: {metrics}")
        print()

    if summary["candidate_inherited_debt_count"]:
        print("Inherited above-threshold functions:")
        for item in summary["inherited_debt"][:20]:
            metrics = ", ".join(
                f"{metric}={values['candidate']} (baseline={values['baseline']}, limit={values['threshold']})"
                for metric, values in item["metrics"].items()
            )
            print(f"- {item['file']}:{item['start_line']} {item['function']}: {metrics}")
        if summary["candidate_inherited_debt_count"] > 20:
            remaining = summary["candidate_inherited_debt_count"] - 20
            print(f"- ... {remaining} more inherited functions")


def main() -> int:
    args = parse_args()
    candidate_root = Path(args.candidate_root).resolve()
    baseline_root = Path(args.baseline_root).resolve()
    summary_path = Path(args.summary)
    summary_path.parent.mkdir(parents=True, exist_ok=True)

    thresholds = load_thresholds(Path(args.thresholds))
    candidate_rows = run_lizard(candidate_root)
    baseline_rows = run_lizard(baseline_root)

    if args.candidate_report:
        path = Path(args.candidate_report)
        path.parent.mkdir(parents=True, exist_ok=True)
        write_csv(path, candidate_rows)
    if args.baseline_report:
        path = Path(args.baseline_report)
        path.parent.mkdir(parents=True, exist_ok=True)
        write_csv(path, baseline_rows)

    summary = summarize_rows(candidate_rows, baseline_rows, thresholds)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print_summary(summary)
    return 1 if summary["regression_count"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
