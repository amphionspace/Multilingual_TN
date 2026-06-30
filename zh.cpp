// TTS Chinese text normalization — rules in rules/zh.json, pinyin map in rules/zh_pinyin.json.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <unicode/locid.h>
#include <unicode/rbnf.h>
#include <unicode/unistr.h>

#include "tts_normalizer_engine.hpp"

namespace {

std::filesystem::path rulesDir() {
    return std::filesystem::path(__FILE__).parent_path() / "rules";
}

std::filesystem::path rulesV2Dir() {
    return std::filesystem::path(__FILE__).parent_path() / "rules_v2";
}

bool useRulesV2() {
    const char* raw = std::getenv("TTS_RULES_FORMAT");
    if (raw == nullptr) {
        return true;  // v2 (rules_v2/zh.full.json) is the default delivery format.
    }
    std::string v(raw);
    for (char& c : v) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    // Explicit legacy opt-out; anything else stays on v2.
    return !(v == "v1" || v == "rules_v1" || v == "legacy" || v == "rules");
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
        if (useRulesV2()) {
            if (!engine_.loadRulesV2((rulesV2Dir() / "zh.full.json").string(), err)) {
                std::cerr << "Failed to load TTS rules v2: " << err << std::endl;
                std::abort();
            }
        } else {
            if (!engine_.loadRules((rulesDir() / "zh.json").string(), err)) {
                std::cerr << "Failed to load TTS rules: " << err << std::endl;
                std::abort();
            }
        }
        if (!engine_.loadPinyinMap((rulesDir() / "zh_pinyin.json").string(), err)) {
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
