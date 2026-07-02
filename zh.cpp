// TTS Chinese text normalization — rules in rules_v2/zh.full.json, pinyin map in rules_v2/zh_pinyin.json.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <unicode/locid.h>
#include <unicode/rbnf.h>
#include <unicode/unistr.h>

#include "tts_normalizer_engine.hpp"

namespace {

std::filesystem::path rulesV2Dir() {
    return std::filesystem::path(__FILE__).parent_path() / "rules_v2";
}

} // namespace

class TextNormalizer {
private:
    icu::RuleBasedNumberFormat* spelloutFmt = nullptr;
    TtsNormalizerEngine engine_;

    icu::UnicodeString numberToChinese(const icu::UnicodeString& numStr) {
        UErrorCode status = U_ZERO_ERROR;
        std::string utf8Num;
        numStr.toUTF8String(utf8Num);
        try {
            double val = std::stod(utf8Num);
            icu::UnicodeString result;
            spelloutFmt->format(val, result, status);
            return result;
        } catch (...) {
            return numStr;
        }
    }

public:
    TextNormalizer() {
        UErrorCode status = U_ZERO_ERROR;
        spelloutFmt = new icu::RuleBasedNumberFormat(icu::URBNF_SPELLOUT, icu::Locale::getChinese(), status);
        std::string err;
        if (!engine_.loadRulesV2((rulesV2Dir() / "zh.full.json").string(), err)) {
            std::cerr << "Failed to load TTS rules v2: " << err << std::endl;
            std::abort();
        }
        if (!engine_.loadPinyinMap((rulesV2Dir() / "zh_pinyin.json").string(), err)) {
            std::cerr << "Failed to load pinyin map: " << err << std::endl;
            std::abort();
        }
    }

    ~TextNormalizer() { delete spelloutFmt; }

    std::string normalizeLine(const std::string& inputUtf8) {
        icu::UnicodeString text = icu::UnicodeString::fromUTF8(inputUtf8);
        TtsCallbacks cb;
        cb.spellout = [this](const icu::UnicodeString& n) { return numberToChinese(n); };
        cb.ordinal_spellout = [this](const icu::UnicodeString& n, UErrorCode& st) {
            (void)st;
            return numberToChinese(n);
        };
        text = engine_.runPipeline(text, cb);
        std::string out;
        text.toUTF8String(out);
        return out;
    }
};

int main() {
    TextNormalizer normalizer;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            std::cout << std::endl;
            continue;
        }
        std::cout << normalizer.normalizeLine(line) << std::endl;
    }
    return 0;
}
