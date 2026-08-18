#!/bin/bash
# Exact and semantic regressions for the Chinese TN -> en-zh-g2p pipeline.
#
# Usage:
#   bash test/scripts/build_unified.sh       # if test/bin/tts_cli is absent/stale
#   bash test/scripts/run_zh_tn_g2p_regressions.sh
#
# Optional overrides:
#   TTS_CLI=/path/to/tts_cli
#   TN_G2P_CASES=/path/to/zh_tn_g2p_regressions.tsv
set -uo pipefail

TEST="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "${TEST}/.." && pwd)"
BIN="${TTS_CLI:-${TEST}/bin/tts_cli}"
CASES="${TN_G2P_CASES:-${TEST}/oov/zh_tn_g2p_regressions.tsv}"

if [[ ! -x "${BIN}" ]]; then
  echo "Missing normalizer binary: ${BIN}" >&2
  echo "Build it first: bash test/scripts/build_unified.sh" >&2
  exit 1
fi
if [[ ! -f "${CASES}" ]]; then
  echo "Missing cases file: ${CASES}" >&2
  exit 1
fi

malformed_rows="$(awk -F'\t' '!/^#/ && NF != 0 && NF != 7 { print NR }' "${CASES}")"
if [[ -n "${malformed_rows}" ]]; then
  echo "Malformed fixture rows (expected exactly 7 TSV fields): ${malformed_rows//$'\n'/, }" >&2
  exit 1
fi
duplicate_ids="$(awk -F'\t' '!/^#/ && NF == 7 { print $1 }' "${CASES}" | sort | uniq -d)"
if [[ -n "${duplicate_ids}" ]]; then
  echo "Duplicate fixture IDs: ${duplicate_ids//$'\n'/, }" >&2
  exit 1
fi
case_count="$(awk -F'\t' '!/^#/ && NF == 7 { count++ } END { print count + 0 }' "${CASES}")"
if [[ "${case_count}" -ne 5 ]]; then
  echo "Unexpected fixture coverage: cases=${case_count}/5" >&2
  exit 1
fi

count_literal() {
  local rest="$1"
  local needle="$2"
  local count=0
  while [[ -n "${needle}" && "${rest}" == *"${needle}"* ]]; do
    rest="${rest#*"${needle}"}"
    count=$((count + 1))
  done
  printf '%s' "${count}"
}

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
IN="${WORK}/in.txt"
TN_OUT="${WORK}/tn.out"
G2P_OUT="${WORK}/g2p.out"

awk -F'\t' '!/^#/ && NF == 7 { print $2 }' "${CASES}" > "${IN}"
if ! "${BIN}" --data "${ROOT}/data/zh" < "${IN}" > "${TN_OUT}"; then
  echo "FAIL: Chinese TN exited non-zero" >&2
  exit 1
fi
if ! "${BIN}" --data "${ROOT}/data/en-zh-g2p" < "${TN_OUT}" > "${G2P_OUT}"; then
  echo "FAIL: en-zh-g2p exited non-zero" >&2
  exit 1
fi

input_lines="$(awk 'END { print NR + 0 }' "${IN}")"
tn_lines="$(awk 'END { print NR + 0 }' "${TN_OUT}")"
g2p_lines="$(awk 'END { print NR + 0 }' "${G2P_OUT}")"
if [[ "${tn_lines}" -ne "${input_lines}" || "${g2p_lines}" -ne "${input_lines}" ]]; then
  echo "FAIL: line count changed across pipeline: input=${input_lines} tn=${tn_lines} g2p=${g2p_lines}" >&2
  exit 1
fi

pass=0
fail=0
index=0
while IFS=$'\t' read -r id input expected_tn expected_g2p required_g2p required_count forbidden_g2p _extra; do
  case "${id}" in ''|'#'*) continue ;; esac
  index=$((index + 1))
  got_tn="$(sed -n "${index}p" "${TN_OUT}")"
  got_g2p="$(sed -n "${index}p" "${G2P_OUT}")"
  case_failed=0

  if [[ "${got_tn}" != "${expected_tn}" ]]; then
    printf 'FAIL [%s] exact TN mismatch\n      input:    %s\n      expected: %s\n      got:      %s\n' \
      "${id}" "${input}" "${expected_tn}" "${got_tn}"
    case_failed=1
  fi

  if [[ "${expected_g2p}" != "-" && "${got_g2p}" != "${expected_g2p}" ]]; then
    printf 'FAIL [%s] exact G2P mismatch\n      expected: %s\n      got:      %s\n' \
      "${id}" "${expected_g2p}" "${got_g2p}"
    case_failed=1
  fi

  if [[ "${required_g2p}" != "-" ]]; then
    actual_count="$(count_literal "${got_g2p}" "${required_g2p}")"
    if [[ "${actual_count}" -ne "${required_count}" ]]; then
      printf 'FAIL [%s] required G2P literal count: expected %s, got %s\n      literal: %s\n      G2P:    %s\n' \
        "${id}" "${required_count}" "${actual_count}" "${required_g2p}" "${got_g2p}"
      case_failed=1
    fi
  fi

  if [[ "${forbidden_g2p}" != "-" && "${got_g2p}" == *"${forbidden_g2p}"* ]]; then
    printf 'FAIL [%s] forbidden G2P literal remains: %s\n      G2P: %s\n' \
      "${id}" "${forbidden_g2p}" "${got_g2p}"
    case_failed=1
  fi

  if [[ "${case_failed}" -eq 0 ]]; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
  fi
done < "${CASES}"

echo "=== result: ${pass} passed, ${fail} failed (${index} cases) ==="
if [[ "${fail}" -eq 0 ]]; then
  echo ">>> ALL GREEN"
  exit 0
fi
echo ">>> FAILURES PRESENT"
exit 1
