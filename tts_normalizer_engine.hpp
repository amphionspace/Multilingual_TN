#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <unicode/regex.h>
#include <unicode/unistr.h>

#include "third_party/nlohmann/json.hpp"

// JSON-driven TTS text normalization pipeline (ICU Regex + optional RBNF spellout callbacks).

struct TtsCallbacks {
    struct MorphFeatures {
        std::string number; // UD Number: Sing/Plur/...
        std::string gender; // UD Gender: Masc/Fem/Neut/...
        std::string kase;   // UD Case: Nom/Gen/Dat/Acc/Ins/Loc/Prep/...
    };

    // Cardinal spellout (locale-specific RBNF), e.g. "12" -> "twelve" / "十二"
    std::function<icu::UnicodeString(const icu::UnicodeString& num)> spellout;
    // Ordinal spellout (English only in original code; zh pipeline did not use ordinals the same way)
    std::function<icu::UnicodeString(const icu::UnicodeString& num, UErrorCode& status)> ordinal_spellout;
    // Optional lookup hook for language-specific morphology keyed by regex match span.
    std::function<bool(int32_t start, int32_t end, MorphFeatures& out)> lookup_morph;
    // Optional spellout hook using morphology (e.g. Russian case/gender/number inflection).
    std::function<icu::UnicodeString(const icu::UnicodeString& num, const MorphFeatures& mf)> spellout_with_morph;
    // Russian year ordinal (1800–2099); mode: neut | gen | fem | nom
    std::function<std::string(int year, const std::string& mode)> ru_year_spellout;
    // Russian ordinal number; mode: nom | gen (e.g. 39-го → тридцать девятого)
    std::function<std::string(const icu::UnicodeString& num, const std::string& mode)> ru_ordinal_spellout;
};

class TtsNormalizerEngine {
public:
    bool loadRules(const std::string& jsonPath, std::string& errOut);
    // Skeleton entry for linguist-authored rules_v2 documents.
    bool loadRulesV2(const std::string& jsonPath, std::string& errOut);
    bool loadPinyinMap(const std::string& jsonPath, std::string& errOut);

    const std::vector<icu::UnicodeString>& digitNames() const { return digitNames_; }
    const std::string& localeTag() const { return localeTag_; }

    icu::UnicodeString runPipeline(const icu::UnicodeString& input, const TtsCallbacks& cb) const;

private:
    struct Rule {
        icu::UnicodeString pattern;
        icu::UnicodeString replacement; // used when action empty: supports $0..$9
        std::string action;
        std::string id; // optional stable id from JSON ("id"); used when TTS_NORMALIZER_DEBUG is set
        nlohmann::json params = nlohmann::json::object();
    };

    std::string localeTag_;
    std::vector<icu::UnicodeString> digitNames_;
    std::unordered_map<std::string, icu::UnicodeString> currencyMap_;
    std::unordered_map<std::string, icu::UnicodeString> unitComposite_;
    std::unordered_map<std::string, icu::UnicodeString> unitSingle_;
    std::unordered_map<std::string, std::string> pinyinMap_;
    std::vector<Rule> rules_;
    std::vector<icu::UnicodeString> monthNames_;

    static icu::UnicodeString expandReplacement(const icu::UnicodeString& templ,
                                                icu::RegexMatcher& m,
                                                UErrorCode& st);
    icu::UnicodeString applyAction(const std::string& action,
                                   const nlohmann::json& params,
                                   icu::RegexMatcher& m,
                                   UErrorCode& st,
                                   const TtsCallbacks& cb) const;

    icu::UnicodeString applyExec(const nlohmann::json& params,
                                 icu::RegexMatcher& m,
                                 UErrorCode& st,
                                 const TtsCallbacks& cb) const;

    icu::UnicodeString applyExecStep(const nlohmann::json& step,
                                     icu::RegexMatcher& m,
                                     UErrorCode& st,
                                     const TtsCallbacks& cb) const;

    icu::UnicodeString digitNameChar(UChar c) const;
    icu::UnicodeString englishMonthName(int month) const;
    icu::UnicodeString monthNameForIsoDate(int month) const;
    /** English speech years ~1200–1999 (non-00 last two digits): "nineteen forty-one"; else cardinal spellout. */
    icu::UnicodeString spelloutEnglishYearProse(int year, const TtsCallbacks& cb) const;
    bool loadRulesFromJson(const nlohmann::json& j, std::string& errOut);
};
