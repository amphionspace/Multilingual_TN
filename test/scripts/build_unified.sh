#!/bin/bash
# Build the unified single-binary TTS normalizer (Phase 0/1).
# One binary drives every language; the language is chosen at runtime via --data <dir>.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST="$(cd "$(dirname "$0")/.." && pwd)"

ICU_ROOT="${ICU_ROOT:-}"
if [[ -z "${ICU_ROOT}" ]]; then
  # Unversioned prefixes first, then homebrew's versioned kegs (icu4c@78, ...) newest first:
  # homebrew now installs icu4c under a versioned name and leaves no unversioned symlink.
  for cand in /opt/homebrew/opt/icu4c /usr/local/opt/icu4c \
              $(ls -d /opt/homebrew/opt/icu4c@* /usr/local/opt/icu4c@* 2>/dev/null | sort -Vr || true); do
    if [[ -f "${cand}/include/unicode/locid.h" ]]; then ICU_ROOT="${cand}"; break; fi
  done
fi
if [[ -z "${ICU_ROOT}" ]]; then
  echo "Set ICU_ROOT to your icu4c prefix (e.g. /opt/homebrew/opt/icu4c@78)." >&2
  exit 1
fi

MORPH_INC="${ROOT}/original/morphodita/src_lib_only"
MORPH_CPP="${MORPH_INC}/morphodita.cpp"
if [[ ! -f "${MORPH_CPP}" ]]; then
  bash "${TEST}/scripts/prepare_morphodita.sh"
fi

mkdir -p "${TEST}/bin"
g++ -std=c++17 -O2 \
  "${ROOT}/unified/tts_cli.cpp" \
  "${ROOT}/unified/text_normalizer.cpp" \
  "${ROOT}/unified/frontend_ops.cpp" \
  "${ROOT}/unified/frontend_rules_merge.cpp" \
  "${ROOT}/unified/backend.cpp" \
  "${ROOT}/unified/icu_backend.cpp" \
  "${ROOT}/unified/english_backend.cpp" \
  "${ROOT}/unified/arabic_backend.cpp" \
  "${ROOT}/unified/chinese_backend.cpp" \
  "${ROOT}/unified/chinese_g2p_backend.cpp" \
  "${ROOT}/unified/chinese_g2p_ops.cpp" \
  "${ROOT}/unified/en_zh_g2p_backend.cpp" \
  "${ROOT}/unified/bengali_backend.cpp" \
  "${ROOT}/unified/russian_backend.cpp" \
  "${ROOT}/tts_normalizer_engine.cpp" \
  "${ROOT}/ru_year_spellout.cpp" \
  "${MORPH_CPP}" \
  -I"${ROOT}" -I"${ROOT}/third_party" -I"${ROOT}/unified" -I"${MORPH_INC}" \
  -I"${ICU_ROOT}/include" \
  -L"${ICU_ROOT}/lib" -licui18n -licuuc -licudata -lpthread \
  -o "${TEST}/bin/tts_cli"
echo "Built ${TEST}/bin/tts_cli"
echo "Run: ${TEST}/bin/tts_cli --data ${ROOT}/data/ru < input.txt"
