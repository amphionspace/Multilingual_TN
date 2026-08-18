#!/bin/bash
# Exact TN and semantic-anchor regressions for OOV-derived zh/ru inputs.
#
# Usage:
#   bash test/scripts/build_unified.sh       # if test/bin/tts_cli is absent/stale
#   bash test/scripts/run_oov_regressions.sh
#
# Optional overrides:
#   TTS_CLI=/path/to/tts_cli
#   OOV_CASES=/path/to/tn_oov_regressions.tsv
set -uo pipefail

TEST="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "${TEST}/.." && pwd)"
BIN="${TTS_CLI:-${TEST}/bin/tts_cli}"
CASES="${OOV_CASES:-${TEST}/oov/tn_oov_regressions.tsv}"

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
base_cases="$(awk -F'\t' '!/^#/ && NF == 7 && $3 != "boundary" { count++ } END { print count + 0 }' "${CASES}")"
boundary_cases="$(awk -F'\t' '!/^#/ && NF == 7 && $3 == "boundary" { count++ } END { print count + 0 }' "${CASES}")"
zh_cases="$(awk -F'\t' '!/^#/ && NF == 7 && $2 == "zh" { count++ } END { print count + 0 }' "${CASES}")"
ru_cases="$(awk -F'\t' '!/^#/ && NF == 7 && $2 == "ru" { count++ } END { print count + 0 }' "${CASES}")"
if [[ "${base_cases}" -ne 27 || "${boundary_cases}" -ne 18 || \
      "${zh_cases}" -ne 37 || "${ru_cases}" -ne 8 ]]; then
  echo "Unexpected fixture coverage: base=${base_cases}/27 boundary=${boundary_cases}/18 zh=${zh_cases}/37 ru=${ru_cases}/8" >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

pass=0
fail=0
total=0
infrastructure_fail=0
summary=""

for locale in zh ru; do
  data_dir="${ROOT}/data/${locale}"
  in_file="${WORK}/${locale}.in"
  out_file="${WORK}/${locale}.out"

  awk -F'\t' -v locale="${locale}" \
    '!/^#/ && NF >= 5 && $2 == locale { print $4 }' "${CASES}" > "${in_file}"
  expected_lines="$(awk 'END { print NR + 0 }' "${in_file}")"
  if [[ "${expected_lines}" -eq 0 ]]; then
    echo "SKIP ${locale}: no cases"
    continue
  fi

  if ! "${BIN}" --data "${data_dir}" < "${in_file}" > "${out_file}"; then
    echo "FAIL ${locale}: normalizer exited non-zero" >&2
    fail=$((fail + expected_lines))
    total=$((total + expected_lines))
    summary="${summary}${locale} runtime-fail ${expected_lines}"$'\n'
    continue
  fi

  actual_lines="$(awk 'END { print NR + 0 }' "${out_file}")"
  if [[ "${actual_lines}" -ne "${expected_lines}" ]]; then
    echo "FAIL ${locale}: expected ${expected_lines} output lines, got ${actual_lines}" >&2
    infrastructure_fail=1
  fi

  locale_index=0
  locale_pass=0
  locale_fail=0
  while IFS=$'\t' read -r id row_locale category input expected required forbidden _extra; do
    case "${id}" in ''|'#'*) continue ;; esac
    [[ "${row_locale}" == "${locale}" ]] || continue

    [[ "${required}" == "-" ]] && required=""
    [[ "${forbidden}" == "-" ]] && forbidden=""

    locale_index=$((locale_index + 1))
    total=$((total + 1))
    got="$(sed -n "${locale_index}p" "${out_file}")"
    case_failed=0

    if [[ "${got}" != "${expected}" ]]; then
      printf 'FAIL [%s/%s] exact TN mismatch\n      input:    %s\n      expected: %s\n      got:      %s\n' \
        "${locale}" "${id}" "${input}" "${expected}" "${got}"
      case_failed=1
    fi
    if [[ -n "${required}" && "${got}" != *"${required}"* ]]; then
      printf 'FAIL [%s/%s] missing semantic anchor: %s\n' \
        "${locale}" "${id}" "${required}"
      case_failed=1
    fi
    if [[ -n "${forbidden}" && "${got}" == *"${forbidden}"* ]]; then
      printf 'FAIL [%s/%s] unresolved/forbidden literal remains: %s\n' \
        "${locale}" "${id}" "${forbidden}"
      case_failed=1
    fi

    if [[ "${case_failed}" -eq 0 ]]; then
      pass=$((pass + 1))
      locale_pass=$((locale_pass + 1))
    else
      fail=$((fail + 1))
      locale_fail=$((locale_fail + 1))
    fi
  done < "${CASES}"

  summary="${summary}${locale} ${locale_pass} passed ${locale_fail} failed"$'\n'
done

echo "--- per locale ---"
printf '%s' "${summary}" | sed 's/^/  /'
echo "=== result: ${pass} passed, ${fail} failed (${total} cases) ==="
if [[ "${fail}" -eq 0 && "${infrastructure_fail}" -eq 0 ]]; then
  echo ">>> ALL GREEN"
  exit 0
fi
echo ">>> FAILURES PRESENT"
exit 1
