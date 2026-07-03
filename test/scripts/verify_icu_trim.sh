#!/bin/bash
# Regression check for the ICU-size-reduction work.
#
# Runs a given tts_cli binary over three real corpora and diffs the output
# byte-for-byte against the committed pre-trim baseline (test/baseline_pretrim).
# A trimmed ICU build PASSES only if every corpus is byte-identical to baseline.
#
# Usage:
#   TTS_CLI=/path/to/tts_cli_trimmed \
#   [DYLD_LIBRARY_PATH=/path/to/trimmed/icu/lib] \
#   [TRANSSION_TESTSET_DIR=/path/to/transsion_testset] \
#   bash test/scripts/verify_icu_trim.sh
#
# TTS_CLI defaults to ./test/bin/tts_cli.
# The 1000-sentence inputs live outside the repo; set TRANSSION_TESTSET_DIR to
# their directory (files <lang>_1000_sample_sent.txt). If unset/missing, that
# corpus is skipped (cmos + smoke still run).
set -uo pipefail
TEST="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "${TEST}/.." && pwd)"
cd "${TEST}"

BIN="${TTS_CLI:-./bin/tts_cli}"
BASE="baseline_pretrim"
TS="${TRANSSION_TESTSET_DIR:-}"

if [[ ! -x "${BIN}" ]]; then
  echo "Missing tts_cli binary: ${BIN} (set TTS_CLI=...)" >&2
  exit 1
fi
if [[ ! -d "${BASE}" ]]; then
  echo "Missing baseline: ${TEST}/${BASE}" >&2
  exit 1
fi

PASS=0; FAIL=0; SKIP=0
mkdir -p out/icu_trim_check

check() { # <label> <lang> <input> <baseline-out>
  local label="$1" lang="$2" in="$3" gold="$4"
  if [[ ! -f "${in}" ]]; then echo "  SKIP ${label} ${lang} (no input: ${in})"; SKIP=$((SKIP+1)); return; fi
  if [[ ! -f "${gold}" ]]; then echo "  SKIP ${label} ${lang} (no baseline: ${gold})"; SKIP=$((SKIP+1)); return; fi
  local outf="out/icu_trim_check/${label}_${lang}.out"
  "${BIN}" --data "${ROOT}/data/${lang}" < "${in}" > "${outf}" 2>/dev/null
  if diff -q "${gold}" "${outf}" >/dev/null; then
    echo "  OK   ${label} ${lang}"; PASS=$((PASS+1))
  else
    echo "  FAIL ${label} ${lang}  (diff ${gold} ${outf})"; FAIL=$((FAIL+1))
  fi
}

echo "=== transsion_cmos x3 ==="
for L in en zh ru; do check cmos "$L" "transsion_cmos/${L}_cmos.txt" "${BASE}/cmos/${L}.out"; done

echo "=== transsion 1000-sample x5 ==="
for L in en zh ar ru bn; do
  in="${TS:+${TS}/${L}_1000_sample_sent.txt}"
  check 1000 "$L" "${in:-/nonexistent}" "${BASE}/transsion1000/${L}.out"
done

echo "=== smoke x5 ==="
for L in en zh ar bn ru; do check smoke "$L" "in/${L}.txt" "${BASE}/smoke/${L}.out"; done

echo "=== result: ${PASS} passed, ${FAIL} failed, ${SKIP} skipped ==="
[[ ${FAIL} -eq 0 ]] && echo ">>> BYTE-IDENTICAL to baseline (no regression)" || echo ">>> REGRESSION DETECTED"
exit "${FAIL}"
