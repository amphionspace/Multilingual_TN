// Russian backend: MorphoDiTa morphological analysis + case/gender/number-aware RBNF.
// Ported from ru.cpp. The morph model path comes from config.json ("morph_model").
// Note: this backend keeps the essential morphological analyzer — number case
// normalization depends on it. Phase 1's decoupling only removed the *engine's*
// hard link to Russian year code; here we supply parseRussianSpokenYear via the
// TtsCallbacks::parse_spoken_year hook.
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <unicode/locid.h>
#include <unicode/rbnf.h>
#include <unicode/unistr.h>

#include "backend.hpp"
#include "morphodita.h"
#include "op_helpers.hpp"
#include "ru_year_spellout.hpp"

namespace {

using namespace opu; // jgetS, parseIntUStr, trimAsciiSpaces

using ufal::morphodita::string_piece;
using ufal::morphodita::tagged_lemma;
using ufal::morphodita::tagger;
using ufal::morphodita::token_range;
using ufal::morphodita::tokenizer;

// ==== BEGIN verbatim from tts_normalizer_engine.cpp (Russian numeric helpers) ====
std::string toRussianFeminineCardinalTail(std::string spokenUtf8) {
    spokenUtf8 = trimAsciiSpaces(std::move(spokenUtf8));
    auto replaceTail = [&](const std::string& from, const std::string& to) {
        if (spokenUtf8 == from) {
            spokenUtf8 = to;
            return true;
        }
        const std::string needle = " " + from;
        if (spokenUtf8.size() > needle.size() &&
            spokenUtf8.compare(spokenUtf8.size() - needle.size(), needle.size(), needle) == 0) {
            spokenUtf8.replace(spokenUtf8.size() - from.size(), from.size(), to);
            return true;
        }
        return false;
    };
    if (replaceTail("один", "одна")) {
        return spokenUtf8;
    }
    replaceTail("два", "две");
    return spokenUtf8;
}

bool russianSingularByLastOne(int value) {
    const int absValue = std::abs(value);
    const int mod100 = absValue % 100;
    if (mod100 >= 11 && mod100 <= 14) {
        return false;
    }
    return (absValue % 10) == 1;
}

std::pair<std::string, std::string> russianFractionDenominatorForms(int fracDigits) {
    switch (fracDigits) {
        case 1:
            return {"десятая", "десятых"};
        case 2:
            return {"сотая", "сотых"};
        case 3:
            return {"тысячная", "тысячных"};
        case 4:
            return {"десятитысячная", "десятитысячных"};
        case 5:
            return {"стотысячная", "стотысячных"};
        case 6:
            return {"миллионная", "миллионных"};
        default:
            return {"десятичная", "десятичных"};
    }
}
// ==== END verbatim ====

struct TokenInfo {
    int32_t start16 = 0;
    int32_t end16 = 0;
    std::string upos;
    std::string feats;
    bool numeral = false;
    bool hasHeadFeat = false;
    TtsCallbacks::MorphFeatures mf;
};

struct UdFields {
    std::string upos;
    std::string feats;
};

bool splitConlluTag(const std::string& tag, UdFields& out) {
    if (tag.empty()) {
        return false;
    }
    const char sep = tag[0];
    std::vector<std::string> parts;
    size_t p = 1;
    while (p < tag.size()) {
        size_t q = tag.find(sep, p);
        if (q == std::string::npos) {
            parts.emplace_back(tag.substr(p));
            break;
        }
        parts.emplace_back(tag.substr(p, q - p));
        p = q + 1;
    }
    if (!parts.empty()) {
        out.upos = parts[0];
    }
    if (parts.size() >= 3) {
        out.feats = parts[2];
    }
    return true;
}

std::string featValue(const std::string& feats, const std::string& key) {
    const std::string needle = key + "=";
    size_t pos = 0;
    while (pos < feats.size()) {
        const size_t end = feats.find('|', pos);
        const std::string part = (end == std::string::npos) ? feats.substr(pos) : feats.substr(pos, end - pos);
        if (part.compare(0, needle.size(), needle) == 0) {
            return part.substr(needle.size());
        }
        if (end == std::string::npos) {
            break;
        }
        pos = end + 1;
    }
    return "";
}

bool looksNumericSurface(const std::string& form) {
    for (char c : form) {
        if (c >= '0' && c <= '9') {
            return true;
        }
    }
    return false;
}

int findHeadNounForwardBackward(const std::vector<TokenInfo>& sent, int index, int window) {
    for (int d = 1; d <= window; ++d) {
        const int j = index + d;
        if (j >= static_cast<int>(sent.size())) {
            break;
        }
        if (sent[static_cast<size_t>(j)].upos == "NOUN") {
            return j;
        }
    }
    for (int d = 1; d <= window; ++d) {
        const int j = index - d;
        if (j < 0) {
            break;
        }
        if (sent[static_cast<size_t>(j)].upos == "NOUN") {
            return j;
        }
    }
    return -1;
}

int32_t byteOffsetToUtf16(const std::string& utf8, size_t byteOffset) {
    if (byteOffset > utf8.size()) {
        byteOffset = utf8.size();
    }
    icu::UnicodeString u = icu::UnicodeString::fromUTF8(utf8.substr(0, byteOffset));
    return u.length();
}

icu::UnicodeString formatRuleSet(icu::RuleBasedNumberFormat* fmt,
                                 const icu::UnicodeString& numStr,
                                 const char* ruleSet,
                                 UErrorCode& st) {
    if (!fmt || U_FAILURE(st)) {
        return numStr;
    }
    std::string utf8;
    numStr.toUTF8String(utf8);
    try {
        const double val = std::stod(utf8);
        icu::UnicodeString out;
        icu::FieldPosition pos(icu::FieldPosition::DONT_CARE);
        fmt->format(val, icu::UnicodeString::fromUTF8(ruleSet), out, pos, st);
        return U_SUCCESS(st) ? out : numStr;
    } catch (...) {
        return numStr;
    }
}

std::string ruleSetFromMorph(const TtsCallbacks::MorphFeatures& mf) {
    std::string rule = "%spellout-cardinal";
    if (mf.number == "Plur") {
        rule += "-plural";
    } else if (mf.gender == "Fem") {
        rule += "-feminine";
    } else if (mf.gender == "Neut") {
        rule += "-neuter";
    } else {
        rule += "-masculine";
    }
    if (mf.kase == "Gen") {
        rule += "-genitive";
    } else if (mf.kase == "Dat") {
        rule += "-dative";
    } else if (mf.kase == "Acc") {
        rule += "-accusative";
    } else if (mf.kase == "Ins") {
        rule += "-instrumental";
    } else if (mf.kase == "Loc" || mf.kase == "Prep") {
        rule += "-locative";
    }
    return rule;
}

class RussianBackend : public LanguageBackend {
public:
    ~RussianBackend() override {
        delete spelloutFmt_;
        delete morphTagger_;
    }

    bool configure(const nlohmann::json& cfg, const std::string& resDir, std::string& err) override {
        UErrorCode status = U_ZERO_ERROR;
        spelloutFmt_ = new icu::RuleBasedNumberFormat(icu::URBNF_SPELLOUT, icu::Locale("ru"), status);
        if (U_FAILURE(status)) {
            err = "RussianBackend: failed to init spellout formatter";
            return false;
        }

        std::string modelPath = cfg.value("morph_model", "");
        if (modelPath.empty()) {
            err = "RussianBackend: config missing \"morph_model\"";
            return false;
        }
        if (!std::filesystem::path(modelPath).is_absolute()) {
            modelPath = (std::filesystem::path(resDir) / modelPath).lexically_normal().string();
        }
        morphTagger_ = tagger::load(modelPath.c_str());
        if (!morphTagger_) {
            err = "RussianBackend: failed to load MorphoDiTa model: " + modelPath;
            return false;
        }
        tokenizer_.reset(morphTagger_->new_tokenizer());
        if (!tokenizer_) {
            err = "RussianBackend: MorphoDiTa model has no tokenizer: " + modelPath;
            return false;
        }

        cb_.spellout = [this](const icu::UnicodeString& n) { return numberToRussianMasculine(n); };
        cb_.ordinal_spellout = [this](const icu::UnicodeString& n, UErrorCode& st) {
            (void)st;
            return numberToRussianMasculine(n);
        };
        cb_.lookup_morph = [this](int32_t start, int32_t end, TtsCallbacks::MorphFeatures& mf) {
            return lookupMorphBySpan(start, end, mf);
        };
        cb_.spellout_with_morph = [this](const icu::UnicodeString& n, const TtsCallbacks::MorphFeatures& mf) {
            return numberToRussianMorph(n, mf);
        };
        cb_.ru_year_spellout = [](int year, const std::string& mode) { return russianYearOrdinal(year, mode); };
        cb_.parse_spoken_year = [](const std::string& phrase) { return parseRussianSpokenYear(phrase); };
        cb_.ru_ordinal_spellout = [this](const icu::UnicodeString& n, const std::string& mode) {
            std::string out;
            numberToRussianOrdinal(n, mode).toUTF8String(out);
            return out;
        };
        return true;
    }

    const TtsCallbacks& callbacks() const override { return cb_; }

    void prepare(const std::string& inputUtf8) override { analyzeMorph(inputUtf8); }

    // Phase 2: Russian exec-ops, moved out of the engine. All use cb.* + free helpers,
    // so they are registered as plain handlers (no backend state needed).
    void registerOps(TtsNormalizerEngine& engine) override {
        engine.registerOp("ru_decimal_whole_cardinal", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                                          UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
            const int g = step.value("g", step.value("int_g", 1));
            const icu::UnicodeString num = m.group(g, st);
            if (num.isEmpty()) {
                return num;
            }
            icu::UnicodeString out;
            const bool hasLookup = static_cast<bool>(cb.lookup_morph);
            const bool hasMorphSpellout = static_cast<bool>(cb.spellout_with_morph);
            if (hasLookup && hasMorphSpellout) {
                TtsCallbacks::MorphFeatures mf;
                const int32_t start = m.start(g, st);
                const int32_t end = m.end(g, st);
                if (U_SUCCESS(st) && cb.lookup_morph(start, end, mf)) {
                    out = cb.spellout_with_morph(num, mf);
                }
            }
            if (out.isEmpty()) {
                out = cb.spellout(num);
            }
            if (russianSingularByLastOne(parseIntUStr(num))) {
                std::string spokenUtf8;
                out.toUTF8String(spokenUtf8);
                out = icu::UnicodeString::fromUTF8(toRussianFeminineCardinalTail(spokenUtf8));
            }
            return out;
        });

        engine.registerOp("ru_decimal_whole_word", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                                      UErrorCode& st, const TtsCallbacks& /*cb*/) -> icu::UnicodeString {
            const int g = step.value("g", step.value("int_g", 1));
            const icu::UnicodeString raw = m.group(g, st);
            if (raw.isEmpty()) {
                return icu::UnicodeString();
            }
            const std::string word = russianSingularByLastOne(parseIntUStr(raw)) ? "целая" : "целых";
            return icu::UnicodeString::fromUTF8(word);
        });

        engine.registerOp("ru_decimal_frac_cardinal", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                                         UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
            const int g = step.value("g", step.value("frac_g", 2));
            icu::UnicodeString rawNum = m.group(g, st);
            if (rawNum.isEmpty()) {
                return rawNum;
            }
            const std::string stripChars = jgetS(step, "strip", ",");
            icu::UnicodeString cleanNum;
            for (int i = 0; i < rawNum.length(); ++i) {
                UChar c = rawNum.charAt(i);
                bool skip = false;
                for (size_t j = 0; j < stripChars.size(); ++j) {
                    if (c == static_cast<UChar>(static_cast<unsigned char>(stripChars[j]))) {
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    cleanNum += c;
                }
            }
            std::string spokenUtf8;
            cb.spellout(cleanNum).toUTF8String(spokenUtf8);
            return icu::UnicodeString::fromUTF8(toRussianFeminineCardinalTail(spokenUtf8));
        });

        engine.registerOp("ru_decimal_denominator", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                                       UErrorCode& st, const TtsCallbacks& /*cb*/) -> icu::UnicodeString {
            const int g = step.value("g", step.value("frac_g", 2));
            const icu::UnicodeString fracRaw = m.group(g, st);
            if (fracRaw.isEmpty()) {
                return icu::UnicodeString();
            }
            std::string fracUtf8;
            fracRaw.toUTF8String(fracUtf8);
            int fracDigits = 0;
            for (char c : fracUtf8) {
                if (c >= '0' && c <= '9') {
                    ++fracDigits;
                }
            }
            if (fracDigits <= 0) {
                return icu::UnicodeString();
            }
            const auto denForms = russianFractionDenominatorForms(fracDigits);
            const std::string denomWord =
                russianSingularByLastOne(parseIntUStr(fracRaw)) ? denForms.first : denForms.second;
            return icu::UnicodeString::fromUTF8(denomWord);
        });

        engine.registerOp("ru_ipv4_spellout", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                                 UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
            const int g = step.value("g", 0);
            const icu::UnicodeString raw = m.group(g, st);
            if (raw.isEmpty()) {
                return raw;
            }
            const std::string octetSep = jgetS(step, "sep", " точка ");
            const std::string portSep = jgetS(step, "port_sep", " двоеточие ");
            icu::UnicodeString out;
            int start = 0;
            for (int i = 0; i <= raw.length(); ++i) {
                const bool atEnd = i == raw.length();
                const UChar c = atEnd ? 0 : raw.charAt(i);
                if (atEnd || c == '.' || c == ':') {
                    if (i > start) {
                        if (!out.isEmpty()) {
                            out += icu::UnicodeString::fromUTF8(octetSep);
                        }
                        out += cb.spellout(raw.tempSubString(start, i - start));
                    }
                    if (!atEnd && c == ':') {
                        out += icu::UnicodeString::fromUTF8(portSep);
                    }
                    start = i + 1;
                }
            }
            return out;
        });

        engine.registerOp("ru_ordinal_spellout", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                                    UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
            const int g = step.value("g", 1);
            const icu::UnicodeString raw = m.group(g, st);
            if (raw.isEmpty()) {
                return raw;
            }
            const std::string mode = jgetS(step, "mode", "nom");
            if (cb.ru_ordinal_spellout) {
                return icu::UnicodeString::fromUTF8(cb.ru_ordinal_spellout(raw, mode));
            }
            return raw;
        });

        engine.registerOp("ru_year_spellout", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                                 UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
            const int g = step.value("g", 1);
            const icu::UnicodeString raw = m.group(g, st);
            if (raw.isEmpty()) {
                return raw;
            }
            std::string key;
            raw.toUTF8String(key);
            int year = 0;
            try {
                year = std::stoi(key);
            } catch (...) {
                return raw;
            }
            const std::string mode = jgetS(step, "mode", "gen");
            if (cb.ru_year_spellout) {
                return icu::UnicodeString::fromUTF8(cb.ru_year_spellout(year, mode));
            }
            return raw;
        });

        engine.registerOp("ru_year_fixup", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                              UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
            const int g = step.value("g", 0);
            icu::UnicodeString raw = m.group(g, st);
            std::string phrase;
            raw.toUTF8String(phrase);
            const std::string suffix = jgetS(step, "suffix", " года");
            const std::string mode = jgetS(step, "mode", "gen");
            if (!cb.ru_year_spellout) {
                return raw;
            }
            int year = -1;
            if (step.value("parse_spoken", true) && cb.parse_spoken_year) {
                year = cb.parse_spoken_year(phrase);
            }
            if (year < 0) {
                std::string digits;
                for (char c : phrase) {
                    if (c >= '0' && c <= '9') {
                        digits += c;
                    }
                }
                if (digits.size() == 4) {
                    try {
                        year = std::stoi(digits);
                    } catch (...) {
                        year = -1;
                    }
                }
            }
            if (year >= 1800 && year <= 2099) {
                return icu::UnicodeString::fromUTF8(cb.ru_year_spellout(year, mode) + suffix);
            }
            return raw;
        });

        engine.registerOp("ru_num_morph", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                             UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
            const int g = step.value("g", 0);
            const icu::UnicodeString num = m.group(g, st);
            if (num.isEmpty()) {
                return num;
            }
            const bool hasLookup = static_cast<bool>(cb.lookup_morph);
            const bool hasMorphSpellout = static_cast<bool>(cb.spellout_with_morph);
            const bool forceMorph = step.value("force_morph", false) || step.contains("case") ||
                                    step.contains("gender") || step.contains("number");
            if (forceMorph && hasMorphSpellout) {
                TtsCallbacks::MorphFeatures mf;
                mf.gender = jgetS(step, "gender", "Masc");
                mf.number = jgetS(step, "number", "Sing");
                mf.kase = jgetS(step, "case", "Nom");
                icu::UnicodeString out = cb.spellout_with_morph(num, mf);
                if (!out.isEmpty()) {
                    return out;
                }
            }
            if (hasLookup && hasMorphSpellout) {
                TtsCallbacks::MorphFeatures mf;
                const int32_t start = m.start(g, st);
                const int32_t end = m.end(g, st);
                if (U_SUCCESS(st) && cb.lookup_morph(start, end, mf)) {
                    icu::UnicodeString out = cb.spellout_with_morph(num, mf);
                    if (!out.isEmpty()) {
                        return out;
                    }
                }
            }
            return cb.spellout(num);
        });
    }

private:
    icu::UnicodeString numberToRussianMasculine(const icu::UnicodeString& numStr) {
        UErrorCode status = U_ZERO_ERROR;
        icu::UnicodeString out = formatRuleSet(spelloutFmt_, numStr, "%spellout-cardinal-masculine", status);
        if (U_FAILURE(status) || out.isEmpty()) {
            status = U_ZERO_ERROR;
            std::string utf8;
            numStr.toUTF8String(utf8);
            try {
                const double val = std::stod(utf8);
                spelloutFmt_->format(val, out, status);
                if (U_SUCCESS(status) && !out.isEmpty()) {
                    return out;
                }
            } catch (...) {
            }
            return numStr;
        }
        return out;
    }

    icu::UnicodeString numberToRussianMorph(const icu::UnicodeString& numStr, const TtsCallbacks::MorphFeatures& mf) {
        const std::string ruleSet = ruleSetFromMorph(mf);
        UErrorCode status = U_ZERO_ERROR;
        icu::UnicodeString out = formatRuleSet(spelloutFmt_, numStr, ruleSet.c_str(), status);
        if (U_FAILURE(status) || out.isEmpty()) {
            status = U_ZERO_ERROR;
            out = formatRuleSet(spelloutFmt_, numStr, "%spellout-cardinal-masculine", status);
            if (U_FAILURE(status) || out.isEmpty()) {
                return numStr;
            }
        }
        return out;
    }

    icu::UnicodeString numberToRussianOrdinal(const icu::UnicodeString& numStr, const std::string& mode) {
        const char* ruleSet = "%spellout-ordinal-masculine";
        if (mode == "gen") {
            ruleSet = "%spellout-ordinal-masculine-genitive";
        }
        UErrorCode status = U_ZERO_ERROR;
        icu::UnicodeString out = formatRuleSet(spelloutFmt_, numStr, ruleSet, status);
        if (U_FAILURE(status) || out.isEmpty()) {
            return numberToRussianMasculine(numStr);
        }
        return out;
    }

    bool lookupMorphBySpan(int32_t start16, int32_t end16, TtsCallbacks::MorphFeatures& out) const {
        for (const TokenInfo& tk : analyzedTokens_) {
            if (!tk.numeral || !tk.hasHeadFeat) {
                continue;
            }
            const bool overlap = !(end16 <= tk.start16 || start16 >= tk.end16);
            if (overlap) {
                out = tk.mf;
                return true;
            }
        }
        return false;
    }

    void analyzeMorph(const std::string& inputUtf8) {
        analyzedTokens_.clear();
        if (!tokenizer_ || !morphTagger_) {
            return;
        }
        tokenizer_->set_text(string_piece(inputUtf8), true);
        std::vector<string_piece> forms;
        std::vector<token_range> ranges;
        while (tokenizer_->next_sentence(&forms, &ranges)) {
            if (forms.empty()) {
                continue;
            }
            std::vector<tagged_lemma> tags;
            morphTagger_->tag(forms, tags);
            if (tags.size() != forms.size() || ranges.size() != forms.size()) {
                continue;
            }
            std::vector<TokenInfo> sent;
            sent.reserve(forms.size());
            for (size_t i = 0; i < forms.size(); ++i) {
                TokenInfo tk;
                tk.start16 = byteOffsetToUtf16(inputUtf8, ranges[i].start);
                tk.end16 = byteOffsetToUtf16(inputUtf8, ranges[i].start + ranges[i].length);
                UdFields ud;
                splitConlluTag(tags[i].tag, ud);
                tk.upos = ud.upos;
                tk.feats = ud.feats;
                const std::string form(forms[i].str, forms[i].len);
                tk.numeral = (tk.upos == "NUM") || looksNumericSurface(form);
                sent.push_back(std::move(tk));
            }
            for (size_t i = 0; i < sent.size(); ++i) {
                if (!sent[i].numeral) {
                    continue;
                }
                const int head = findHeadNounForwardBackward(sent, static_cast<int>(i), 5);
                if (head < 0) {
                    continue;
                }
                sent[i].mf.kase = featValue(sent[static_cast<size_t>(head)].feats, "Case");
                sent[i].mf.gender = featValue(sent[static_cast<size_t>(head)].feats, "Gender");
                sent[i].mf.number = featValue(sent[static_cast<size_t>(head)].feats, "Number");
                sent[i].hasHeadFeat = true;
            }
            analyzedTokens_.insert(analyzedTokens_.end(), sent.begin(), sent.end());
        }
    }

    icu::RuleBasedNumberFormat* spelloutFmt_ = nullptr;
    tagger* morphTagger_ = nullptr;
    std::unique_ptr<tokenizer> tokenizer_;
    std::vector<TokenInfo> analyzedTokens_;
    TtsCallbacks cb_;
};

BackendAutoRegister _reg_ru("russian", [] { return std::unique_ptr<LanguageBackend>(new RussianBackend()); });

} // namespace
