// Arabic backend ("arabic"): ICU spellout + Arabic morphology ops (ar_num_morph,
// ar_date_day) that Phase 2 moved out of the engine. The two large helper functions
// below were lifted verbatim from the engine to preserve output byte-for-byte.
#include <string>
#include <utility>

#include <unicode/regex.h>
#include <unicode/unistr.h>

#include "backend.hpp"
#include "icu_backend.hpp"
#include "op_helpers.hpp"

namespace {

using namespace opu; // trimAsciiSpaces, replaceAllInPlace, parseIntUStr, jgetS

// ==== BEGIN verbatim from tts_normalizer_engine.cpp (Arabic helpers) ====
std::string applyArabicNumberMorph(std::string spokenUtf8,
                                   const std::string& kase,
                                   bool definiteArticle,
                                   const std::string& nounGender,
                                   bool tamyiiz) {
    spokenUtf8 = trimAsciiSpaces(spokenUtf8);
    // Remove grouping separators before spellout post-morph processing.
    replaceAllInPlace(spokenUtf8, ",", "");
    replaceAllInPlace(spokenUtf8, "،", "");
    replaceAllInPlace(spokenUtf8, "٬", "");
    // Normalize ICU spelling variants for 2-family lexemes.
    replaceAllInPlace(spokenUtf8, "إثنان", "اثنان");
    replaceAllInPlace(spokenUtf8, "إثنين", "اثنين");
    replaceAllInPlace(spokenUtf8, "إثنا", "اثنا");
    replaceAllInPlace(spokenUtf8, "إثني", "اثني");
    replaceAllInPlace(spokenUtf8, "إثنتا", "اثنتا");
    replaceAllInPlace(spokenUtf8, "إثنتي", "اثنتي");
    replaceAllInPlace(spokenUtf8, "إثنا", "اثنا");
    replaceAllInPlace(spokenUtf8, "إثني", "اثني");

    if (kase == "nom") {
        // Subject-like nominative year style (e.g. 2023 هو ... -> ألفان وثلاثة وعشرون).
        replaceAllInPlace(spokenUtf8, "ألفين", "ألفان");
    }
    // Normalize common "X مائة" spellout into fused hundred forms.
    replaceAllInPlace(spokenUtf8, "ثلاثة مائة", "ثلاثمائة");
    replaceAllInPlace(spokenUtf8, "أربعة مائة", "أربعمائة");
    replaceAllInPlace(spokenUtf8, "خمسة مائة", "خمسمائة");
    replaceAllInPlace(spokenUtf8, "ستة مائة", "ستمائة");
    replaceAllInPlace(spokenUtf8, "سبعة مائة", "سبعمائة");
    replaceAllInPlace(spokenUtf8, "ثمانية مائة", "ثمانمائة");
    replaceAllInPlace(spokenUtf8, "تسعة مائة", "تسعمائة");

    const bool oblique = (kase == "gen_acc" || kase == "acc" || kase == "oblique");
    if (oblique) {
        replaceAllInPlace(spokenUtf8, "مائتان", "مائتين");
        // Dual nominative -> oblique/gen-acc.
        replaceAllInPlace(spokenUtf8, "اثنان", "اثنين");
        replaceAllInPlace(spokenUtf8, "اثنا عشر", "اثني عشر");
        replaceAllInPlace(spokenUtf8, "اثنتا عشرة", "اثنتي عشرة");
        // Round tens nominative -> oblique/gen-acc.
        replaceAllInPlace(spokenUtf8, "عشرون", "عشرين");
        replaceAllInPlace(spokenUtf8, "ثلاثون", "ثلاثين");
        replaceAllInPlace(spokenUtf8, "أربعون", "أربعين");
        replaceAllInPlace(spokenUtf8, "خمسون", "خمسين");
        replaceAllInPlace(spokenUtf8, "ستون", "ستين");
        replaceAllInPlace(spokenUtf8, "سبعون", "سبعين");
        replaceAllInPlace(spokenUtf8, "ثمانون", "ثمانين");
        replaceAllInPlace(spokenUtf8, "تسعون", "تسعين");
        // 21-99 compounds: refine unit part after tens become oblique.
        replaceAllInPlace(spokenUtf8, "واحد وعشرين", "واحدا وعشرين");
        replaceAllInPlace(spokenUtf8, "واحد وثلاثين", "واحدا وثلاثين");
        replaceAllInPlace(spokenUtf8, "واحد وأربعين", "واحدا وأربعين");
        replaceAllInPlace(spokenUtf8, "واحد وخمسين", "واحدا وخمسين");
        replaceAllInPlace(spokenUtf8, "واحد وستين", "واحدا وستين");
        replaceAllInPlace(spokenUtf8, "واحد وسبعين", "واحدا وسبعين");
        // Hundreds/thousands + unit: واحد stays (مائة وواحد وسبعين), not واحدا.
        replaceAllInPlace(spokenUtf8, "مائة وواحدا و", "مائة وواحد و");
        replaceAllInPlace(spokenUtf8, "مائتين وواحدا و", "مائتين وواحد و");
        replaceAllInPlace(spokenUtf8, "ألف وواحدا و", "ألف وواحد و");
        replaceAllInPlace(spokenUtf8, "ألفين وواحدا و", "ألفين وواحد و");
        replaceAllInPlace(spokenUtf8, "واحد وثمانين", "واحدا وثمانين");
        replaceAllInPlace(spokenUtf8, "واحد وتسعين", "واحدا وتسعين");
        replaceAllInPlace(spokenUtf8, "اثنان وعشرين", "اثنين وعشرين");
        replaceAllInPlace(spokenUtf8, "اثنان وثلاثين", "اثنين وثلاثين");
        replaceAllInPlace(spokenUtf8, "اثنان وأربعين", "اثنين وأربعين");
        replaceAllInPlace(spokenUtf8, "اثنان وخمسين", "اثنين وخمسين");
        replaceAllInPlace(spokenUtf8, "اثنان وستين", "اثنين وستين");
        replaceAllInPlace(spokenUtf8, "اثنان وسبعين", "اثنين وسبعين");
        replaceAllInPlace(spokenUtf8, "اثنان وثمانين", "اثنين وثمانين");
        replaceAllInPlace(spokenUtf8, "اثنان وتسعين", "اثنين وتسعين");
        replaceAllInPlace(spokenUtf8, "اثنتان وعشرين", "اثنتين وعشرين");
        replaceAllInPlace(spokenUtf8, "اثنتان وثلاثين", "اثنتين وثلاثين");
        replaceAllInPlace(spokenUtf8, "اثنتان وأربعين", "اثنتين وأربعين");
        replaceAllInPlace(spokenUtf8, "اثنتان وخمسين", "اثنتين وخمسين");
        replaceAllInPlace(spokenUtf8, "اثنتان وستين", "اثنتين وستين");
        replaceAllInPlace(spokenUtf8, "اثنتان وسبعين", "اثنتين وسبعين");
        replaceAllInPlace(spokenUtf8, "اثنتان وثمانين", "اثنتين وثمانين");
        replaceAllInPlace(spokenUtf8, "اثنتان وتسعين", "اثنتين وتسعين");
        // Oblique tens + thousands: ألف -> ألفا (e.g. بعشرين ألفا وستمائة).
        replaceAllInPlace(spokenUtf8, "ين ألف", "ين ألفا");
    }

    if (tamyiiz) {
        // Apocopated dual before counted noun (تمييز): مائتان/مائتين -> مائتي.
        replaceAllInPlace(spokenUtf8, "مائتين", "مائتي");
        replaceAllInPlace(spokenUtf8, "مائتان", "مائتي");
    }

    if (definiteArticle) {
        if (spokenUtf8.rfind("اثنين", 0) == 0) {
            spokenUtf8.replace(0, std::string("اثنين").size(), "الاثنين");
        } else if (spokenUtf8.rfind("اثنان", 0) == 0) {
            spokenUtf8.replace(0, std::string("اثنان").size(), "الاثنان");
        } else if (spokenUtf8.rfind("ال", 0) != 0) {
            spokenUtf8 = "ال" + spokenUtf8;
        }
    }
    if (nounGender == "feminine" || nounGender == "fem") {
        if (spokenUtf8 == "عشرة") {
            spokenUtf8 = "عشر";
        }
        // 13-19 with feminine counted nouns.
        replaceAllInPlace(spokenUtf8, "ثلاثة عشر", "ثلاث عشرة");
        replaceAllInPlace(spokenUtf8, "أربعة عشر", "أربع عشرة");
        replaceAllInPlace(spokenUtf8, "خمسة عشر", "خمس عشرة");
        replaceAllInPlace(spokenUtf8, "ستة عشر", "ست عشرة");
        replaceAllInPlace(spokenUtf8, "سبعة عشر", "سبع عشرة");
        replaceAllInPlace(spokenUtf8, "ثمانية عشر", "ثماني عشرة");
        replaceAllInPlace(spokenUtf8, "تسعة عشر", "تسع عشرة");
        // Simple numeral agreement for feminine counted nouns.
        replaceAllInPlace(spokenUtf8, "ثلاثة", "ثلاث");
        replaceAllInPlace(spokenUtf8, "أربعة", "أربع");
        replaceAllInPlace(spokenUtf8, "خمسة", "خمس");
        replaceAllInPlace(spokenUtf8, "ستة", "ست");
        replaceAllInPlace(spokenUtf8, "سبعة", "سبع");
        replaceAllInPlace(spokenUtf8, "ثمانية", "ثماني");
        replaceAllInPlace(spokenUtf8, "تسعة", "تسع");
        // Compound unit part before tens with feminine counted nouns (e.g. خمس وثلاثين دقيقة).
        replaceAllInPlace(spokenUtf8, "ثلاثة و", "ثلاث و");
        replaceAllInPlace(spokenUtf8, "أربعة و", "أربع و");
        replaceAllInPlace(spokenUtf8, "خمسة و", "خمس و");
        replaceAllInPlace(spokenUtf8, "ستة و", "ست و");
        replaceAllInPlace(spokenUtf8, "سبعة و", "سبع و");
        replaceAllInPlace(spokenUtf8, "ثمانية و", "ثماني و");
        replaceAllInPlace(spokenUtf8, "تسعة و", "تسع و");
    } else if (nounGender == "masculine" || nounGender == "masc") {
        // Years and masculine-counted contexts: ICU spellout may use feminine 11–19.
        replaceAllInPlace(spokenUtf8, "إحدى عشر", "أحد عشر");
        replaceAllInPlace(spokenUtf8, "إحدى عشرة", "أحد عشر");
        replaceAllInPlace(spokenUtf8, "واحدة عشرة", "واحد عشر");
        replaceAllInPlace(spokenUtf8, "واحدة و", "واحد و");
        replaceAllInPlace(spokenUtf8, "اثنتي عشرة", "اثني عشر");
        replaceAllInPlace(spokenUtf8, "اثنتا عشرة", "اثنا عشر");
        replaceAllInPlace(spokenUtf8, "ثلاث عشرة", "ثلاثة عشر");
        replaceAllInPlace(spokenUtf8, "أربع عشرة", "أربعة عشر");
        replaceAllInPlace(spokenUtf8, "خمس عشرة", "خمسة عشر");
        replaceAllInPlace(spokenUtf8, "ست عشرة", "ستة عشر");
        replaceAllInPlace(spokenUtf8, "سبع عشرة", "سبعة عشر");
        replaceAllInPlace(spokenUtf8, "ثماني عشرة", "ثمانية عشر");
        replaceAllInPlace(spokenUtf8, "تسع عشرة", "تسعة عشر");
        replaceAllInPlace(spokenUtf8, "ثلاث و", "ثلاثة و");
        replaceAllInPlace(spokenUtf8, "أربع و", "أربعة و");
        replaceAllInPlace(spokenUtf8, "خمس و", "خمسة و");
        replaceAllInPlace(spokenUtf8, "ست و", "ستة و");
        replaceAllInPlace(spokenUtf8, "سبع و", "سبعة و");
        replaceAllInPlace(spokenUtf8, "ثماني و", "ثمانية و");
        replaceAllInPlace(spokenUtf8, "تسع و", "تسعة و");
    }
    return spokenUtf8;
}

std::string formatArabicDateDayGenitive(int day) {
    if (day < 1 || day > 31) {
        return {};
    }
    switch (day) {
        case 1: return "الأول";
        case 2: return "الثاني";
        case 3: return "الثالث";
        case 4: return "الرابع";
        case 5: return "الخامس";
        case 6: return "السادس";
        case 7: return "السابع";
        case 8: return "الثامن";
        case 9: return "التاسع";
        case 10: return "العاشر";
        case 11: return "الحادي عشر";
        case 12: return "الثاني عشر";
        case 13: return "الثالث عشر";
        case 14: return "الرابع عشر";
        case 15: return "الخامس عشر";
        case 16: return "السادس عشر";
        case 17: return "السابع عشر";
        case 18: return "الثامن عشر";
        case 19: return "التاسع عشر";
        case 20: return "العشرين";
        case 30: return "الثلاثين";
        case 31: return "الواحد والثلاثين";
        default: break;
    }
    if (day >= 21 && day <= 29) {
        static const char* units[] = {
            "", "الواحد", "الثاني", "الثالث", "الرابع", "الخامس",
            "السادس", "السابع", "الثامن", "التاسع"
        };
        return std::string(units[day - 20]) + " والعشرين";
    }
    return {};
}
// ==== END verbatim ====

class ArabicBackend : public IcuSpelloutBackend {
protected:
    void registerOps(TtsNormalizerEngine& engine) override {
        engine.registerOp("ar_num_morph", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                              UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
            const int g = step.value("g", 0);
            const icu::UnicodeString numRaw = m.group(g, st);
            if (numRaw.isEmpty()) {
                return numRaw;
            }
            std::string numUtf8;
            numRaw.toUTF8String(numUtf8);
            replaceAllInPlace(numUtf8, ",", "");
            replaceAllInPlace(numUtf8, "،", "");
            replaceAllInPlace(numUtf8, "٬", "");
            const icu::UnicodeString num = icu::UnicodeString::fromUTF8(numUtf8);
            icu::UnicodeString spoken = cb.spellout(num);
            std::string spokenUtf8;
            spoken.toUTF8String(spokenUtf8);
            const std::string kase = jgetS(step, "case", "nom");
            const bool definiteArticle = step.value("definite_article", false);
            const std::string nounGender = jgetS(step, "noun_gender", "");
            const bool tamyiiz = step.value("tamyiiz", false);
            return icu::UnicodeString::fromUTF8(
                applyArabicNumberMorph(spokenUtf8, kase, definiteArticle, nounGender, tamyiiz));
        });

        engine.registerOp("ar_date_day", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                            UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
            const int g = step.value("g", 1);
            const int day = parseIntUStr(m.group(g, st));
            const std::string spoken = formatArabicDateDayGenitive(day);
            if (spoken.empty()) {
                return cb.spellout(m.group(g, st));
            }
            return icu::UnicodeString::fromUTF8(spoken);
        });
    }
};

BackendAutoRegister _reg_ar("arabic", [] { return std::unique_ptr<LanguageBackend>(new ArabicBackend()); });

} // namespace
