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
find src tests -type f -name '*.c' | LC_ALL=C sort > "$file_list"
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
