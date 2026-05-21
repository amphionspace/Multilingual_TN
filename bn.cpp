// TTS Bengali text normalization — rules live in rules/bn.json (regenerate via tools/generate_tts_rules_json.py).
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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
        return false;
    }
    std::string v(raw);
    for (char& c : v) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return v == "v2" || v == "rules_v2";
}

} // namespace

class TextNormalizer {
private:
    icu::RuleBasedNumberFormat* spelloutFmt = nullptr;
    TtsNormalizerEngine engine_;

    icu::UnicodeString numberToBengali(const icu::UnicodeString& numStr) {
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

    icu::UnicodeString numberToOrdinalBengali(const icu::UnicodeString& numStr, UErrorCode& status) {
        if (U_FAILURE(status)) {
            return numStr;
        }
        std::string utf8;
        numStr.toUTF8String(utf8);
        try {
            int val = std::stoi(utf8);
            const std::vector<std::string> ordinals = {
                "",       "প্রথম", "দ্বিতীয়", "তৃতীয়", "চতুর্থ", "পঞ্চম",
                "ষষ্ঠ", "সপ্তম", "অষ্টম", "নবম", "দশম"};
            if (val > 0 && val <= 10) {
                return icu::UnicodeString::fromUTF8(ordinals[static_cast<size_t>(val)]);
            }
            icu::UnicodeString cardinal = numberToBengali(numStr);
            return cardinal + icu::UnicodeString::fromUTF8("তম");
        } catch (...) {
            return numStr;
        }
    }

public:
    TextNormalizer() {
        UErrorCode status = U_ZERO_ERROR;
        spelloutFmt = new icu::RuleBasedNumberFormat(icu::URBNF_SPELLOUT, icu::Locale("bn"), status);
        std::string err;
        if (useRulesV2()) {
            if (!engine_.loadRulesV2((rulesV2Dir() / "bn.full.json").string(), err)) {
                std::cerr << "Failed to load TTS rules v2: " << err << std::endl;
                std::abort();
            }
        } else {
            if (!engine_.loadRules((rulesDir() / "bn.json").string(), err)) {
                std::cerr << "Failed to load TTS rules: " << err << std::endl;
                std::abort();
            }
        }
        engine_.loadPinyinMap("", err);
    }

    ~TextNormalizer() { delete spelloutFmt; }

    std::string normalizeLine(const std::string& inputUtf8) {
        icu::UnicodeString text = icu::UnicodeString::fromUTF8(inputUtf8);
        TtsCallbacks cb;
        cb.spellout = [this](const icu::UnicodeString& n) { return numberToBengali(n); };
        cb.ordinal_spellout = [this](const icu::UnicodeString& n, UErrorCode& st) {
            return numberToOrdinalBengali(n, st);
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
