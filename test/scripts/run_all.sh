#!/bin/bash
# Smoke runner: normalize test/in/<locale>.txt for every locale through the unified
# binary and write test/out/<locale>.out. (The golden testset was removed; regression
# is done by diffing against a known-good baseline out-of-band.)
set -euo pipefail
TEST="$(cd "$(dirname "$0")/.." && pwd)"
cd "${TEST}"

BIN="./bin/tts_cli"
if [[ ! -x "${BIN}" ]]; then
  echo "Missing ${BIN} (run scripts/build_unified.sh)" >&2
  exit 1
fi

LOCALES=(en zh ar bn ru)
mkdir -p out
FAILED=0
for L in "${LOCALES[@]}"; do
  IN="./in/${L}.txt"
  OUT="./out/${L}.out"
  if [[ ! -f "${IN}" ]]; then
    echo "SKIP ${L}: missing ${IN}" >&2
    continue
  fi
  if "${BIN}" --data "../data/${L}" < "${IN}" > "${OUT}"; then
    echo "OK   ${L} -> ${OUT} ($(awk 'END{print NR+0}' "${OUT}") lines)"
  else
    echo "FAIL ${L}" >&2
    FAILED=1
  fi
done
exit "${FAILED}"
