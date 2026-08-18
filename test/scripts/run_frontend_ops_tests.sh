#!/bin/bash
# Focused tests for JSON-composable frontend punctuation operations.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ICU_ROOT="${ICU_ROOT:-}"
if [[ -z "${ICU_ROOT}" ]]; then
  for cand in /opt/homebrew/opt/icu4c /usr/local/opt/icu4c \
              $(ls -d /opt/homebrew/opt/icu4c@* /usr/local/opt/icu4c@* 2>/dev/null | sort -Vr || true); do
    if [[ -f "${cand}/include/unicode/locid.h" ]]; then
      ICU_ROOT="${cand}"
      break
    fi
  done
fi
if [[ -z "${ICU_ROOT}" ]]; then
  echo "Set ICU_ROOT to your icu4c prefix (e.g. /opt/homebrew/opt/icu4c@78)." >&2
  exit 1
fi

BUILD_DIR="$(mktemp -d)"
cleanup() { rm -rf "${BUILD_DIR}"; }
trap cleanup EXIT

g++ -std=c++17 -O2 \
  "${ROOT}/test/frontend_ops_test.cpp" \
  "${ROOT}/unified/frontend_ops.cpp" \
  "${ROOT}/unified/frontend_rules_merge.cpp" \
  "${ROOT}/tts_normalizer_engine.cpp" \
  "${ROOT}/ru_year_spellout.cpp" \
  -I"${ROOT}" -I"${ROOT}/third_party" -I"${ROOT}/unified" \
  -I"${ICU_ROOT}/include" \
  -L"${ICU_ROOT}/lib" -licui18n -licuuc -licudata \
  -o "${BUILD_DIR}/frontend_ops_test"

export DYLD_LIBRARY_PATH="${ICU_ROOT}/lib${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"
export LD_LIBRARY_PATH="${ICU_ROOT}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
"${BUILD_DIR}/frontend_ops_test" "${ROOT}"
