#!/bin/sh
# Run every CLI test case in tests/cases and compare the output with
# tests/expected/<name>.out.
#
#   tests/cases/<name>.args  command line arguments for bin/banker
#   tests/cases/<name>.in    commands sent to stdin (optional)
#
# Usage: tests/run_cli_tests.sh [--update]
#   --update  overwrite the expected output with the current output

cd "$(dirname "$0")/.." || exit 1

BIN=./bin/banker
UPDATE=0
[ "$1" = "--update" ] && UPDATE=1

if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found, run 'make' first" >&2
    exit 1
fi

pass=0
fail=0
actual=$(mktemp)
trap 'rm -f "$actual"' EXIT

for args_file in tests/cases/*.args; do
    name=$(basename "$args_file" .args)
    input=tests/cases/$name.in
    [ -f "$input" ] || input=/dev/null
    expected=tests/expected/$name.out

    # shellcheck disable=SC2046
    $BIN $(cat "$args_file") < "$input" > "$actual" 2>&1
    echo "exit status: $?" >> "$actual"

    if [ "$UPDATE" -eq 1 ]; then
        cp "$actual" "$expected"
    fi

    if diff -u "$expected" "$actual" > /dev/null 2>&1; then
        echo "[PASS] $name"
        pass=$((pass + 1))
    else
        echo "[FAIL] $name"
        diff -u "$expected" "$actual" | head -20
        fail=$((fail + 1))
    fi
done

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
