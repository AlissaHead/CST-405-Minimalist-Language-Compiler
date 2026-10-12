#!/bin/bash
# ============================================================================
# Regression test runner for the CST-405 mini compiler   (used by `make test`)
# ----------------------------------------------------------------------------
# For every tests/NAME.cm there is exactly one expected-result file:
#
#   tests/expected/NAME.out   the program must COMPILE, and running the
#                             generated MIPS under SPIM must print exactly this
#   tests/expected/NAME.err   the program must FAIL to compile (non-zero exit
#                             status), and the compiler's error messages on
#                             stderr must be exactly this
#
# Programs that compile are checked twice: once fully optimized, and once with
# every optimization switched off (-O0 -nopeep).  Both builds must print the
# same thing — an optimizer that changes a program's output is a broken one.
#
# Exit status: 0 if every test passed, 1 otherwise.
# ============================================================================
cd "$(dirname "$0")/.." || exit 1

COMPILER=./minicompiler
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0

# SPIM prints a banner and a "Loaded:" line before the program's own output.
run_spim() {
    spim -file "$1" 2>&1 | grep -v -e '^SPIM Version' -e '^Copyright' \
        -e '^All Rights Reserved' -e '^See the file README' -e '^Loaded:'
}

report() {   # report NAME OK/FAIL [details-file]
    if [ "$2" = OK ]; then
        printf '  PASS  %s\n' "$1"; pass=$((pass + 1))
    else
        printf '  FAIL  %s\n' "$1"; fail=$((fail + 1))
        [ -n "$3" ] && sed 's/^/        /' "$3"
    fi
}

for src in tests/*.cm; do
    name=$(basename "$src" .cm)
    out="tests/expected/$name.out"
    err="tests/expected/$name.err"

    if [ -f "$out" ]; then
        # ---- must compile and run correctly, optimized and unoptimized ----
        for flags in "" "-O0 -nopeep"; do
            label="$name${flags:+ [$flags]}"
            if ! $COMPILER "$src" "$TMP/$name.s" -q $flags >/dev/null 2>"$TMP/stderr"; then
                report "$label" FAIL "$TMP/stderr"; continue
            fi
            run_spim "$TMP/$name.s" >"$TMP/actual"
            if diff -u "$out" "$TMP/actual" >"$TMP/diff"; then
                report "$label" OK
            else
                report "$label" FAIL "$TMP/diff"
            fi
        done

    elif [ -f "$err" ]; then
        # ---- must be rejected, with exactly the expected diagnostics ----
        if $COMPILER "$src" "$TMP/$name.s" -q >/dev/null 2>"$TMP/stderr"; then
            echo "compiled successfully but was expected to fail" >"$TMP/why"
            report "$name" FAIL "$TMP/why"
        elif diff -u "$err" "$TMP/stderr" >"$TMP/diff"; then
            report "$name (rejected as expected)" OK
        else
            report "$name" FAIL "$TMP/diff"
        fi

    else
        echo "no tests/expected/$name.out or .err" >"$TMP/why"
        report "$name" FAIL "$TMP/why"
    fi
done

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
