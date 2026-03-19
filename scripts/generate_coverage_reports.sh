#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <build-dir> <output-dir>" >&2
    exit 1
fi

build_dir="$1"
output_dir="$2"
repo_root="$(pwd)"

mkdir -p "$output_dir"

common_args="
  --root $repo_root
  --object-directory $build_dir
  --filter $repo_root/src/
"

python3 -m gcovr $common_args \
    --json-pretty \
    --json "$output_dir/coverage.json"

python3 -m gcovr $common_args \
    --json-summary-pretty \
    --json-summary "$output_dir/coverage-summary.json" \
    --print-summary

python3 -m gcovr $common_args \
    --cobertura-pretty \
    --cobertura "$output_dir/coverage.xml"

python3 -m gcovr $common_args \
    --html-details "$output_dir/coverage.html"
