// Unified en-zh G2P backend: TN-normalized mixed text -> model phoneme tokens.
//
// Pipeline (rhyme-body-tone paradigm, matching en-zh-dict training):
//   mixed TN text -> zh lexicon G2P + EN CMUdict -> zh_g2p sandhi rules -> Bopomofo + ARPAbet tokens
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <unicode/unistr.h>
#include <unicode/utf8.h>

#include "backend.hpp"
#include "chinese_g2p_ops.hpp"
#include "op_helpers.hpp"

namespace {

using opu::trimAsciiSpaces;

struct EnZhG2pResources;

std::vector<std::string> utf8Chars(const std::string& text);

bool isHanzi(UChar32 c) { return c >= 0x4E00 && c <= 0x9FFF; }

bool isLatinAlpha(UChar32 c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isPinyinSyllable(const std::string& tok) {
    if (tok.size() < 2) {
        return false;
    }
    const char tone = tok.back();
    if (tone < '1' || tone > '5') {
        return false;
    }
    for (size_t i = 0; i + 1 < tok.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(tok[i]);
        if (!std::islower(ch) && ch != 'v') {
            return false;
        }
    }
    return true;
}

bool isArpabetToken(const std::string& tok) {
    if (tok.empty() || !std::isupper(static_cast<unsigned char>(tok[0]))) {
        return false;
    }
    for (char ch : tok) {
        if (!std::isupper(static_cast<unsigned char>(ch)) && !std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

std::string toUpperAscii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::vector<std::string> splitSpaces(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) {
        out.push_back(tok);
    }
    return out;
}

void appendToken(std::vector<std::string>& out, const std::string& tok) {
    if (!tok.empty()) {
        out.push_back(tok);
    }
}

void appendUtf8Codepoint(std::vector<std::string>& out, UChar32 cp) {
    char buf[8] = {};
    int32_t bi = 0;
    U8_APPEND_UNSAFE(buf, bi, cp);
    out.emplace_back(buf, static_cast<size_t>(bi));
}

bool isZhBreakPunct(UChar32 cp) {
    switch (cp) {
    case ',':
    case '.':
    case '!':
    case '?':
    case ';':
    case ':':
    case 0xFF0C: // ，
    case 0x3002: // 。
    case 0xFF01: // ！
    case 0xFF1F: // ？
    case 0xFF1B: // ；
    case 0xFF1A: // ：
        return true;
    default:
        return false;
    }
}

UChar32 canonicalHalfwidthPunct(UChar32 cp) {
    switch (cp) {
    case 0xFF0C:
        return ',';
    case 0x3002:
        return '.';
    case 0xFF01:
        return '!';
    case 0xFF1F:
        return '?';
    case 0xFF1B:
        return ';';
    case 0xFF1A:
        return ':';
    default:
        return cp;
    }
}

void pushStreamBoundary(std::vector<std::string>& stream) {
    if (!stream.empty()) {
        stream.push_back("_");
    }
}

bool textContainsHanzi(const std::string& text) {
    for (const std::string& c : utf8Chars(text)) {
        UChar32 cp;
        int32_t idx = 0;
        U8_NEXT(c.c_str(), idx, static_cast<int32_t>(c.size()), cp);
        if (isHanzi(cp)) {
            return true;
        }
    }
    return false;
}

bool isPunctuationToken(const std::string& tok) {
    if (tok.empty()) {
        return false;
    }
    const std::vector<std::string> chars = utf8Chars(tok);
    if (chars.size() != 1) {
        return false;
    }
    UChar32 cp;
    int32_t idx = 0;
    U8_NEXT(chars[0].c_str(), idx, static_cast<int32_t>(chars[0].size()), cp);
    return isZhBreakPunct(cp);
}

std::string canonicalHalfwidthPunctToken(const std::string& tok) {
    UChar32 cp;
    int32_t idx = 0;
    U8_NEXT(tok.c_str(), idx, static_cast<int32_t>(tok.size()), cp);
    char buf[8] = {};
    int32_t bi = 0;
    U8_APPEND_UNSAFE(buf, bi, canonicalHalfwidthPunct(cp));
    return std::string(buf, static_cast<size_t>(bi));
}

std::vector<std::string> spellWord(const std::string& word, const EnZhG2pResources& res);

void appendEnglishArpabetWord(std::vector<std::string>& stream,
                              const std::string& word,
                              const std::string* prevWord,
                              const std::string* nextWord,
                              const EnZhG2pResources& res);

void appendEnglishWordRun(std::vector<std::string>& stream,
                          const std::vector<std::string>& words,
                          bool mixedHanzi,
                          const EnZhG2pResources& res);

void appendBoundary(std::vector<std::string>& out) {
    if (!out.empty() && out.back() != "_") {
        out.push_back("_");
    }
}

void ensureBoundaryBeforeArpabet(std::vector<std::string>& out) {
    if (out.empty() || out.back() == "_") {
        return;
    }
    if (isArpabetToken(out.back())) {
        return;
    }
    out.push_back("_");
}

void ensureBoundaryAfterArpabet(std::vector<std::string>& out) {
    if (!out.empty() && isArpabetToken(out.back())) {
        out.push_back("_");
    }
}

void appendPunctuationToken(std::vector<std::string>& out, const std::string& ch) {
    if (!out.empty() && out.back() != "_") {
        out.push_back("_");
    }
    out.push_back(ch);
    out.push_back("_");
}

// --- merge_words (yi / bu / er) ported from frontend_rules/ops/sandhi.py ---

std::vector<std::string> mergeBu(std::vector<std::string> seg) {
    std::vector<std::string> newSeg;
    std::string lastWord;
    for (const std::string& word : seg) {
        if (word != "不") {
            std::string merged = word;
            if (lastWord == "不") {
                merged = lastWord + word;
            }
            newSeg.push_back(std::move(merged));
        }
        lastWord = word;
    }
    if (lastWord == "不") {
        newSeg.push_back("不");
    }
    return newSeg;
}

std::vector<std::string> mergeEr(std::vector<std::string> seg) {
    std::vector<std::string> newSeg;
    for (size_t i = 0; i < seg.size(); ++i) {
        if (i > 0 && seg[i] == "儿") {
            newSeg.back() += seg[i];
        } else {
            newSeg.push_back(seg[i]);
        }
    }
    return newSeg;
}

std::vector<std::string> mergeYi(std::vector<std::string> seg) {
    std::vector<std::string> newSeg;
    for (size_t i = 0; i < seg.size(); ++i) {
        if (i > 0 && seg[i] == "一" && i + 1 < seg.size() && seg[i - 1] == seg[i + 1]) {
            if (i - 1 < newSeg.size()) {
                newSeg[i - 1] = newSeg[i - 1] + "一" + newSeg[i - 1];
            } else {
                newSeg.push_back(seg[i]);
                newSeg.push_back(seg[i + 1]);
            }
        } else {
            if (i >= 2 && seg[i - 1] == "一" && seg[i - 2] == seg[i]) {
                continue;
            }
            newSeg.push_back(seg[i]);
        }
    }
    seg = std::move(newSeg);
    newSeg.clear();
    for (const std::string& word : seg) {
        if (!newSeg.empty() && newSeg.back() == "一") {
            newSeg.back() += word;
        } else {
            newSeg.push_back(word);
        }
    }
    return newSeg;
}

std::vector<std::string> mergeWords(std::vector<std::string> words) {
    words = mergeYi(std::move(words));
    words = mergeBu(std::move(words));
    words = mergeEr(std::move(words));
    return words;
}

// --- resource tables ---

using CmuVariantList = std::vector<std::vector<std::string>>;

struct EnZhG2pResources {
    std::unordered_map<std::string, std::string> lexicon;
    std::unordered_map<std::string, std::string> pinyinToBpmf;
    std::unordered_map<std::string, CmuVariantList> cmudict;
    size_t maxLexiconWordLen = 1;

    static bool loadTsvMap(const std::string& path,
                           std::unordered_map<std::string, std::string>& out,
                           std::string& err) {
        std::ifstream in(path);
        if (!in) {
            err = "cannot open: " + path;
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) {
                continue;
            }
            out[line.substr(0, tab)] = line.substr(tab + 1);
        }
        return true;
    }

    static bool loadCmudict(const std::string& path,
                            std::unordered_map<std::string, CmuVariantList>& out,
                            std::string& err) {
        std::ifstream in(path);
        if (!in) {
            err = "cannot open cmudict: " + path;
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line.rfind(";;;", 0) == 0) {
                continue;
            }
            std::string word;
            std::string phones;
            const size_t tab = line.find('\t');
            if (tab != std::string::npos) {
                word = line.substr(0, tab);
                phones = line.substr(tab + 1);
            } else {
                std::istringstream iss(line);
                if (!(iss >> word)) {
                    continue;
                }
                std::getline(iss, phones);
                trimAsciiSpaces(phones);
            }
            if (word.empty() || phones.empty()) {
                continue;
            }
            std::string base = word;
            size_t variantIdx = 0;
            const size_t paren = word.find('(');
            if (paren != std::string::npos) {
                base = word.substr(0, paren);
                const size_t close = word.find(')', paren + 1);
                if (close == std::string::npos) {
                    continue;
                }
                try {
                    variantIdx = static_cast<size_t>(std::stoul(word.substr(paren + 1, close - paren - 1)));
                } catch (...) {
                    continue;
                }
            }
            std::vector<std::string> toks = splitSpaces(phones);
            if (toks.empty()) {
                continue;
            }
            const std::string key = toUpperAscii(base);
            CmuVariantList& variants = out[key];
            if (variants.size() <= variantIdx) {
                variants.resize(variantIdx + 1);
            }
            variants[variantIdx] = std::move(toks);
        }
        return true;
    }

    bool load(const nlohmann::json& cfg, const std::string& resDir, std::string& err) {
        auto resolve = [&](const std::string& key) -> std::string {
            const std::string rel = cfg.value(key, "");
            if (rel.empty()) {
                return "";
            }
            if (rel[0] == '/') {
                return rel;
            }
            return (std::filesystem::path(resDir) / rel).lexically_normal().string();
        };

        const std::string lexPath = resolve("lexicon");
        const std::string bpmfPath = resolve("pinyin_bpmf");
        const std::string cmuPath = resolve("cmudict");
        if (lexPath.empty() || bpmfPath.empty() || cmuPath.empty()) {
            err = "en_zh_g2p config requires lexicon, pinyin_bpmf, cmudict";
            return false;
        }
        if (!loadTsvMap(lexPath, lexicon, err)) {
            return false;
        }
        const std::string userDictPath = resolve("user_dict");
        if (!userDictPath.empty()) {
            std::ifstream userDictIn(userDictPath);
            if (userDictIn) {
                if (!loadTsvMap(userDictPath, lexicon, err)) {
                    return false;
                }
            }
        }
        if (!loadTsvMap(bpmfPath, pinyinToBpmf, err)) {
            return false;
        }
        if (!loadCmudict(cmuPath, cmudict, err)) {
            return false;
        }
        for (const auto& kv : lexicon) {
            maxLexiconWordLen = std::max(maxLexiconWordLen, utf8Chars(kv.first).size());
        }
        return true;
    }
};

std::string collapseLetterDotsInWord(const std::string& word) {
    if (word.find('.') == std::string::npos) {
        return word;
    }
    bool sawDot = false;
    std::string collapsed;
    collapsed.reserve(word.size());
    for (unsigned char ch : word) {
        if (ch == '.') {
            sawDot = true;
            continue;
        }
        if (!std::isalpha(ch)) {
            return word;
        }
        collapsed.push_back(static_cast<char>(ch));
    }
    return sawDot ? collapsed : word;
}

const CmuVariantList* lookupCmudictVariants(const std::string& word, const EnZhG2pResources& res) {
    const auto it = res.cmudict.find(toUpperAscii(word));
    if (it == res.cmudict.end() || it->second.empty()) {
        return nullptr;
    }
    return &it->second;
}

std::optional<int> selectAcronymCaseAction(const std::string& word, size_t numVariants) {
    if (numVariants <= 1) {
        return std::nullopt;
    }
    bool hasLetter = false;
    bool allUpper = true;
    bool allLower = true;
    for (unsigned char ch : word) {
        if (!std::isalpha(ch)) {
            continue;
        }
        hasLetter = true;
        if (std::isupper(ch)) {
            allLower = false;
        } else {
            allUpper = false;
        }
    }
    if (!hasLetter || (!allUpper && !allLower)) {
        return std::nullopt;
    }
    const std::string key = toUpperAscii(word);
    if (key != "US" && key != "AI" && key != "AM") {
        return std::nullopt;
    }
    if (allLower) {
        return 0;
    }
    return 1;
}

std::optional<int> selectHomographVariant(const std::string& word, size_t numVariants) {
    if (toUpperAscii(word) == "TO" && numVariants > 2) {
        return 2;
    }
    return std::nullopt;
}

std::vector<std::string> lookupIsolatedLetterPhonemes(const std::string& letter,
                                                      bool preferLetterName,
                                                      const EnZhG2pResources& res) {
    if (letter.size() != 1 || !std::isalpha(static_cast<unsigned char>(letter[0]))) {
        return {};
    }
    const CmuVariantList* variants = lookupCmudictVariants(letter, res);
    if (variants == nullptr || variants->empty() || (*variants)[0].empty()) {
        return {};
    }
    if (preferLetterName && toUpperAscii(letter) == "A" && variants->size() > 1 &&
        !(*variants)[1].empty()) {
        return (*variants)[1];
    }
    return (*variants)[0];
}

std::vector<std::string> lookupWordPhonemes(const std::string& word,
                                              const std::string* prevWord,
                                              const std::string* nextWord,
                                              const EnZhG2pResources& res) {
    (void)prevWord;
    (void)nextWord;
    const CmuVariantList* variants = lookupCmudictVariants(word, res);
    if (variants != nullptr && !variants->empty() && !(*variants)[0].empty()) {
        if (const std::optional<int> pick = selectAcronymCaseAction(word, variants->size())) {
            if (!(*variants)[static_cast<size_t>(*pick)].empty()) {
                return (*variants)[static_cast<size_t>(*pick)];
            }
        }
        if (const std::optional<int> pick = selectHomographVariant(word, variants->size())) {
            if (!(*variants)[static_cast<size_t>(*pick)].empty()) {
                return (*variants)[static_cast<size_t>(*pick)];
            }
        }
        return (*variants)[0];
    }
    return spellWord(word, res);
}

std::vector<std::string> spellWord(const std::string& word, const EnZhG2pResources& res) {
    std::vector<std::string> phones;
    for (char ch : word) {
        if (!std::isalpha(static_cast<unsigned char>(ch))) {
            continue;
        }
        const std::vector<std::string> letterPhones =
            lookupIsolatedLetterPhonemes(std::string(1, ch), true, res);
        if (letterPhones.empty()) {
            return {};
        }
        phones.insert(phones.end(), letterPhones.begin(), letterPhones.end());
    }
    return phones;
}

void appendEnglishArpabetWord(std::vector<std::string>& stream,
                              const std::string& word,
                              const std::string* prevWord,
                              const std::string* nextWord,
                              const EnZhG2pResources& res) {
    const std::string normalized = collapseLetterDotsInWord(word);
    const std::vector<std::string> phones =
        lookupWordPhonemes(normalized, prevWord, nextWord, res);
    if (phones.empty()) {
        return;
    }
    pushStreamBoundary(stream);
    stream.insert(stream.end(), phones.begin(), phones.end());
}

std::vector<std::string> normalizeLexiconPinyin(const std::string& pinyin) {
    std::vector<std::string> syllables;
    for (const std::string& py : splitSpaces(pinyin)) {
        std::string norm;
        norm.reserve(py.size());
        for (size_t i = 0; i < py.size();) {
            const unsigned char b0 = static_cast<unsigned char>(py[i]);
            if (b0 == 0xC3 && i + 1 < py.size()) {
                const unsigned char b1 = static_cast<unsigned char>(py[i + 1]);
                if (b1 == 0xBC || b1 == 0x9C) { // ü / Ü
                    norm.push_back('v');
                    i += 2;
                    continue;
                }
            }
            norm.push_back(py[i]);
            ++i;
        }
        if (isPinyinSyllable(norm)) {
            syllables.push_back(norm);
        }
    }
    return syllables;
}

std::vector<std::string> utf8Chars(const std::string& text) {
    std::vector<std::string> chars;
    const char* src = text.c_str();
    int32_t srcLen = static_cast<int32_t>(text.size());
    int32_t i = 0;
    while (i < srcLen) {
        UChar32 c;
        U8_NEXT(src, i, srcLen, c);
        if (c < 0) {
            break;
        }
        char buf[5] = {};
        int32_t j = 0;
        U8_APPEND_UNSAFE(buf, j, c);
        chars.emplace_back(buf, static_cast<size_t>(j));
    }
    return chars;
}

bool candidateSplitsFollowingLexiconWord(const std::string& cand,
                                         const std::vector<std::string>& textChars,
                                         size_t i,
                                         const EnZhG2pResources& res) {
    const std::vector<std::string> candChars = utf8Chars(cand);
    if (candChars.size() < 2 || i + candChars.size() > textChars.size()) {
        return false;
    }
    const size_t overlapStart = i + candChars.size() - 1;
    std::string prefix;
    for (size_t k = 0; k + 1 < candChars.size(); ++k) {
        prefix += candChars[k];
    }
    const size_t maxLen = std::min(res.maxLexiconWordLen, textChars.size() - overlapStart);
    for (size_t len = maxLen; len >= 2; --len) {
        std::string word;
        for (size_t k = 0; k < len; ++k) {
            word += textChars[overlapStart + k];
        }
        if (word == cand || !res.lexicon.count(word)) {
            continue;
        }
        if (textChars[overlapStart] != candChars.back()) {
            continue;
        }
        if (!res.lexicon.count(prefix)) {
            continue;
        }
        const std::vector<std::string> wordChars = utf8Chars(word);
        if (wordChars.size() < 2) {
            continue;
        }
        std::string tail;
        for (size_t k = 1; k < wordChars.size(); ++k) {
            tail += wordChars[k];
        }
        std::string suffix;
        for (size_t k = i + candChars.size(); k < textChars.size(); ++k) {
            suffix += textChars[k];
        }
        if (suffix.size() < tail.size() || suffix.compare(0, tail.size(), tail) != 0) {
            continue;
        }
        return true;
    }
    return false;
}

std::vector<std::string> segmentHanzi(const std::string& hanzi, const EnZhG2pResources& res) {
    const auto chars = utf8Chars(hanzi);
    std::vector<std::string> words;
    size_t i = 0;
    while (i < chars.size()) {
        size_t bestLen = 0;
        std::string bestWord;
        const size_t maxLen = std::min(res.maxLexiconWordLen, chars.size() - i);
        for (size_t len = maxLen; len >= 1; --len) {
            std::string cand;
            for (size_t k = 0; k < len; ++k) {
                cand += chars[i + k];
            }
            if (!res.lexicon.count(cand)) {
                continue;
            }
            if (candidateSplitsFollowingLexiconWord(cand, chars, i, res)) {
                continue;
            }
            bestLen = len;
            bestWord = std::move(cand);
            break;
        }
        if (bestLen == 0) {
            words.push_back(chars[i]);
            ++i;
        } else {
            words.push_back(bestWord);
            i += bestLen;
        }
    }
    return words;
}

std::vector<std::string> lexiconPinyinForWord(const std::string& word, const EnZhG2pResources& res) {
    const auto it = res.lexicon.find(word);
    if (it != res.lexicon.end()) {
        const std::vector<std::string> syllables = normalizeLexiconPinyin(it->second);
        if (!syllables.empty()) {
            return syllables;
        }
    }

    const std::vector<std::string> chars = utf8Chars(word);
    if (chars.size() > 1 && chars[0] == "不") {
        std::string tail;
        for (size_t i = 1; i < chars.size(); ++i) {
            tail += chars[i];
        }
        const std::vector<std::string> tailSyllables = lexiconPinyinForWord(tail, res);
        if (!tailSyllables.empty()) {
            const std::vector<std::string> buSyllables = lexiconPinyinForWord("不", res);
            if (!buSyllables.empty()) {
                std::vector<std::string> combined = buSyllables;
                combined.insert(combined.end(), tailSyllables.begin(), tailSyllables.end());
                return combined;
            }
        }
    }

    std::vector<std::string> syllables;
    for (const std::string& ch : chars) {
        const auto chIt = res.lexicon.find(ch);
        if (chIt == res.lexicon.end()) {
            return {};
        }
        const std::vector<std::string> part = normalizeLexiconPinyin(chIt->second);
        if (part.empty()) {
            return {};
        }
        syllables.insert(syllables.end(), part.begin(), part.end());
    }
    return syllables;
}

std::vector<std::string> hanziToPinyinTokens(const std::string& hanzi, const EnZhG2pResources& res) {
    if (hanzi.empty()) {
        return {};
    }
    std::vector<std::string> words = mergeWords(segmentHanzi(hanzi, res));
    std::vector<std::string> syllables;
    for (const std::string& word : words) {
        std::vector<std::string> py = lexiconPinyinForWord(word, res);
        if (py.empty()) {
            continue;
        }
        if (!syllables.empty()) {
            syllables.push_back("|");
        }
        syllables.insert(syllables.end(), py.begin(), py.end());
    }
    return syllables;
}

void appendEnglishWordRun(std::vector<std::string>& stream,
                          const std::vector<std::string>& words,
                          bool mixedHanzi,
                          const EnZhG2pResources& res) {
    if (words.empty()) {
        return;
    }
    if (mixedHanzi && stream.empty()) {
        stream.push_back("_");
    }
    for (size_t wi = 0; wi < words.size(); ++wi) {
        const std::string* prev = wi > 0 ? &words[wi - 1] : nullptr;
        const std::string* next = wi + 1 < words.size() ? &words[wi + 1] : nullptr;
        appendEnglishArpabetWord(stream, words[wi], prev, next, res);
    }
}

static const std::unordered_map<char, std::string> kToneMark = {
    {'1', "ˉ"}, {'2', "ˊ"}, {'3', "ˇ"}, {'4', "ˋ"}, {'5', "˙"}, {'0', "˙"},
};

static const std::string kBopomofoInitials = "ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙ";

static std::unordered_set<std::string> makeBopomofoInitialSet() {
    std::unordered_set<std::string> out;
    const char* src = kBopomofoInitials.c_str();
    int32_t srcLen = static_cast<int32_t>(kBopomofoInitials.size());
    int32_t i = 0;
    while (i < srcLen) {
        UChar32 c;
        U8_NEXT(src, i, srcLen, c);
        if (c < 0) {
            break;
        }
        char buf[5] = {};
        int32_t j = 0;
        U8_APPEND_UNSAFE(buf, j, c);
        out.emplace(buf, static_cast<size_t>(j));
    }
    return out;
}

static const std::unordered_set<std::string> kBopomofoInitialSet = makeBopomofoInitialSet();

std::pair<std::string, std::string> splitBpmfBody(const std::string& bpmf) {
    const std::vector<std::string> chars = utf8Chars(bpmf);
    size_t idx = 0;
    while (idx < chars.size() && kBopomofoInitialSet.count(chars[idx]) > 0) {
        ++idx;
    }
    std::string initial;
    for (size_t i = 0; i < idx; ++i) {
        initial += chars[i];
    }
    std::string rhyme;
    for (size_t i = idx; i < chars.size(); ++i) {
        rhyme += chars[i];
    }
    if (rhyme.empty()) {
        return {"", bpmf};
    }
    return {initial, rhyme};
}

void appendBpmfRhymeBodyTone(std::vector<std::string>& out,
                             const std::string& bpmf,
                             char toneDigit) {
    const auto markIt = kToneMark.find(toneDigit);
    const std::string toneMark = markIt != kToneMark.end() ? markIt->second : "ˉ";
    const auto [initial, rhyme] = splitBpmfBody(bpmf);
    if (!initial.empty()) {
        out.push_back(initial);
    }
    if (!rhyme.empty()) {
        out.push_back(rhyme);
    }
    out.push_back(toneMark);
}

std::vector<std::string> pinyinStreamToModelTokens(const std::vector<std::string>& tokens,
                                                   const EnZhG2pResources& res) {
    std::vector<std::string> out;
    for (const std::string& syllable : tokens) {
        if (syllable.empty() || syllable == "|") {
            continue;
        }
        if (syllable == "_" || syllable == "/") {
            if (out.empty() || out.back() != "_") {
                out.push_back("_");
            }
            continue;
        }
        if (isPunctuationToken(syllable)) {
            appendPunctuationToken(out, canonicalHalfwidthPunctToken(syllable));
            continue;
        }
        if (isArpabetToken(syllable)) {
            ensureBoundaryBeforeArpabet(out);
            out.push_back(syllable);
            continue;
        }
        if (!isPinyinSyllable(syllable)) {
            if (syllable.size() == 1) {
                appendPunctuationToken(out, syllable);
            } else {
                out.push_back(syllable);
            }
            continue;
        }
        const char tone = syllable.back();
        std::string base = syllable.substr(0, syllable.size() - 1);
        const auto it = res.pinyinToBpmf.find(base);
        if (it != res.pinyinToBpmf.end()) {
            ensureBoundaryAfterArpabet(out);
            appendBpmfRhymeBodyTone(out, it->second, tone);
            continue;
        }
        if (base.size() > 1 && base.back() == 'r') {
            const std::string stem = base.substr(0, base.size() - 1);
            const auto itR = res.pinyinToBpmf.find(stem);
            if (itR != res.pinyinToBpmf.end()) {
                ensureBoundaryAfterArpabet(out);
                appendBpmfRhymeBodyTone(out, itR->second, tone);
                out.push_back("ㄦ");
                out.push_back("˙");
                continue;
            }
        }
        out.push_back(syllable);
    }
    while (!out.empty() && out.back() == "_") {
        out.pop_back();
    }
    return out;
}

std::string joinTokens(const std::vector<std::string>& toks) {
    std::ostringstream oss;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (i) {
            oss << ' ';
        }
        oss << toks[i];
    }
    return oss.str();
}

std::vector<std::string> processMixedTnText(const std::string& text, const EnZhG2pResources& res) {
    const bool mixedHanzi = textContainsHanzi(text);
    std::vector<std::string> pinyinStream;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isspace(ch)) {
            ++i;
            continue;
        }
        if (std::isalpha(ch)) {
            std::vector<std::string> words;
            while (i < n) {
                while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) {
                    ++i;
                }
                if (i >= n || !std::isalpha(static_cast<unsigned char>(text[i]))) {
                    break;
                }
                size_t j = i;
                while (j < n) {
                    unsigned char cj = static_cast<unsigned char>(text[j]);
                    const bool allowDot = !mixedHanzi && cj == '.';
                    if (std::isalpha(cj) || cj == '\'' || cj == '-' || allowDot) {
                        ++j;
                    } else {
                        break;
                    }
                }
                words.push_back(text.substr(i, j - i));
                i = j;
                size_t k = i;
                while (k < n && std::isspace(static_cast<unsigned char>(text[k]))) {
                    ++k;
                }
                if (k < n && std::isalpha(static_cast<unsigned char>(text[k]))) {
                    i = k;
                    continue;
                }
                break;
            }
            appendEnglishWordRun(pinyinStream, words, mixedHanzi, res);
            continue;
        }
        size_t j = i;
        while (j < n) {
            unsigned char cj = static_cast<unsigned char>(text[j]);
            if (!std::isspace(cj) && !std::isalpha(cj)) {
                ++j;
            } else {
                break;
            }
        }
        const std::string segment = text.substr(i, j - i);
        std::string hanziRun;
        const std::vector<std::string> segmentChars = utf8Chars(segment);
        for (size_t ci = 0; ci < segmentChars.size(); ++ci) {
            const std::string& c = segmentChars[ci];
            UChar32 cp;
            int32_t idx = 0;
            U8_NEXT(c.c_str(), idx, static_cast<int32_t>(c.size()), cp);
            if (isHanzi(cp)) {
                hanziRun += c;
                continue;
            }
            if (!hanziRun.empty()) {
                const std::vector<std::string> zhPy = hanziToPinyinTokens(hanziRun, res);
                pinyinStream.insert(pinyinStream.end(), zhPy.begin(), zhPy.end());
                hanziRun.clear();
            }
            if (cp == 0x3002 && ci + 1 < segmentChars.size()) {
                UChar32 nextCp = 0;
                int32_t nextIdx = 0;
                U8_NEXT(segmentChars[ci + 1].c_str(),
                        nextIdx,
                        static_cast<int32_t>(segmentChars[ci + 1].size()),
                        nextCp);
                if (nextCp == '.') {
                    continue;
                }
            }
            if (isZhBreakPunct(cp)) {
                if (cp == 0xFF0C) {
                    continue;
                }
                appendUtf8Codepoint(pinyinStream, canonicalHalfwidthPunct(cp));
            }
        }
        if (!hanziRun.empty()) {
            const std::vector<std::string> zhPy = hanziToPinyinTokens(hanziRun, res);
            pinyinStream.insert(pinyinStream.end(), zhPy.begin(), zhPy.end());
        }
        i = j;
    }
    return pinyinStream;
}

class EnZhG2pBackend : public LanguageBackend {
public:
    bool configure(const nlohmann::json& cfg, const std::string& resDir, std::string& err) override {
        return resources_.load(cfg, resDir, err);
    }

    const TtsCallbacks& callbacks() const override { return cb_; }

    void registerOps(TtsNormalizerEngine& engine) override {
        engine.registerOp("tone3_sandhi", tts_zh_g2p_ops::opTone3Sandhi);
        engine.registerOp("py_tone_mark_to_digit", tts_zh_g2p_ops::opPyToneMarkToDigit);
    }

    bool handlesNormalizationDirectly() const override { return true; }

    std::string normalizeDirect(const std::string& inputUtf8, TtsNormalizerEngine& engine) override {
        const std::vector<std::string> pinyinStream = processMixedTnText(inputUtf8, resources_);
        if (pinyinStream.empty()) {
            return "";
        }
        const std::string sandhiIn = joinTokens(pinyinStream);
        icu::UnicodeString sandhiU = icu::UnicodeString::fromUTF8(sandhiIn);
        sandhiU = engine.runPipeline(sandhiU, cb_);
        std::string sandhiOut;
        sandhiU.toUTF8String(sandhiOut);
        const std::vector<std::string> sandhiToks = splitSpaces(sandhiOut);
        return joinTokens(pinyinStreamToModelTokens(sandhiToks, resources_));
    }

private:
    EnZhG2pResources resources_;
    TtsCallbacks cb_;
};

BackendAutoRegister _reg_en_zh_g2p("en_zh_g2p", [] { return std::unique_ptr<LanguageBackend>(new EnZhG2pBackend()); });

} // namespace
