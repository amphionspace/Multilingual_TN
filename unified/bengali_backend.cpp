// Bengali backend: ICU has no usable bn spellout, so number-to-words is hand-written
// (Indian lakh/crore system). Ported verbatim from bn.cpp so output is byte-identical.
#include <string>
#include <vector>

#include <unicode/unistr.h>

#include "backend.hpp"

namespace {

class BengaliBackend : public LanguageBackend {
public:
    bool configure(const nlohmann::json& /*cfg*/, const std::string& /*resDir*/, std::string& /*err*/) override {
        cb_.spellout = [this](const icu::UnicodeString& n) { return numberToBengali(n); };
        cb_.ordinal_spellout = [this](const icu::UnicodeString& n, UErrorCode& st) {
            return numberToOrdinalBengali(n, st);
        };
        return true;
    }

    const TtsCallbacks& callbacks() const override { return cb_; }

private:
    std::string spellIntBn(long long n) {
        static const std::vector<std::string> k0to99 = {
            "শূন্য",   "এক",       "দুই",      "তিন",        "চার",      "পাঁচ",      "ছয়",       "সাত",      "আট",        "নয়",
            "দশ",      "এগারো",    "বারো",     "তেরো",       "চৌদ্দ",    "পনেরো",     "ষোলো",      "সতেরো",    "আঠারো",     "উনিশ",
            "বিশ",     "একুশ",     "বাইশ",     "তেইশ",       "চব্বিশ",   "পঁচিশ",     "ছাব্বিশ",    "সাতাশ",    "আটাশ",      "ঊনত্রিশ",
            "ত্রিশ",    "একত্রিশ",   "বত্রিশ",    "তেত্রিশ",     "চৌত্রিশ",  "পঁয়ত্রিশ",  "ছত্রিশ",     "সাঁইত্রিশ", "আটত্রিশ",    "ঊনচল্লিশ",
            "চল্লিশ",   "একচল্লিশ",  "বিয়াল্লিশ", "তেতাল্লিশ",   "চুয়াল্লিশ", "পঁয়তাল্লিশ", "ছেচল্লিশ",   "সাতচল্লিশ", "আটচল্লিশ",   "ঊনপঞ্চাশ",
            "পঞ্চাশ",   "একান্ন",    "বাহান্ন",   "তিপ্পান্ন",    "চুয়ান্ন",  "পঞ্চান্ন",   "ছাপ্পান্ন",   "সাতান্ন",   "আটান্ন",     "ঊনষাট",
            "ষাট",      "একষট্টি",   "বাষট্টি",   "তেষট্টি",     "চৌষট্টি",  "পঁয়ষট্টি",  "ছেষট্টি",    "সাতষট্টি", "আটষট্টি",    "ঊনসত্তর",
            "সত্তর",    "একাত্তর",   "বাহাত্তর",  "তিয়াত্তর",    "চুয়াত্তর", "পঁচাত্তর",   "ছিয়াত্তর",   "সাতাত্তর", "আটাত্তর",    "ঊনআশি",
            "আশি",      "একাশি",     "বিরাশি",   "তিরাশি",      "চুরাশি",   "পঁচাশি",    "ছিয়াশি",    "সাতাশি",   "আটাশি",      "ঊননব্বই",
            "নব্বই",     "একানব্বই",  "বিরানব্বই", "তিরানব্বই",   "চুরানব্বই", "পঁচানব্বই",  "ছিয়ানব্বই",  "সাতানব্বই", "আটানব্বই",   "নিরানব্বই"};

        if (n < 100) {
            return k0to99[static_cast<size_t>(n)];
        }
        if (n < 1000) {
            const int h = static_cast<int>(n / 100);
            const int r = static_cast<int>(n % 100);
            std::string out = (h == 1) ? "একশ" : (k0to99[static_cast<size_t>(h)] + "শ");
            if (r != 0) {
                out += " " + k0to99[static_cast<size_t>(r)];
            }
            return out;
        }
        if (n < 100000) {
            const long long th = n / 1000;
            const int r = static_cast<int>(n % 1000);
            std::string out = spellIntBn(th) + " হাজার";
            if (r != 0) {
                out += " " + spellIntBn(r);
            }
            return out;
        }
        if (n < 10000000) {
            const long long lakh = n / 100000;
            const int r = static_cast<int>(n % 100000);
            std::string out = spellIntBn(lakh) + " লাখ";
            if (r != 0) {
                out += " " + spellIntBn(r);
            }
            return out;
        }
        const long long crore = n / 10000000;
        const long long r = n % 10000000;
        std::string out = spellIntBn(crore) + " কোটি";
        if (r != 0) {
            out += " " + spellIntBn(r);
        }
        return out;
    }

    icu::UnicodeString numberToBengali(const icu::UnicodeString& numStr) {
        if (numStr.isEmpty()) {
            return numStr;
        }
        bool isNeg = false;
        std::string cleaned;
        cleaned.reserve(static_cast<size_t>(numStr.length()));
        for (int i = 0; i < numStr.length(); ++i) {
            const UChar c = numStr.charAt(i);
            if (i == 0 && c == '-') {
                isNeg = true;
                continue;
            }
            if (c >= '0' && c <= '9') {
                cleaned.push_back(static_cast<char>(c));
                continue;
            }
            if (c >= 0x09E6 && c <= 0x09EF) {
                cleaned.push_back(static_cast<char>('0' + (c - 0x09E6)));
                continue;
            }
            if (c == '.') {
                cleaned.push_back('.');
            }
        }
        if (cleaned.empty()) {
            return numStr;
        }

        auto digitWord = [](char d) -> const char* {
            switch (d) {
                case '0': return "শূন্য";
                case '1': return "এক";
                case '2': return "দুই";
                case '3': return "তিন";
                case '4': return "চার";
                case '5': return "পাঁচ";
                case '6': return "ছয়";
                case '7': return "সাত";
                case '8': return "আট";
                case '9': return "নয়";
                default: return "";
            }
        };

        const size_t dotPos = cleaned.find('.');
        std::string out;
        if (dotPos == std::string::npos) {
            try {
                long long n = std::stoll(cleaned);
                out = spellIntBn(n);
            } catch (...) {
                return numStr;
            }
        } else {
            std::string intPart = cleaned.substr(0, dotPos);
            std::string fracPart = cleaned.substr(dotPos + 1);
            if (intPart.empty()) {
                intPart = "0";
            }
            try {
                long long n = std::stoll(intPart);
                out = spellIntBn(n) + " দশমিক";
            } catch (...) {
                return numStr;
            }
            for (char d : fracPart) {
                if (d >= '0' && d <= '9') {
                    out += " ";
                    out += digitWord(d);
                }
            }
        }
        if (isNeg) {
            out = std::string("ঋণাত্মক ") + out;
        }
        return icu::UnicodeString::fromUTF8(out);
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

    TtsCallbacks cb_;
};

BackendAutoRegister _reg_bn("bengali", [] { return std::unique_ptr<LanguageBackend>(new BengaliBackend()); });

} // namespace
