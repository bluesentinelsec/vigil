#!/bin/sh

set -eu

usage() {
    echo "usage: $0 --check|--in-place" >&2
    exit 1
}

if [ "$#" -ne 1 ]; then
    usage
fi

mode="$1"

if [ -n "${CLANG_FORMAT_BIN:-}" ]; then
    clang_format="$CLANG_FORMAT_BIN"
elif command -v clang-format-17 >/dev/null 2>&1; then
    clang_format="clang-format-17"
elif command -v clang-format >/dev/null 2>&1; then
    clang_format="clang-format"
else
    echo "clang-format not found" >&2
    exit 1
fi

case "$mode" in
    --check)
        format_args="--dry-run --Werror"
        ;;
    --in-place)
        format_args="-i"
        ;;
    *)
        usage
        ;;
esac

echo "using $clang_format: $("$clang_format" --version)" >&2

find src include tests \( -name '*.c' -o -name '*.h' \) -print0 \
    | xargs -0 "$clang_format" $format_args
