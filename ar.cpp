// TTS Arabic text normalization — rules live in rules_v2/ar.full.json.
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

    icu::UnicodeString numberToArabic(const icu::UnicodeString& numStr) {
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

    icu::UnicodeString numberToOrdinalArabic(const icu::UnicodeString& numStr, UErrorCode& status) {
        if (U_FAILURE(status)) {
            return numStr;
        }
        icu::RuleBasedNumberFormat formatter(icu::URBNF_SPELLOUT, icu::Locale("ar"), status);
        if (U_FAILURE(status)) {
            return numStr;
        }
        icu::UnicodeString ruleSet("%spellout-ordinal");
        double value = 0;
        std::string utf8;
        numStr.toUTF8String(utf8);
        value = std::atof(utf8.c_str());
        icu::UnicodeString result;
        icu::FieldPosition pos(icu::FieldPosition::DONT_CARE);
        formatter.format(value, ruleSet, result, pos, status);
        return result;
    }

public:
    TextNormalizer() {
        UErrorCode status = U_ZERO_ERROR;
        spelloutFmt = new icu::RuleBasedNumberFormat(icu::URBNF_SPELLOUT, icu::Locale("ar"), status);
        std::string err;
        if (!engine_.loadRulesV2((rulesV2Dir() / "ar.full.json").string(), err)) {
            std::cerr << "Failed to load TTS rules v2: " << err << std::endl;
            std::abort();
        }
        engine_.loadPinyinMap("", err);
    }

    ~TextNormalizer() { delete spelloutFmt; }

    std::string normalizeLine(const std::string& inputUtf8) {
        icu::UnicodeString text = icu::UnicodeString::fromUTF8(inputUtf8);
        TtsCallbacks cb;
        cb.spellout = [this](const icu::UnicodeString& n) { return numberToArabic(n); };
        cb.ordinal_spellout = [this](const icu::UnicodeString& n, UErrorCode& st) {
            return numberToOrdinalArabic(n, st);
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
