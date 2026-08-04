#!/bin/bash
# Golden tests for the Chinese G2P tone-sandhi module (data/zh_g2p).
#
# Cases live in test/g2p/zh_sandhi_cases.tsv as  <bucket> TAB <input> TAB <expected>.
# Every case is fed through one tts_cli run, then compared line by line, so a rule that
# silently fails to compile (the engine leaves the text untouched) shows up as a failure.
#
# Usage:
#   bash test/scripts/build_unified.sh          # or set TTS_CLI=<binary>
#   bash test/scripts/run_g2p_tests.sh
set -uo pipefail
TEST="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "${TEST}/.." && pwd)"
BIN="${TTS_CLI:-${TEST}/bin/tts_cli}"
CASES="${TEST}/g2p/zh_sandhi_cases.tsv"
DATA="${ROOT}/data/zh_g2p"

if [[ ! -x "${BIN}" ]]; then
  echo "Missing normalizer binary: ${BIN}" >&2
  echo "Build it first: bash test/scripts/build_unified.sh" >&2
  exit 1
fi
if [[ ! -f "${CASES}" ]]; then
  echo "Missing cases file: ${CASES}" >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
IN="${WORK}/in.txt"
OUT="${WORK}/out.txt"

awk -F'\t' '!/^#/ && NF >= 3 { print $2 }' "${CASES}" > "${IN}"
"${BIN}" --data "${DATA}" < "${IN}" > "${OUT}"

pass=0
fail=0
idx=0
buckets=""
while IFS=$'\t' read -r bucket input expected; do
  case "${bucket}" in ''|'#'*) continue ;; esac
  [[ -z "${expected}" ]] && continue
  idx=$((idx + 1))
  got="$(sed -n "${idx}p" "${OUT}")"
  if [[ "${got}" == "${expected}" ]]; then
    pass=$((pass + 1))
    buckets="${buckets}${bucket} ok"$'\n'
  else
    fail=$((fail + 1))
    buckets="${buckets}${bucket} FAIL"$'\n'
    printf 'FAIL [%s] %s\n      expected: %s\n      got:      %s\n' \
      "${bucket}" "${input}" "${expected}" "${got}"
  fi
done < "${CASES}"

echo "--- per bucket ---"
printf '%s' "${buckets}" | sort | uniq -c | sed 's/^/  /'
echo "=== result: ${pass} passed, ${fail} failed (${idx} cases) ==="
[[ ${fail} -eq 0 ]] && echo ">>> ALL GREEN" || echo ">>> FAILURES PRESENT"
exit "${fail}"
