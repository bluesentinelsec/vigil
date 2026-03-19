#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <source-root> <build-dir> <log-file>" >&2
    exit 1
fi

source_root="$1"
build_dir="$2"
log_file="$3"
clang_tidy_bin="${CLANG_TIDY_BIN:-clang-tidy}"
config_file="${CLANG_TIDY_CONFIG_FILE:-}"
file_list="$(mktemp)"

if [ ! -f "$build_dir/compile_commands.json" ]; then
    echo "missing compile_commands.json in $build_dir" >&2
    exit 1
fi

trap 'rm -f "$file_list"' EXIT INT TERM HUP
mkdir -p "$(dirname "$log_file")"
: > "$log_file"

source_root_abs="$(cd "$source_root" && pwd)"
build_dir_abs="$(cd "$build_dir" && pwd)"
header_filter="^${source_root_abs}/(src|include|tests)/"
config_args=""

if [ -n "$config_file" ]; then
    config_file_abs="$(cd "$(dirname "$config_file")" && pwd)/$(basename "$config_file")"
    config_args="--config-file=$config_file_abs"
elif [ -f "$source_root_abs/.clang-tidy" ]; then
    config_args="--config-file=$source_root_abs/.clang-tidy"
fi

status=0

cd "$source_root_abs"
python3 - "$build_dir_abs/compile_commands.json" "$source_root_abs" > "$file_list" <<'PY'
import json
import os
import sys

compile_commands_path = sys.argv[1]
source_root = os.path.realpath(sys.argv[2])
seen = set()
files = []

with open(compile_commands_path, "r", encoding="utf-8") as handle:
    compile_commands = json.load(handle)

for entry in compile_commands:
    path = entry.get("file")
    if not path:
        continue

    abs_path = os.path.realpath(path)
    if not abs_path.startswith(source_root + os.sep):
        continue
    if not abs_path.endswith(".c"):
        continue

    rel_path = os.path.relpath(abs_path, source_root)
    if not (rel_path.startswith("src" + os.sep) or rel_path.startswith("tests" + os.sep)):
        continue
    if rel_path in seen:
        continue

    seen.add(rel_path)
    files.append(rel_path)

for rel_path in sorted(files):
    print(rel_path)
PY

while IFS= read -r file; do
    if [ -n "$config_args" ]; then
        if ! "$clang_tidy_bin" \
            -p "$build_dir_abs" \
            -header-filter="$header_filter" \
            "$config_args" \
            --quiet \
            "$file" >>"$log_file" 2>&1; then
            status=1
        fi
    else
        if ! "$clang_tidy_bin" \
            -p "$build_dir_abs" \
            -header-filter="$header_filter" \
            --quiet \
            "$file" >>"$log_file" 2>&1; then
            status=1
        fi
    fi
done < "$file_list"

exit "$status"
