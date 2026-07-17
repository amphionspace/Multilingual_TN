#!/bin/bash
# One-click build of libtts_normalizer.so (self-contained, size-trimmed).
#
#   bash test/scripts/build_tts_so.sh
#
# What it does:
#   1. Fetches the pinned ICU source (once, cached) — or uses ICU_SRC if you supply one.
#   2. Builds ICU *static* with the data filter -> only the ICU data we actually use.
#   3. Links TN + backends + MorphoDiTa + trimmed ICU into ONE shared library that
#      exports only the C API (unified/tts_capi.h), with dead-code elimination + strip.
#
# Customizing the trim (e.g. when adding a language):
#   edit docs/icu_size_reduction/filter.json  ->  localeFilter.includelist
#   then re-run this script. The ICU build is cached and keyed on the filter's
#   content hash, so changing the filter automatically triggers a rebuild.
#
# Env knobs:
#   FILTER      data filter json     (default: docs/icu_size_reduction/filter.json)
#   ICU_SRC     existing icu4c source dir containing source/runConfigureICU
#               (default: downloads the pinned version into BUILD_DIR)
#   BUILD_DIR   scratch dir          (default: <repo>/build)
#   OUT         output library path  (default: BUILD_DIR/libtts_normalizer.{so,dylib})
#   JOBS        parallel make jobs   (default: CPU count)
#
# Cross-compiling (Android/embedded): ICU needs a host build first, then a target
# build with --host=... and --with-cross-build=<host build dir>. Point ICU_SRC at
# your source tree and adapt the runConfigureICU line below; the filter and the
# link step are unchanged. The DATA size is architecture-independent.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FILTER="${FILTER:-${ROOT}/docs/icu_size_reduction/filter.json}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"
JOBS="${JOBS:-$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4 )}"

ICU_TAG="release-78.3"          # pinned: behavior is frozen to this ICU version
ICU_URL="https://github.com/unicode-org/icu/archive/refs/tags/${ICU_TAG}.tar.gz"

case "$(uname -s)" in
  Darwin) SHARED_FLAG="-dynamiclib"; LIBEXT="dylib"; GC_FLAG="-Wl,-dead_strip"; ICU_PLATFORM="MacOSX"; STRIP_CMD=(strip -x) ;;
  *)      SHARED_FLAG="-shared";     LIBEXT="so";    GC_FLAG="-Wl,--gc-sections"; ICU_PLATFORM="Linux"; STRIP_CMD=(strip --strip-unneeded) ;;
esac
OUT="${OUT:-${BUILD_DIR}/libtts_normalizer.${LIBEXT}}"

[[ -f "${FILTER}" ]] || { echo "filter not found: ${FILTER}" >&2; exit 1; }
mkdir -p "${BUILD_DIR}"

# Export ONLY the C API. -fvisibility=hidden covers our own sources but not the
# prebuilt static ICU archives, whose symbols would otherwise be exported — that
# both defeats dead-stripping and risks clashing with a platform ICU at load time.
# A linker-level allowlist makes every non-tts_* symbol local.
if [[ "${LIBEXT}" == "dylib" ]]; then
  printf '_tts_*\n' > "${BUILD_DIR}/export.sym"
  EXPORT_FLAG="-Wl,-exported_symbols_list,${BUILD_DIR}/export.sym"
else
  printf '{ global: tts_*; local: *; };\n' > "${BUILD_DIR}/export.map"
  EXPORT_FLAG="-Wl,--version-script=${BUILD_DIR}/export.map"
fi

# ---- 1. ICU source ----------------------------------------------------------
if [[ -n "${ICU_SRC:-}" ]]; then
  ICU_ROOT_SRC="${ICU_SRC}"
else
  ICU_ROOT_SRC="${BUILD_DIR}/icu-${ICU_TAG}/icu4c"
  if [[ ! -f "${ICU_ROOT_SRC}/source/runConfigureICU" ]]; then
    echo "==> fetching ICU ${ICU_TAG} (once; cached in ${BUILD_DIR})"
    curl -sL --fail -o "${BUILD_DIR}/icu-src.tar.gz" "${ICU_URL}" \
      || { echo "download failed; set ICU_SRC=<icu4c source dir> instead" >&2; exit 1; }
    tar -xzf "${BUILD_DIR}/icu-src.tar.gz" -C "${BUILD_DIR}"
    mv "${BUILD_DIR}/icu-${ICU_TAG}" "${BUILD_DIR}/icu-${ICU_TAG}.tmp" 2>/dev/null || true
    [[ -d "${BUILD_DIR}/icu-${ICU_TAG}.tmp" ]] && mv "${BUILD_DIR}/icu-${ICU_TAG}.tmp" "${BUILD_DIR}/icu-${ICU_TAG}"
    rm -f "${BUILD_DIR}/icu-src.tar.gz"
  fi
fi
[[ -f "${ICU_ROOT_SRC}/source/runConfigureICU" ]] || { echo "bad ICU source: ${ICU_ROOT_SRC}" >&2; exit 1; }

# ---- 2. ICU static build with the data filter (cached on filter hash) --------
ICU_BUILD="${BUILD_DIR}/icu-static"
FILTER_HASH="$( (shasum -a 256 "${FILTER}" 2>/dev/null || sha256sum "${FILTER}") | cut -d' ' -f1)"
STAMP="${ICU_BUILD}/.filter.stamp"
if [[ ! -f "${ICU_BUILD}/lib/libicudata.a" ]] || [[ "$(cat "${STAMP}" 2>/dev/null || echo none)" != "${FILTER_HASH}" ]]; then
  echo "==> building ICU (static, filtered by $(basename "${FILTER}")) — this takes a few minutes"
  rm -rf "${ICU_BUILD}"; mkdir -p "${ICU_BUILD}"
  ( cd "${ICU_BUILD}" && \
    ICU_DATA_FILTER_FILE="${FILTER}" "${ICU_ROOT_SRC}/source/runConfigureICU" "${ICU_PLATFORM}" \
      --enable-static --disable-shared --with-data-packaging=static > configure.log 2>&1 && \
    make -j"${JOBS}" > make.log 2>&1 ) \
    || { echo "ICU build failed; see ${ICU_BUILD}/{configure,make}.log" >&2; exit 1; }
  echo "${FILTER_HASH}" > "${STAMP}"
else
  echo "==> reusing cached ICU build (filter unchanged): ${ICU_BUILD}"
fi

DAT="$(ls "${ICU_BUILD}"/data/out/tmp/icudt*.dat 2>/dev/null | head -1 || true)"

# ---- 3. link the shared library ---------------------------------------------
echo "==> linking ${OUT}"
MORPH_SRC="${ROOT}/original/morphodita/src_lib_only/morphodita.cpp"
[[ -f "${MORPH_SRC}" ]] || bash "${ROOT}/test/scripts/prepare_morphodita.sh"

g++ -std=c++17 -O2 ${SHARED_FLAG} -fPIC \
  -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections \
  "${ROOT}/unified/tts_capi.cpp" \
  "${ROOT}/unified/text_normalizer.cpp" \
  "${ROOT}/unified/backend.cpp" \
  "${ROOT}/unified/icu_backend.cpp" \
  "${ROOT}/unified/english_backend.cpp" \
  "${ROOT}/unified/arabic_backend.cpp" \
  "${ROOT}/unified/chinese_backend.cpp" \
  "${ROOT}/unified/bengali_backend.cpp" \
  "${ROOT}/unified/russian_backend.cpp" \
  "${ROOT}/tts_normalizer_engine.cpp" \
  "${ROOT}/ru_year_spellout.cpp" \
  "${MORPH_SRC}" \
  -I"${ROOT}" -I"${ROOT}/third_party" -I"${ROOT}/unified" \
  -I"${ROOT}/original/morphodita/src_lib_only" \
  -I"${ICU_ROOT_SRC}/source/common" -I"${ICU_ROOT_SRC}/source/i18n" \
  "${ICU_BUILD}/lib/libicui18n.a" "${ICU_BUILD}/lib/libicuuc.a" "${ICU_BUILD}/lib/libicudata.a" \
  -lpthread ${GC_FLAG} ${EXPORT_FLAG} \
  -o "${OUT}"
"${STRIP_CMD[@]}" "${OUT}" 2>/dev/null || true

# ---- 4. report ---------------------------------------------------------------
size_mb() { awk 'BEGIN{printf "%.2f MB", '"$(wc -c < "$1" | tr -d ' ')"'/1048576}'; }
echo
echo "==> built: ${OUT}  ($(size_mb "${OUT}"))"
[[ -n "${DAT}" ]] && echo "    trimmed ICU data: $(size_mb "${DAT}")  (filter: $(basename "${FILTER}"))"
echo "    exported symbols:"
if command -v nm >/dev/null; then
  nm -gU "${OUT}" 2>/dev/null | grep " T " | awk '{print "      "$3}' \
    || nm -D --defined-only "${OUT}" 2>/dev/null | grep " T " | awk '{print "      "$3}' || true
fi
echo
echo "Use it:  #include \"unified/tts_capi.h\"; link ${OUT}"
echo "         h = tts_init(\"data/ru\"); tts_normalize(h, line); tts_free(h);"
echo "Add a language: edit ${FILTER} (localeFilter.includelist) + add data/<lang>/, re-run this script."
