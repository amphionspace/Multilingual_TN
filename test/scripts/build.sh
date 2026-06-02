#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST="$(cd "$(dirname "$0")/.." && pwd)"
ICU_ROOT="${ICU_ROOT:-}"
if [[ -z "${ICU_ROOT}" ]]; then
  for cand in /opt/homebrew/opt/icu4c /usr/local/opt/icu4c; do
    if [[ -f "${cand}/include/unicode/locid.h" ]]; then
      ICU_ROOT="${cand}"
      break
    fi
  done
fi
if [[ -z "${ICU_ROOT}" ]]; then
  echo "Set ICU_ROOT to your icu4c prefix (e.g. /opt/homebrew/opt/icu4c)." >&2
  exit 1
fi
mkdir -p "${TEST}/bin"
for src in en zh ar bn; do
  g++ -std=c++17 -O2 \
    "${ROOT}/${src}.cpp" "${ROOT}/tts_normalizer_engine.cpp" "${ROOT}/ru_year_spellout.cpp" \
    -I"${ROOT}" -I"${ROOT}/third_party" \
    -I"${ICU_ROOT}/include" \
    -L"${ICU_ROOT}/lib" -licui18n -licuuc -licudata \
    -o "${TEST}/bin/${src}_tts"
  echo "Built ${TEST}/bin/${src}_tts"
done

MORPH_ROOT="${ROOT}/original/morphodita"
MORPH_INC="${MORPH_ROOT}/src_lib_only"
MORPH_CPP="${MORPH_INC}/morphodita.cpp"
if [[ ! -f "${MORPH_INC}/morphodita.h" || ! -f "${MORPH_CPP}" ]]; then
  bash "${TEST}/scripts/prepare_morphodita.sh"
fi
if [[ ! -f "${MORPH_INC}/morphodita.h" || ! -f "${MORPH_CPP}" ]]; then
  echo "MorphoDiTa headers/sources are still missing under ${MORPH_INC}" >&2
  exit 1
fi
g++ -std=c++17 -O2 \
  "${ROOT}/ru.cpp" "${ROOT}/tts_normalizer_engine.cpp" "${ROOT}/ru_year_spellout.cpp" "${MORPH_CPP}" \
  -I"${ROOT}" -I"${ROOT}/third_party" -I"${MORPH_INC}" \
  -I"${ICU_ROOT}/include" \
  -L"${ICU_ROOT}/lib" -licui18n -licuuc -licudata \
  -lpthread \
  -o "${TEST}/bin/ru_tts"
echo "Built ${TEST}/bin/ru_tts"
