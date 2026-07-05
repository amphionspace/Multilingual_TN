#!/bin/bash
set -euo pipefail
TEST="$(cd "$(dirname "$0")/.." && pwd)"
cd "${TEST}"

if [[ ! -d "${TEST}/../rules_v2" ]]; then
  echo "Missing rules_v2 directory at ${TEST}/../rules_v2" >&2
  exit 1
fi

if [[ -n "${UPDATE_GOLDEN:-}" ]]; then
  echo "Refusing UPDATE_GOLDEN. Use guarded flow instead:" >&2
  echo "  UPDATE_LOCALES=ru CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF ./scripts/run_all.sh" >&2
  exit 2
fi

LOCALES=(en zh ar bn ru)
UPDATE_LOCALES_RAW="${UPDATE_LOCALES:-}"
UPDATE_CONFIRM_TOKEN="${CONFIRM_GOLDEN_UPDATE:-}"
AUTO_APPROVE_UPDATE="${AUTO_APPROVE_UPDATE:-0}"

normalize_locale_list() {
  echo "$1" | tr ',' ' ' | tr -s ' '
}

is_known_locale() {
  local x="$1"
  for l in "${LOCALES[@]}"; do
    if [[ "${l}" == "${x}" ]]; then
      return 0
    fi
  done
  return 1
}

is_update_target() {
  local x="$1"
  if [[ -z "${UPDATE_LOCALES_RAW}" ]]; then
    return 1
  fi
  local list
  list="$(normalize_locale_list "${UPDATE_LOCALES_RAW}")"
  for item in ${list}; do
    if [[ "${item}" == "all" || "${item}" == "${x}" ]]; then
      return 0
    fi
  done
  return 1
}

if [[ -n "${UPDATE_LOCALES_RAW}" ]]; then
  if [[ "${UPDATE_CONFIRM_TOKEN}" != "I_HAVE_REVIEWED_DIFF" ]]; then
    echo "Refusing golden update: set CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF after review." >&2
    exit 2
  fi
  for item in $(normalize_locale_list "${UPDATE_LOCALES_RAW}"); do
    if [[ "${item}" != "all" ]] && ! is_known_locale "${item}"; then
      echo "Unknown locale in UPDATE_LOCALES: ${item}" >&2
      exit 2
    fi
  done
fi

FAILED=0
RU_MODEL="${RU_MORPH_MODEL:-$(cd .. && pwd)/original/morphodita/models/russian-syntagrus-morphodita-only.tagger}"
for L in "${LOCALES[@]}"; do
  BIN="./bin/${L}_tts"
  IN="./in/${L}.txt"
  OUT="./out/${L}.out"
  GOLD="./expected/${L}.golden"
  if [[ ! -x "${BIN}" ]]; then
    echo "SKIP ${L}: ${BIN} not executable (run scripts/build.sh)" >&2
    FAILED=1
    continue
  fi
  mkdir -p out
  if [[ "${L}" == "ru" ]]; then
    if [[ ! -f "${RU_MODEL}" ]]; then
      echo "SKIP ru: MorphoDiTa model not found at ${RU_MODEL} (set RU_MORPH_MODEL=...)" >&2
      FAILED=1
      continue
    fi
    "${BIN}" --morph-model "${RU_MODEL}" < "${IN}" > "${OUT}"
  else
    "${BIN}" < "${IN}" > "${OUT}"
  fi

  if is_update_target "${L}"; then
    if [[ -f "${GOLD}" ]] && diff -u "${GOLD}" "${OUT}" > /tmp/tts_diff_"${L}".txt; then
      echo "UNCHANGED ${L}: no golden update needed"
      continue
    fi
    if [[ -f /tmp/tts_diff_"${L}".txt ]]; then
      echo "DIFF ${L}:"
      sed -n '1,120p' /tmp/tts_diff_"${L}".txt
    else
      echo "DIFF ${L}: new golden file ${GOLD}"
    fi
    if [[ "${AUTO_APPROVE_UPDATE}" != "1" ]]; then
      if [[ -t 0 ]]; then
        read -r -p "Approve golden update for ${L}? [y/N] " yn
        case "${yn}" in
          [Yy]|[Yy][Ee][Ss]) ;;
          *)
            echo "SKIP ${L}: update not approved"
            FAILED=1
            continue
            ;;
        esac
      else
        echo "Refusing non-interactive update for ${L}; set AUTO_APPROVE_UPDATE=1 if intentional." >&2
        FAILED=1
        continue
      fi
    fi
    cp "${OUT}" "${GOLD}"
    echo "UPDATED golden ${GOLD}"
    continue
  fi

  if [[ ! -f "${GOLD}" ]]; then
    echo "FAIL ${L}: missing ${GOLD} (use guarded update flow with UPDATE_LOCALES=${L})" >&2
    FAILED=1
    continue
  fi
  if ! diff -u "${GOLD}" "${OUT}"; then
    echo "FAIL ${L}: output differs from golden" >&2
    FAILED=1
  else
    echo "OK   ${L}"
  fi
done
exit "${FAILED}"
