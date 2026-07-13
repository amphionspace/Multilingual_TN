#!/bin/bash
# Reproduce the "zero regression" check for the unified-architecture merge.
#
# Builds BOTH sides and diffs their normalization output byte-for-byte:
#   - OLD architecture at ORACLE_REF (default origin/main): per-language binaries
#   - NEW architecture on the current checkout: the unified tts_cli
# Any difference => regression. Identical => the refactor changed no output.
#
# Why compare against origin/main (not a stored golden): main advanced with new
# rules/features after the refactor branched, so origin/main IS the correct oracle.
#
# Env:
#   ICU_ROOT               icu4c prefix (auto-detected if unset)
#   ORACLE_REF             git ref for the old architecture (default: origin/main)
#   RU_MORPH_MODEL         MorphoDiTa .tagger (default: repo model path)
#   TRANSSION_TESTSET_DIR  dir with <lang>_1000_sample_sent.txt (optional; skipped if unset)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST="${ROOT}/test"
ORACLE_REF="${ORACLE_REF:-origin/main}"
RU_MODEL="${RU_MORPH_MODEL:-${ROOT}/original/morphodita/models/russian-syntagrus-morphodita-only.tagger}"
TS="${TRANSSION_TESTSET_DIR:-}"

ICU_ROOT="${ICU_ROOT:-}"
if [[ -z "${ICU_ROOT}" ]]; then
  for c in /opt/homebrew/opt/icu4c /usr/local/opt/icu4c; do
    [[ -f "${c}/include/unicode/locid.h" ]] && ICU_ROOT="${c}" && break
  done
fi
[[ -z "${ICU_ROOT}" ]] && { echo "Set ICU_ROOT to your icu4c prefix." >&2; exit 1; }
export ICU_ROOT
ICUFLAGS="-I${ICU_ROOT}/include -L${ICU_ROOT}/lib -licui18n -licuuc -licudata"

WTBASE="$(mktemp -d)"; WT="${WTBASE}/oracle"; TMP="$(mktemp -d)"
cleanup() { git -C "${ROOT}" worktree remove --force "${WT}" >/dev/null 2>&1; rm -rf "${WTBASE}" "${TMP}"; }
trap cleanup EXIT

echo "== building OLD architecture oracle (${ORACLE_REF}) =="
git -C "${ROOT}" worktree add -q --detach "${WT}" "${ORACLE_REF}" || { echo "worktree add failed (fetch ${ORACLE_REF}?)"; exit 2; }
mkdir -p "${WT}/bin"
for s in en zh ar bn; do
  g++ -std=c++17 -O2 "${WT}/${s}.cpp" "${WT}/tts_normalizer_engine.cpp" "${WT}/ru_year_spellout.cpp" \
    -I"${WT}" -I"${WT}/third_party" ${ICUFLAGS} -o "${WT}/bin/${s}_tts" \
    || { echo "oracle build ${s} failed"; exit 2; }
done
g++ -std=c++17 -O2 "${WT}/ru.cpp" "${WT}/tts_normalizer_engine.cpp" "${WT}/ru_year_spellout.cpp" \
  "${WT}/original/morphodita/src_lib_only/morphodita.cpp" \
  -I"${WT}" -I"${WT}/third_party" -I"${WT}/original/morphodita/src_lib_only" ${ICUFLAGS} -lpthread \
  -o "${WT}/bin/ru_tts" || { echo "oracle ru build failed"; exit 2; }

echo "== building NEW architecture (this checkout, unified) =="
bash "${TEST}/scripts/build_unified.sh" >/dev/null || { echo "unified build failed"; exit 2; }
NEW="${TEST}/bin/tts_cli"
export DYLD_LIBRARY_PATH="${ICU_ROOT}/lib"

run_old() { # <lang> <in> <out>
  if [[ "$1" == "ru" ]]; then "${WT}/bin/ru_tts" --morph-model "${RU_MODEL}" < "$2" > "$3" 2>/dev/null
  else "${WT}/bin/${1}_tts" < "$2" > "$3" 2>/dev/null; fi
}
run_new() { "${NEW}" --data "${ROOT}/data/$1" < "$2" > "$3" 2>/dev/null; }

PASS=0; FAIL=0; SKIP=0
check() { # <label> <lang> <in>
  if [[ ! -f "$3" ]]; then echo "  SKIP ${1} ${2} (no input: $3)"; SKIP=$((SKIP+1)); return; fi
  if [[ "$2" == "ru" && ! -f "${RU_MODEL}" ]]; then echo "  SKIP ${1} ru (no model: ${RU_MODEL})"; SKIP=$((SKIP+1)); return; fi
  run_old "$2" "$3" "${TMP}/o"; run_new "$2" "$3" "${TMP}/n"
  if diff -q "${TMP}/o" "${TMP}/n" >/dev/null; then
    echo "  OK   ${1} ${2}"; PASS=$((PASS+1))
  else
    echo "  FAIL ${1} ${2}"; diff "${TMP}/o" "${TMP}/n" | head -8; FAIL=$((FAIL+1))
  fi
}

echo "== smoke (test/in) =="
for L in en zh ar bn ru; do check smoke "$L" "${TEST}/in/${L}.txt"; done
echo "== transsion_cmos =="
for L in en zh ru; do check cmos "$L" "${TEST}/transsion_cmos/${L}_cmos.txt"; done
echo "== transsion 1000-sample (set TRANSSION_TESTSET_DIR to enable) =="
for L in en zh ar ru bn; do check 1000 "$L" "${TS:+${TS}/${L}_1000_sample_sent.txt}"; done

echo "== ${PASS} passed, ${FAIL} failed, ${SKIP} skipped =="
if [[ ${FAIL} -eq 0 ]]; then echo ">>> NEW architecture == ${ORACLE_REF} (byte-identical, no regression)"; else echo ">>> REGRESSION DETECTED"; fi
exit "${FAIL}"
