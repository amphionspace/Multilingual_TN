#!/bin/bash
# Focused English OOV and semantic-preservation regressions:
# raw text -> English TN -> English G2P -> model token inventory.
set -uo pipefail

TEST="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "${TEST}/.." && pwd)"
BIN="${TTS_CLI:-${TEST}/bin/tts_cli}"
CASES="${EN_TN_G2P_CASES:-${TEST}/oov/en_tn_g2p_regressions.tsv}"
MODEL_TOKENS="${EN_MODEL_TOKENS:-${ROOT}/data/en-zh-g2p/model_tokens.json}"

if [[ ! -x "${BIN}" ]]; then
  echo "Missing normalizer binary: ${BIN}" >&2
  echo "Build it first: bash test/scripts/build_unified.sh" >&2
  exit 1
fi
for required_file in "${CASES}" "${MODEL_TOKENS}"; do
  if [[ ! -f "${required_file}" ]]; then
    echo "Missing regression input: ${required_file}" >&2
    exit 1
  fi
done

malformed_rows="$(awk -F'\t' '!/^#/ && NF != 0 && NF != 8 { print NR }' "${CASES}")"
if [[ -n "${malformed_rows}" ]]; then
  echo "Malformed fixture rows (expected exactly 8 TSV fields): ${malformed_rows//$'\n'/, }" >&2
  exit 1
fi
duplicate_ids="$(awk -F'\t' '!/^#/ && NF == 8 { print $1 }' "${CASES}" | sort | uniq -d)"
if [[ -n "${duplicate_ids}" ]]; then
  echo "Duplicate fixture IDs: ${duplicate_ids//$'\n'/, }" >&2
  exit 1
fi
case_count="$(awk -F'\t' '!/^#/ && NF == 8 { count++ } END { print count + 0 }' "${CASES}")"
if [[ "${case_count}" -ne 13 ]]; then
  echo "Unexpected fixture coverage: cases=${case_count}/13" >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
IN="${WORK}/in.txt"
TN_OUT="${WORK}/tn.out"
G2P_OUT="${WORK}/g2p.out"
TOKEN_SET="${WORK}/model_tokens.txt"

awk -F'\t' '!/^#/ && NF == 8 { print $2 }' "${CASES}" > "${IN}"
if ! "${BIN}" --data "${ROOT}/data/en" < "${IN}" > "${TN_OUT}"; then
  echo "FAIL: English TN exited non-zero" >&2
  exit 1
fi
if ! "${BIN}" --data "${ROOT}/data/en-zh-g2p" < "${TN_OUT}" > "${G2P_OUT}"; then
  echo "FAIL: English G2P exited non-zero" >&2
  exit 1
fi

awk '
  /"tokens"[[:space:]]*:[[:space:]]*\[/ { in_tokens = 1; next }
  in_tokens && /^[[:space:]]*\]/ { exit }
  in_tokens {
    token = $0
    sub(/^[[:space:]]*"/, "", token)
    sub(/",?[[:space:]]*$/, "", token)
    if (token == "\\\"") token = "\""
    print token
  }
' "${MODEL_TOKENS}" > "${TOKEN_SET}"

input_lines="$(awk 'END { print NR + 0 }' "${IN}")"
tn_lines="$(awk 'END { print NR + 0 }' "${TN_OUT}")"
g2p_lines="$(awk 'END { print NR + 0 }' "${G2P_OUT}")"
if [[ "${tn_lines}" -ne "${input_lines}" || "${g2p_lines}" -ne "${input_lines}" ]]; then
  echo "FAIL: line count changed: input=${input_lines} tn=${tn_lines} g2p=${g2p_lines}" >&2
  exit 1
fi

pass=0
fail=0
index=0
while IFS=$'\t' read -r id input expected_tn expected_g2p required_tn forbidden_tn required_g2p forbidden_g2p _extra; do
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
  if [[ "${got_g2p}" != "${expected_g2p}" ]]; then
    printf 'FAIL [%s] exact G2P mismatch\n      expected: %s\n      got:      %s\n' \
      "${id}" "${expected_g2p}" "${got_g2p}"
    case_failed=1
  fi
  if [[ "${required_tn}" != "-" && "${got_tn}" != *"${required_tn}"* ]]; then
    printf 'FAIL [%s] missing TN semantic anchor: %s\n' "${id}" "${required_tn}"
    case_failed=1
  fi
  if [[ "${forbidden_tn}" != "-" && "${got_tn}" == *"${forbidden_tn}"* ]]; then
    printf 'FAIL [%s] forbidden TN literal remains: %s\n' "${id}" "${forbidden_tn}"
    case_failed=1
  fi
  if [[ "${required_g2p}" != "-" && "${got_g2p}" != *"${required_g2p}"* ]]; then
    printf 'FAIL [%s] missing G2P semantic anchor: %s\n' "${id}" "${required_g2p}"
    case_failed=1
  fi
  if [[ "${forbidden_g2p}" != "-" && "${got_g2p}" == *"${forbidden_g2p}"* ]]; then
    printf 'FAIL [%s] forbidden G2P literal remains: %s\n' "${id}" "${forbidden_g2p}"
    case_failed=1
  fi
  if [[ " ${got_g2p} " == *" <unk> "* ]]; then
    printf 'FAIL [%s] G2P emitted the explicit unknown token: <unk>\n' "${id}"
    case_failed=1
  fi

  unknown_tokens="$(printf '%s\n' "${got_g2p}" | tr ' ' '\n' | awk 'NF' | while IFS= read -r token; do
    grep -Fqx -- "${token}" "${TOKEN_SET}" || printf '%s\n' "${token}"
  done)"
  if [[ -n "${unknown_tokens}" ]]; then
    printf 'FAIL [%s] G2P emitted tokens outside model vocabulary: %s\n' \
      "${id}" "${unknown_tokens//$'\n'/, }"
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
