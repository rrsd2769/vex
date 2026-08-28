#!/usr/bin/env bash
#
# Golden-file test harness for vex.
#
#   ./tests/run_tests.sh              run every test
#   ./tests/run_tests.sh lex          run only tests/lex/
#   UPDATE=1 ./tests/run_tests.sh     rewrite every .expected from current
#                                     output ("blessing" the results)
#
# Each tests/<category>/<name>.vx is run through the vex binary; stdout and
# stderr are captured together and compared against <name>.expected.
#
# UPDATE=1 is powerful and dangerous: it makes every test pass by definition.
# Only use it after you have READ the new output and agree it is correct.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/vex"
FILTER="${1:-}"

if [[ ! -x "$BIN" ]]; then
  echo "no binary at $BIN"
  echo "build first:  cmake -S . -B build -G Ninja && cmake --build build"
  exit 1
fi

SEARCH_DIR="$ROOT/tests/$FILTER"
if [[ ! -d "$SEARCH_DIR" ]]; then
  echo "no such test directory: $SEARCH_DIR"
  exit 1
fi

pass=0
fail=0
blessed=0
failed_names=()

while IFS= read -r src; do
  rel="${src#"$ROOT"/}"
  exp="${src%.vx}.expected"
  # Run from the repo root with a RELATIVE path, so any filename appearing in
  # a diagnostic is identical on every machine. Absolute paths would make
  # every .expected file machine-specific.
  actual="$(cd "$ROOT" && "$BIN" "$rel" 2>&1)"

  if [[ -n "${UPDATE:-}" ]]; then
    printf '%s\n' "$actual" >"$exp"
    echo "blessed  $rel"
    blessed=$((blessed + 1))
    continue
  fi

  if [[ ! -f "$exp" ]]; then
    echo "MISSING  $rel"
    echo "         no .expected file -- create one with: UPDATE=1 $0"
    fail=$((fail + 1))
    failed_names+=("$rel")
    continue
  fi

  if diff -q "$exp" <(printf '%s\n' "$actual") >/dev/null 2>&1; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
    failed_names+=("$rel")
    echo "FAIL     $rel"
    diff -u --label expected "$exp" --label actual <(printf '%s\n' "$actual") \
      | tail -n +3 | sed 's/^/         /'
    echo
  fi
done < <(find "$SEARCH_DIR" -name '*.vx' | sort)

echo "-----------------------------------------"
if [[ -n "${UPDATE:-}" ]]; then
  echo "blessed $blessed file(s)"
  exit 0
fi

total=$((pass + fail))
if [[ $total -eq 0 ]]; then
  echo "no tests found under $SEARCH_DIR"
  exit 1
fi

echo "$pass/$total passed"
if [[ $fail -gt 0 ]]; then
  echo
  echo "failing:"
  for n in "${failed_names[@]}"; do echo "  $n"; done
  exit 1
fi
