// Chinese G2P tone-sandhi backend ("chinese_g2p").
//
// Runs AFTER dictionary lookup: the input is an already looked-up pinyin sequence and
// the output is that same sequence with tone sandhi applied. It spells out no numbers
// and owns no dictionary, so it derives from LanguageBackend directly instead of the
// ICU spellout base.
//
// Token contract (docs/zh_g2p_sandhi.md):
//   syllable   pinyin letters + tone digit 1-5, 5 = neutral        "ni3" "de5"
//   "|"        word / prosodic-word boundary from the segmenter
//   separator  a single space; punctuation passes through as its own token
#include <string>
#include <vector>

#include <unicode/regex.h>
#include <unicode/unistr.h>
#include <unicode/utf16.h>

#include "backend.hpp"
#include "op_helpers.hpp"

namespace {

using namespace opu; // jgetS, parseIntUStr

bool isBoundary(const std::string& tok) {
    return tok == "|";
}

char toneDigit(const std::string& syl) {
    if (syl.empty()) {
        return '\0';
    }
    const char c = syl.back();
    return (c >= '1' && c <= '5') ? c : '\0';
}

void setToneDigit(std::string& syl, char tone) {
    if (toneDigit(syl) != '\0') {
        syl.back() = tone;
    }
}

std::vector<std::string> splitOnSpaces(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') {
            ++i;
        }
        const size_t start = i;
        while (i < s.size() && s[i] != ' ') {
            ++i;
        }
        if (i > start) {
            out.push_back(s.substr(start, i - start));
        }
    }
    return out;
}

std::string joinWithSpaces(const std::vector<std::string>& toks) {
    std::string out;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (i != 0) {
            out += ' ';
        }
        out += toks[i];
    }
    return out;
}

// Third-tone sandhi over one maximal run of T3 syllables. The rule's pattern matches the
// whole run in a single match, because the engine's scan is non-overlapping and does not
// re-read what it already wrote.
//
// Grouping differs inside a word vs. across words, which is what the segmenter's "|" buys:
//   inside a word the grouping is left-branching  -> every T3 but the word's last drops to T2
//       zhan3 lan3 guan3   (展览馆)   -> zhan2 lan2 guan3
//   across words it is right-branching -> scan right-to-left and only lower a T3 that still
//   faces a T3, so a syllable followed by an already-lowered T2 keeps its own T3
//       wo3 | hen3 | hao3  (我很好)   -> wo3 hen2 hao3   (not wo2 hen2 hao3)
//       xiao3 | lao3 hu3   (小老虎)   -> xiao3 lao2 hu3
icu::UnicodeString opTone3Sandhi(const nlohmann::json& step, icu::RegexMatcher& m, UErrorCode& st,
                                 const TtsCallbacks& /*cb*/) {
    const int g = step.value("g", 0);
    const icu::UnicodeString runU = m.group(g, st);
    if (U_FAILURE(st)) {
        return runU;
    }
    std::string run;
    runU.toUTF8String(run);

    std::vector<std::string> toks = splitOnSpaces(run);
    std::vector<size_t> syl;  // indices into toks, boundary markers skipped
    std::vector<int> wordOf;  // word id of each syllable
    int word = 0;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (isBoundary(toks[i])) {
            ++word;
            continue;
        }
        syl.push_back(i);
        wordOf.push_back(word);
    }
    if (syl.size() < 2) {
        return runU;
    }

    for (size_t i = 0; i + 1 < syl.size(); ++i) {
        if (wordOf[i] == wordOf[i + 1] && toneDigit(toks[syl[i]]) == '3') {
            setToneDigit(toks[syl[i]], '2');
        }
    }
    for (size_t i = syl.size() - 1; i-- > 0;) {
        if (toneDigit(toks[syl[i]]) == '3' && toneDigit(toks[syl[i + 1]]) == '3') {
            setToneDigit(toks[syl[i]], '2');
        }
    }
    return icu::UnicodeString::fromUTF8(joinWithSpaces(toks));
}

struct ToneMark {
    UChar32 marked;
    UChar32 base;
    char tone;
};

const ToneMark kToneMarks[] = {
    {0x0101, 'a', '1'},    {0x00E1, 'a', '2'},    {0x01CE, 'a', '3'},    {0x00E0, 'a', '4'},
    {0x0113, 'e', '1'},    {0x00E9, 'e', '2'},    {0x011B, 'e', '3'},    {0x00E8, 'e', '4'},
    {0x012B, 'i', '1'},    {0x00ED, 'i', '2'},    {0x01D0, 'i', '3'},    {0x00EC, 'i', '4'},
    {0x014D, 'o', '1'},    {0x00F3, 'o', '2'},    {0x01D2, 'o', '3'},    {0x00F2, 'o', '4'},
    {0x016B, 'u', '1'},    {0x00FA, 'u', '2'},    {0x01D4, 'u', '3'},    {0x00F9, 'u', '4'},
    {0x01D6, 0x00FC, '1'}, {0x01D8, 0x00FC, '2'}, {0x01DA, 0x00FC, '3'}, {0x01DC, 0x00FC, '4'},
    {0x0144, 'n', '2'},    {0x0148, 'n', '3'},    {0x01F9, 'n', '4'},
};

// Entry adapter for dictionaries that emit diacritic pinyin: "nǐ" -> "ni3", "de" -> "de5".
// A syllable carrying no tone mark is neutral by convention, hence the 5 fallback.
icu::UnicodeString opPyToneMarkToDigit(const nlohmann::json& step, icu::RegexMatcher& m, UErrorCode& st,
                                       const TtsCallbacks& /*cb*/) {
    const int g = step.value("g", 0);
    const icu::UnicodeString sylU = m.group(g, st);
    if (U_FAILURE(st)) {
        return sylU;
    }
    icu::UnicodeString out;
    char tone = '\0';
    for (int32_t i = 0; i < sylU.length();) {
        const UChar32 c = sylU.char32At(i);
        i += U16_LENGTH(c);
        bool mapped = false;
        for (const auto& tm : kToneMarks) {
            if (c == tm.marked) {
                out.append(tm.base);
                tone = tm.tone;
                mapped = true;
                break;
            }
        }
        if (!mapped) {
            out.append(c);
        }
    }
    out.append(static_cast<UChar32>(tone != '\0' ? tone : '5'));
    return out;
}

class ChineseG2pBackend : public LanguageBackend {
public:
    bool configure(const nlohmann::json& /*cfg*/, const std::string& /*resDir*/, std::string& /*err*/) override {
        return true;
    }

    const TtsCallbacks& callbacks() const override { return cb_; }

    void registerOps(TtsNormalizerEngine& engine) override {
        engine.registerOp("tone3_sandhi", opTone3Sandhi);
        engine.registerOp("py_tone_mark_to_digit", opPyToneMarkToDigit);
    }

private:
    TtsCallbacks cb_;
};

BackendAutoRegister _reg_zh_g2p("chinese_g2p", [] { return std::unique_ptr<LanguageBackend>(new ChineseG2pBackend()); });

} // namespace
