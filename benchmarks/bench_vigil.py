#!/usr/bin/env python3
"""Run Vigil benchmark cases and report timing, mirroring bench_python.py output."""
import subprocess
import time
import sys
import os

VIGIL = os.path.join(os.path.dirname(__file__), "../build/vigil")
CASES_DIR = os.path.join(os.path.dirname(__file__), "cases")
ITERATIONS = 5

CASES = [
    "run_vm_arith",
    "run_math_ops",
    "run_parse_ops",
    "run_regex_scan",
    "run_csv_roundtrip",
]

for case in CASES:
    script = os.path.join(CASES_DIR, f"{case}.vigil")
    # warmup
    subprocess.run([VIGIL, "run", script], capture_output=True)
    times = []
    for _ in range(ITERATIONS):
        t0 = time.perf_counter()
        r = subprocess.run([VIGIL, "run", script], capture_output=True)
        elapsed = (time.perf_counter() - t0) * 1000
        if r.returncode != 0:
            print(f"{case}: FAILED (exit {r.returncode})", file=sys.stderr)
            break
        times.append(elapsed)
    else:
        avg = sum(times) / len(times)
        label = case.replace("run_", "").ljust(13)
        print(f"{label}: avg={avg:.2f}ms  min={min(times):.2f}ms  max={max(times):.2f}ms")
