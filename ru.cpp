// TTS Russian text normalization — rules in rules_v2/ru.full.json.
// Requires MorphoDiTa model path via --morph-model <*.tagger>.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <unicode/locid.h>
#include <unicode/rbnf.h>
#include <unicode/unistr.h>

#include "morphodita.h"
#include "ru_year_spellout.hpp"
#include "tts_normalizer_engine.hpp"

namespace {

using ufal::morphodita::string_piece;
using ufal::morphodita::tagged_lemma;
using ufal::morphodita::tagger;
using ufal::morphodita::token_range;
using ufal::morphodita::tokenizer;

std::filesystem::path rulesV2Dir() {
    return std::filesystem::path(__FILE__).parent_path() / "rules_v2";
}

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

} // namespace

class TextNormalizer {
private:
    icu::RuleBasedNumberFormat* spelloutFmt_ = nullptr;
    tagger* morphTagger_ = nullptr;
    std::unique_ptr<tokenizer> tokenizer_;
    TtsNormalizerEngine engine_;
    std::vector<TokenInfo> analyzedTokens_;

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
        std::string ruleSet = "%spellout-ordinal-masculine";
        if (mode == "gen") {
            ruleSet = "%spellout-ordinal-masculine-genitive";
        } else if (mode == "nom_neuter") {
            // Day-of-month ("число") is neuter: 30 -> "тридцатое", not "тридцатый".
            ruleSet = "%spellout-ordinal-neuter";
        } else if (mode == "gen_neuter") {
            ruleSet = "%spellout-ordinal-neuter-genitive";
        } else if (mode.rfind("%spellout-ordinal-", 0) == 0) {
            // Explicit ICU ruleset name passed through verbatim.
            ruleSet = mode;
        } else if (!mode.empty() && mode != "nom") {
            // Shorthand: "<gender>-<case>" (e.g. "masculine-genitive", "feminine",
            // "masculine-prepositional") maps to the ICU ordinal ruleset family.
            ruleSet = "%spellout-ordinal-" + mode;
        }
        UErrorCode status = U_ZERO_ERROR;
        icu::UnicodeString out = formatRuleSet(spelloutFmt_, numStr, ruleSet.c_str(), status);
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

public:
    explicit TextNormalizer(const std::string& morphModelPath) {
        UErrorCode status = U_ZERO_ERROR;
        spelloutFmt_ = new icu::RuleBasedNumberFormat(icu::URBNF_SPELLOUT, icu::Locale("ru"), status);
        if (U_FAILURE(status)) {
            std::cerr << "Failed to init Russian spellout formatter\n";
            std::abort();
        }
        std::string err;
        if (!engine_.loadRulesV2((rulesV2Dir() / "ru.full.json").string(), err)) {
            std::cerr << "Failed to load TTS rules v2: " << err << std::endl;
            std::abort();
        }
        engine_.loadPinyinMap("", err);

        morphTagger_ = tagger::load(morphModelPath.c_str());
        if (!morphTagger_) {
            std::cerr << "Failed to load MorphoDiTa model: " << morphModelPath << std::endl;
            std::abort();
        }
        tokenizer_.reset(morphTagger_->new_tokenizer());
        if (!tokenizer_) {
            std::cerr << "MorphoDiTa model has no tokenizer: " << morphModelPath << std::endl;
            std::abort();
        }
    }

    ~TextNormalizer() {
        delete spelloutFmt_;
        delete morphTagger_;
    }

    std::string normalizeLine(const std::string& inputUtf8) {
        analyzeMorph(inputUtf8);
        icu::UnicodeString text = icu::UnicodeString::fromUTF8(inputUtf8);
        TtsCallbacks cb;
        cb.spellout = [this](const icu::UnicodeString& n) { return numberToRussianMasculine(n); };
        cb.ordinal_spellout = [this](const icu::UnicodeString& n, UErrorCode& st) {
            // Minimal ordinal fallback to cardinal keeps pipeline safe for ops that require ordinal callback.
            (void)st;
            return numberToRussianMasculine(n);
        };
        cb.lookup_morph = [this](int32_t start, int32_t end, TtsCallbacks::MorphFeatures& mf) {
            return lookupMorphBySpan(start, end, mf);
        };
        cb.spellout_with_morph = [this](const icu::UnicodeString& n, const TtsCallbacks::MorphFeatures& mf) {
            return numberToRussianMorph(n, mf);
        };
        cb.ru_year_spellout = [](int year, const std::string& mode) { return russianYearOrdinal(year, mode); };
        cb.ru_ordinal_spellout = [this](const icu::UnicodeString& n, const std::string& mode) {
            std::string out;
            numberToRussianOrdinal(n, mode).toUTF8String(out);
            return out;
        };
        text = engine_.runPipeline(text, cb);
        std::string out;
        text.toUTF8String(out);
        return out;
    }
};

int main(int argc, char** argv) {
    std::string morphModelPath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--morph-model" && i + 1 < argc) {
            morphModelPath = argv[++i];
        }
    }
    if (morphModelPath.empty()) {
        std::cerr << "Usage: ru_tts --morph-model /path/to/russian-syntagrus-morphodita-only.tagger\n";
        return 2;
    }

    TextNormalizer normalizer(morphModelPath);
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
