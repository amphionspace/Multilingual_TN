// English backend ("english"): ICU spellout + the English-only grp modes that Phase 2
// pulled out of the engine — year prose ("nineteen forty-one") and full month naming.
#include <string>

#include <unicode/unistr.h>

#include "icu_backend.hpp"

namespace {

// English month token (full name or common abbreviation) -> 1..12; else 0.
// Ported verbatim from the engine's parseEnglishMonthTokenForEn().
int parseEnglishMonthTokenForEn(const icu::UnicodeString& part) {
    icu::UnicodeString t = part;
    t.trim();
    std::string s;
    t.toUTF8String(s);
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + 32);
        }
    }
    while (!s.empty() && (s.back() == '.' || s.back() == ',')) {
        s.pop_back();
    }
    if (s.empty()) return 0;
    if (s == "january" || s == "jan") return 1;
    if (s == "february" || s == "feb") return 2;
    if (s == "march" || s == "mar") return 3;
    if (s == "april" || s == "apr") return 4;
    if (s == "may") return 5;
    if (s == "june" || s == "jun") return 6;
    if (s == "july" || s == "jul") return 7;
    if (s == "august" || s == "aug") return 8;
    if (s == "september" || s == "sept" || s == "sep") return 9;
    if (s == "october" || s == "oct") return 10;
    if (s == "november" || s == "nov") return 11;
    if (s == "december" || s == "dec") return 12;
    return 0;
}

// en.full.json defines no month_names, so the engine used these English defaults.
const char* kEnMonths[] = {"",     "January", "February", "March",    "April",   "May",     "June",
                           "July", "August",  "September", "October", "November", "December"};

class EnglishBackend : public IcuSpelloutBackend {
protected:
    bool configureLang(const nlohmann::json& /*cfg*/, const std::string& /*resDir*/, std::string& /*err*/) override {
        // year prose: mirrors engine spelloutEnglishYearProse (uses this backend's spellout).
        cb_.year_prose = [this](int year) -> icu::UnicodeString {
            if (year < 1200 || year > 1999) {
                return spelloutCardinal(icu::UnicodeString::fromUTF8(std::to_string(year)));
            }
            const int lo = year % 100;
            if (lo == 0) {
                return spelloutCardinal(icu::UnicodeString::fromUTF8(std::to_string(year)));
            }
            const int hi = year / 100;
            std::string loStr = (lo < 10) ? (std::string("0") + std::to_string(lo)) : std::to_string(lo);
            return spelloutCardinal(icu::UnicodeString::fromUTF8(std::to_string(hi))) + " " +
                   spelloutCardinal(icu::UnicodeString::fromUTF8(loStr));
        };
        // month_en_full: token -> full name; non-month tokens return unchanged (engine parity).
        cb_.month_en_full = [](const icu::UnicodeString& token) -> icu::UnicodeString {
            const int mon = parseEnglishMonthTokenForEn(token);
            if (mon < 1 || mon > 12) {
                return token;
            }
            return icu::UnicodeString::fromUTF8(kEnMonths[mon]);
        };
        return true;
    }
};

BackendAutoRegister _reg_en("english", [] { return std::unique_ptr<LanguageBackend>(new EnglishBackend()); });

} // namespace
