#pragma once

#include <string>
#include <vector>

#include <unicode/regex.h>
#include <unicode/unistr.h>
#include <unicode/utf16.h>

#include "third_party/nlohmann/json.hpp"
#include "tts_normalizer_engine.hpp"

namespace tts_zh_g2p_ops {

icu::UnicodeString opTone3Sandhi(const nlohmann::json& step,
                                 icu::RegexMatcher& m,
                                 UErrorCode& st,
                                 const TtsCallbacks& cb);

icu::UnicodeString opPyToneMarkToDigit(const nlohmann::json& step,
                                       icu::RegexMatcher& m,
                                       UErrorCode& st,
                                       const TtsCallbacks& cb);

} // namespace tts_zh_g2p_ops
