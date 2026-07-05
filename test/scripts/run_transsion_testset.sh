#!/bin/bash
set -euo pipefail

TEST="$(cd "$(dirname "$0")/.." && pwd)"
cd "${TEST}"

if [[ ! -d "${TEST}/../rules_v2" ]]; then
  echo "Missing rules_v2 directory at ${TEST}/../rules_v2" >&2
  exit 1
fi

TESTSET_DIR="${TRANS_TESTSET_DIR:-${TEST}/transsion_testset}"
if [[ ! -d "${TESTSET_DIR}" ]]; then
  echo "Missing testset dir: ${TESTSET_DIR}" >&2
  exit 1
fi

RUN_TAG="${RUN_TAG:-$(date +%Y%m%d_%H%M%S)}"
RUN_ROOT="${TRANS_OUT_DIR:-${TEST}/transsion_runs}/${RUN_TAG}"
OUT_DIR="${RUN_ROOT}/out"
REPORT_DIR="${RUN_ROOT}/report"
mkdir -p "${OUT_DIR}" "${REPORT_DIR}"

export TTS_RULES_FORMAT="${TTS_RULES_FORMAT:-v2}"

RU_MODEL="${RU_MORPH_MODEL:-$(cd .. && pwd)/original/morphodita/models/russian-syntagrus-morphodita-only.tagger}"

SUMMARY_CSV="${REPORT_DIR}/summary.csv"
SUMMARY_TXT="${REPORT_DIR}/summary.txt"
echo "locale,input_file,output_file,input_lines,output_lines,empty_output_lines,status,note" > "${SUMMARY_CSV}"

KNOWN_LOCALES=(en zh ar bn ru)
FAILED=0

is_known_locale() {
  local x="$1"
  for l in "${KNOWN_LOCALES[@]}"; do
    if [[ "${l}" == "${x}" ]]; then
      return 0
    fi
  done
  return 1
}

quote_csv() {
  local s="$1"
  s="${s//\"/\"\"}"
  printf '"%s"' "${s}"
}

for F in "${TESTSET_DIR}"/*_1000_sample_sent.txt; do
  if [[ ! -f "${F}" ]]; then
    continue
  fi
  BN="$(basename "${F}")"
  LOC="${BN%%_*}"

  if ! is_known_locale "${LOC}"; then
    echo "${LOC},${BN},,0,0,0,SKIP,unsupported locale in current TN binaries" >> "${SUMMARY_CSV}"
    echo "SKIP ${LOC}: ${BN} (unsupported locale)" >&2
    continue
  fi

  BIN="./bin/${LOC}_tts"
  if [[ ! -x "${BIN}" ]]; then
    echo "${LOC},${BN},,0,0,0,FAIL,missing executable ${BIN}" >> "${SUMMARY_CSV}"
    echo "FAIL ${LOC}: missing ${BIN} (run scripts/build.sh)" >&2
    FAILED=1
    continue
  fi

  OUT_FILE="${OUT_DIR}/${LOC}.out.txt"
  if [[ "${LOC}" == "ru" ]]; then
    if [[ ! -f "${RU_MODEL}" ]]; then
      echo "${LOC},${BN},${LOC}.out.txt,0,0,0,FAIL,missing RU_MORPH_MODEL ${RU_MODEL}" >> "${SUMMARY_CSV}"
      echo "FAIL ru: MorphoDiTa model not found at ${RU_MODEL} (set RU_MORPH_MODEL=...)" >&2
      FAILED=1
      continue
    fi
    "${BIN}" --morph-model "${RU_MODEL}" < "${F}" > "${OUT_FILE}"
  else
    "${BIN}" < "${F}" > "${OUT_FILE}"
  fi

  IN_LINES="$(awk 'END{print NR+0}' "${F}")"
  OUT_LINES="$(awk 'END{print NR+0}' "${OUT_FILE}")"
  EMPTY_OUT_LINES="$(awk 'NF==0{c++} END{print c+0}' "${OUT_FILE}")"

  STATUS="OK"
  NOTE="line_count_match"
  if [[ "${IN_LINES}" != "${OUT_LINES}" ]]; then
    STATUS="WARN"
    NOTE="line_count_mismatch"
  fi

  echo "${LOC},${BN},$(basename "${OUT_FILE}"),${IN_LINES},${OUT_LINES},${EMPTY_OUT_LINES},${STATUS},${NOTE}" >> "${SUMMARY_CSV}"
  echo "${STATUS} ${LOC}: in=${IN_LINES}, out=${OUT_LINES}, empty_out=${EMPTY_OUT_LINES}"
done

{
  echo "Transsion testset run"
  echo "  run_tag: ${RUN_TAG}"
  echo "  rules_format: ${TTS_RULES_FORMAT}"
  echo "  testset: ${TESTSET_DIR}"
  echo "  out_dir: ${OUT_DIR}"
  echo "  summary: ${SUMMARY_CSV}"
  echo
  echo "Per-locale summary:"
  awk -F, 'NR==1{next} {printf "  - %s: status=%s in=%s out=%s empty_out=%s note=%s\n", $1, $7, $4, $5, $6, $8}' "${SUMMARY_CSV}"
} > "${SUMMARY_TXT}"

ln -snf "${RUN_ROOT}" "${TEST}/transsion_runs/latest"

cat "${SUMMARY_TXT}"
exit "${FAILED}"
