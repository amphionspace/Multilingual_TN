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
ORIG_PWD="${PWD}"          # resolve user-supplied relative paths against the invocation dir
cd "${TEST}"

# TTS_CLI / TRANSSION_TESTSET_DIR are usually passed relative to the repo root, but we
# run from test/ — resolve them before anything else so relative paths just work.
abspath() { case "$1" in /*) printf '%s' "$1";; *) printf '%s/%s' "${ORIG_PWD}" "$1";; esac; }
if [[ -n "${TTS_CLI:-}" ]]; then BIN="$(abspath "${TTS_CLI}")"; else BIN="./bin/tts_cli"; fi
TS="${TRANSSION_TESTSET_DIR:-}"
[[ -n "${TS}" ]] && TS="$(abspath "${TS}")"
BASE="baseline_pretrim"

if [[ ! -x "${BIN}" ]]; then
  cat >&2 <<EOF
Missing normalizer binary: ${BIN}

This script only *checks* an already-built binary; it does not build one.
Pick one:

  1) Verify the trimmed shared library (usual case):
       bash test/scripts/build_tts_so.sh
       TTS_CLI=build/tts_cli_so bash test/scripts/verify_icu_trim.sh

  2) Verify the plain (untrimmed) unified CLI:
       bash test/scripts/build_unified.sh
       bash test/scripts/verify_icu_trim.sh

  3) Point at any binary that accepts '--data <dir>' on stdin:
       TTS_CLI=/path/to/binary bash test/scripts/verify_icu_trim.sh

The 1000-sentence corpus lives outside the repo; set TRANSSION_TESTSET_DIR=<dir>
to include it (otherwise that group is skipped).
EOF
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
