#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MORPH_ROOT="${ROOT}/original/morphodita"
MORPH_BUILD="${MORPH_ROOT}/build"
MORPH_SRC="${MORPH_BUILD}/_deps/morphodita-src"
SRC_LIB_ONLY_GEN="${MORPH_SRC}/src_lib_only"
SRC_LIB_ONLY_VENDOR="${MORPH_ROOT}/src_lib_only"
MORPH_CPP="${SRC_LIB_ONLY_VENDOR}/morphodita.cpp"
MORPH_H="${SRC_LIB_ONLY_VENDOR}/morphodita.h"

if [[ ! -d "${MORPH_ROOT}" ]]; then
  echo "Missing directory: ${MORPH_ROOT}" >&2
  exit 1
fi

if [[ ! -d "${MORPH_SRC}" ]]; then
  echo "[morphodita] Bootstrapping source into ${MORPH_SRC} ..."
  mkdir -p "${MORPH_BUILD}/_deps"
  git clone --depth 1 https://github.com/ufal/morphodita.git "${MORPH_SRC}"
fi

if [[ ! -d "${SRC_LIB_ONLY_GEN}" ]]; then
  echo "MorphoDiTa source layout is unexpected. Missing: ${SRC_LIB_ONLY_GEN}" >&2
  exit 1
fi

if [[ ! -f "${SRC_LIB_ONLY_GEN}/morphodita.cpp" || ! -f "${SRC_LIB_ONLY_GEN}/morphodita.h" ]]; then
  echo "[morphodita] Generating src_lib_only artifacts ..."
  (cd "${SRC_LIB_ONLY_GEN}" && make morphodita.cpp)
fi

mkdir -p "${SRC_LIB_ONLY_VENDOR}"
if [[ ! -f "${MORPH_CPP}" || ! -f "${MORPH_H}" ]]; then
  cp "${SRC_LIB_ONLY_GEN}/morphodita.cpp" "${SRC_LIB_ONLY_GEN}/morphodita.h" "${SRC_LIB_ONLY_VENDOR}/"
fi

if [[ ! -f "${MORPH_CPP}" || ! -f "${MORPH_H}" ]]; then
  echo "MorphoDiTa bootstrap failed. Missing morphodita.cpp or morphodita.h." >&2
  exit 1
fi

echo "[morphodita] Ready: ${SRC_LIB_ONLY_VENDOR}"
