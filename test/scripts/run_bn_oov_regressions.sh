#!/bin/bash
# Exact Bengali TN regressions for broadly reusable OOV fixes.
#
# Usage:
#   bash test/scripts/build_unified.sh
#   bash test/scripts/run_bn_oov_regressions.sh
#
# Optional overrides:
#   TTS_CLI=/path/to/tts_cli
#   BN_OOV_CASES=/path/to/bn_oov_regressions.tsv
#   MULTILINGUAL_LITS_ROOT=/path/to/Multilingual_LITs
#   REQUIRE_BN_TTS_INTEGRATION=1
set -uo pipefail

TEST="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "${TEST}/.." && pwd)"
BIN="${TTS_CLI:-${TEST}/bin/tts_cli}"
CASES="${BN_OOV_CASES:-${TEST}/oov/bn_oov_regressions.tsv}"

if [[ ! -x "${BIN}" ]]; then
  echo "Missing normalizer binary: ${BIN}" >&2
  echo "Build it first: bash test/scripts/build_unified.sh" >&2
  exit 1
fi
if [[ ! -f "${CASES}" ]]; then
  echo "Missing cases file: ${CASES}" >&2
  exit 1
fi

malformed_rows="$(awk -F'\t' '!/^#/ && NF != 0 && NF != 6 { print NR }' "${CASES}")"
if [[ -n "${malformed_rows}" ]]; then
  echo "Malformed fixture rows (expected exactly 6 TSV fields): ${malformed_rows//$'\n'/, }" >&2
  exit 1
fi
duplicate_ids="$(awk -F'\t' '!/^#/ && NF == 6 { print $1 }' "${CASES}" | sort | uniq -d)"
if [[ -n "${duplicate_ids}" ]]; then
  echo "Duplicate fixture IDs: ${duplicate_ids//$'\n'/, }" >&2
  exit 1
fi
case_count="$(awk -F'\t' '!/^#/ && NF == 6 { count++ } END { print count + 0 }' "${CASES}")"
if [[ "${case_count}" -ne 41 ]]; then
  echo "Unexpected Bengali fixture coverage: ${case_count}/41" >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
IN_FILE="${WORK}/bn.in"
OUT_FILE="${WORK}/bn.out"
awk -F'\t' '!/^#/ && NF == 6 { print $3 }' "${CASES}" > "${IN_FILE}"

if ! "${BIN}" --data "${ROOT}/data/bn" < "${IN_FILE}" > "${OUT_FILE}"; then
  echo "FAIL bn: normalizer exited non-zero" >&2
  exit 1
fi
actual_lines="$(awk 'END { print NR + 0 }' "${OUT_FILE}")"
if [[ "${actual_lines}" -ne "${case_count}" ]]; then
  echo "FAIL bn: expected ${case_count} output lines, got ${actual_lines}" >&2
  exit 1
fi

pass=0
fail=0
index=0
while IFS=$'\t' read -r id category input expected required forbidden _extra; do
  case "${id}" in ''|'#'*) continue ;; esac
  index=$((index + 1))
  got="$(sed -n "${index}p" "${OUT_FILE}")"
  [[ "${required}" == "-" ]] && required=""
  [[ "${forbidden}" == "-" ]] && forbidden=""
  case_failed=0

  if [[ "${got}" != "${expected}" ]]; then
    printf 'FAIL [%s/%s] exact TN mismatch\n      input:    %s\n      expected: %s\n      got:      %s\n' \
      "${category}" "${id}" "${input}" "${expected}" "${got}"
    case_failed=1
  fi
  if [[ -n "${required}" && "${got}" != *"${required}"* ]]; then
    printf 'FAIL [%s/%s] missing semantic anchor: %s\n' "${category}" "${id}" "${required}"
    case_failed=1
  fi
  if [[ -n "${forbidden}" && "${got}" == *"${forbidden}"* ]]; then
    printf 'FAIL [%s/%s] unresolved/forbidden literal remains: %s\n' "${category}" "${id}" "${forbidden}"
    case_failed=1
  fi

  if [[ "${case_failed}" -eq 0 ]]; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
  fi
done < "${CASES}"

integration_fail=0
tts_repo="${MULTILINGUAL_LITS_ROOT:-${ROOT}/../../Multilingual_LITs}"
python_bin="${PYTHON_BIN:-python3}"
if [[ -d "${tts_repo}/lits/text" ]]; then
  echo "--- Bengali TN -> bn_en_dict_mixed_cleaners -> Text2Id ---"
  if ! PYTHONPATH="${tts_repo}${PYTHONPATH:+:${PYTHONPATH}}" \
      "${python_bin}" - "${CASES}" "${OUT_FILE}" <<'PY'
import csv
import sys

cases_path, output_path = sys.argv[1:]
from lits.text import text_to_sequence
from lits.text.char_symbols.symbol_inventories import lang2inventory

with open(cases_path, encoding="utf-8", newline="") as handle:
    rows = [
        row for row in csv.reader(handle, delimiter="\t")
        if row and not row[0].startswith("#")
    ]
with open(output_path, encoding="utf-8") as handle:
    outputs = handle.read().splitlines()

if len(rows) != len(outputs):
    raise SystemExit(
        f"integration input mismatch: {len(rows)} fixtures, {len(outputs)} TN outputs"
    )

symbol_to_id = lang2inventory["bn-en"]["symbol_to_id"]
unk_id = symbol_to_id["<unk>"]
failed = False
for row, normalized in zip(rows, outputs):
    case_id = row[0]
    ids, cleaned = text_to_sequence(normalized, ["bn_en_dict_mixed_cleaners"])
    unknown = []
    for token in cleaned.split():
        if token not in symbol_to_id and token not in unknown:
            unknown.append(token)
    if not ids or unk_id in ids or unknown:
        failed = True
        print(
            f"FAIL [Text2Id/{case_id}] unknown={unknown} ids={len(ids)} "
            f"tn={normalized!r} cleaned={cleaned!r}",
            file=sys.stderr,
        )

if failed:
    raise SystemExit(1)
print(f"PASS Text2Id: {len(rows)} cases, zero <unk>")
PY
  then
    integration_fail=1
  fi
elif [[ "${REQUIRE_BN_TTS_INTEGRATION:-0}" == "1" ]]; then
  echo "FAIL: Multilingual_LITs not found at ${tts_repo}" >&2
  integration_fail=1
else
  echo "SKIP Text2Id integration: Multilingual_LITs not found at ${tts_repo}"
fi

echo "=== Bengali OOV result: ${pass} passed, ${fail} failed (${case_count} cases) ==="
if [[ "${fail}" -eq 0 && "${integration_fail}" -eq 0 ]]; then
  echo ">>> ALL GREEN"
  exit 0
fi
echo ">>> FAILURES PRESENT"
exit 1
