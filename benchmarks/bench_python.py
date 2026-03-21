"""Python equivalents of the Vigil benchmark cases."""
import math
import re
import time
import csv
import io

ITERATIONS = 5

def bench(name, fn):
    # warmup
    fn()
    times = []
    for _ in range(ITERATIONS):
        t0 = time.perf_counter()
        fn()
        times.append((time.perf_counter() - t0) * 1000)
    avg = sum(times) / len(times)
    print(f"{name}: avg={avg:.2f}ms  min={min(times):.2f}ms  max={max(times):.2f}ms")

def vm_arith():
    total = 0
    for i in range(5000):
        row = 0
        for j in range(200):
            row += (i * 7 + j * 13) % 97
        total += row
    assert total > 0

def math_ops():
    acc = 0.0
    for i in range(60000):
        x = i * 0.001 + 1.0
        acc += math.sin(x) + math.cos(x) + math.sqrt(x) + math.log(x) + x ** 0.25
    assert acc != 0.0

def parse_ops():
    total = 0
    for _ in range(6000):
        whole = int("12345")
        ratio = float("98.125")
        flag = "true" == "true"
        assert flag
        total += whole + int(ratio)
    assert total > 0

def regex_scan():
    text = "a1 bb22 ccc333 dddd4444 eeeee55555 ffffff666666 ggggggg7777777"
    pat_find = re.compile(r"[a-z]+[0-9]+")
    pat_replace = re.compile(r"[0-9]+")
    total = 0
    for _ in range(4000):
        matches = pat_find.findall(text)
        assert len(matches) == 7
        total += len(matches[0])
        total += len(pat_replace.sub("XX", text))
    assert total > 0

def csv_roundtrip():
    line = '"alpha,one",42,"say ""hi""",delta,999'
    total = 0
    for _ in range(20000):
        row = next(csv.reader([line]))
        assert len(row) == 5
        buf = io.StringIO()
        csv.writer(buf).writerow(row)
        out = buf.getvalue().rstrip("\r\n")
        total += len(out) + len(row[0]) + len(row[2])
    assert total > 0

bench("vm_arith    ", vm_arith)
bench("math_ops    ", math_ops)
bench("parse_ops   ", parse_ops)
bench("regex_scan  ", regex_scan)
bench("csv_roundtrip", csv_roundtrip)
