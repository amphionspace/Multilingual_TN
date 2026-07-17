// C API implementation. TTS_API is the ONLY default-visibility symbol set in the
// shared library: everything else (TN internals, ICU, MorphoDiTa) is compiled with
// -fvisibility=hidden so the linker can dead-strip unused ICU code.
#include "tts_capi.h"

#include <string>

#include "text_normalizer.hpp"

#define TTS_API __attribute__((visibility("default")))

namespace {
thread_local std::string g_err;
thread_local std::string g_out;
} // namespace

extern "C" {

TTS_API tts_normalizer_t* tts_init(const char* resource_dir) {
    if (resource_dir == nullptr) {
        g_err = "resource_dir is NULL";
        return nullptr;
    }
    auto* n = new TextNormalizer();
    if (!n->Init(resource_dir)) {
        g_err = n->error();
        delete n;
        return nullptr;
    }
    g_err.clear();
    return reinterpret_cast<tts_normalizer_t*>(n);
}

TTS_API const char* tts_normalize(tts_normalizer_t* handle, const char* line_utf8) {
    if (handle == nullptr || line_utf8 == nullptr) {
        return nullptr;
    }
    g_out = reinterpret_cast<TextNormalizer*>(handle)->normalizeLine(line_utf8);
    return g_out.c_str();
}

TTS_API const char* tts_last_error(void) {
    return g_err.empty() ? nullptr : g_err.c_str();
}

TTS_API void tts_free(tts_normalizer_t* handle) {
    delete reinterpret_cast<TextNormalizer*>(handle);
}

} // extern "C"
