#include "frontend_ops.hpp"

#include <cctype>
#include <cstring>
#include <functional>
#include <regex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>

namespace frontend {
namespace {

std::unordered_set<char32_t> resolveCharSet(
    const nlohmann::json& resourcesDoc,
    const std::string& name) {
    std::unordered_set<char32_t> out;
    if (!resourcesDoc.contains("resources")) {
        return out;
    }
    const auto& charSets = resourcesDoc["resources"].value("char_sets", nlohmann::json::object());
    if (!charSets.contains(name)) {
        return out;
    }
    const auto& raw = charSets[name];
    auto addUtf8 = [&](const std::string& s) {
        icu::UnicodeString u = icu::UnicodeString::fromUTF8(s);
        for (int32_t i = 0; i < u.length(); i = u.moveIndex32(i, 1)) {
            out.insert(static_cast<char32_t>(u.char32At(i)));
        }
    };
    if (raw.is_string()) {
        addUtf8(raw.get<std::string>());
    } else if (raw.is_array()) {
        for (const auto& item : raw) {
            if (item.is_string()) {
                addUtf8(item.get<std::string>());
            }
        }
    }
    return out;
}

std::unordered_map<char32_t, char32_t> resolveCharMap(
    const nlohmann::json& resourcesDoc,
    const std::string& name) {
    std::unordered_map<char32_t, char32_t> out;
    if (!resourcesDoc.contains("resources")) {
        return out;
    }
    const auto& maps = resourcesDoc["resources"].value("char_maps", nlohmann::json::object());
    if (!maps.contains(name) || !maps[name].is_object()) {
        return out;
    }
    for (auto it = maps[name].begin(); it != maps[name].end(); ++it) {
        icu::UnicodeString from = icu::UnicodeString::fromUTF8(it.key());
        icu::UnicodeString to = icu::UnicodeString::fromUTF8(it.value().get<std::string>());
        if (from.countChar32() == 1 && to.countChar32() == 1) {
            out[static_cast<char32_t>(from.char32At(0))] = static_cast<char32_t>(to.char32At(0));
        }
    }
    return out;
}

std::string resolveConstant(const nlohmann::json& resourcesDoc, const std::string& name) {
    if (!resourcesDoc.contains("resources")) {
        return "";
    }
    const auto& constants = resourcesDoc["resources"].value("constants", nlohmann::json::object());
    if (!constants.contains(name)) {
        return "";
    }
    return constants[name].get<std::string>();
}

std::vector<std::string> utf8Chars(const std::string& s) {
    std::vector<std::string> out;
    icu::UnicodeString u = icu::UnicodeString::fromUTF8(s);
    for (int32_t i = 0; i < u.length();) {
        UChar32 c = u.char32At(i);
        icu::UnicodeString one(c);
        std::string ch;
        one.toUTF8String(ch);
        out.push_back(ch);
        i = u.moveIndex32(i, 1);
    }
    return out;
}

bool isCjkChar(const std::string& ch) {
    icu::UnicodeString u = icu::UnicodeString::fromUTF8(ch);
    if (u.length() != 1) {
        return false;
    }
    UChar32 c = u.char32At(0);
    return c >= 0x4E00 && c <= 0x9FFF;
}

std::string nfkcNormalize(const std::string& text) {
    icu::UnicodeString u = icu::UnicodeString::fromUTF8(text);
    UErrorCode status = U_ZERO_ERROR;
    const icu::Normalizer2* norm = icu::Normalizer2::getNFKCInstance(status);
    if (U_FAILURE(status)) {
        return text;
    }
    icu::UnicodeString out;
    norm->normalize(u, out, status);
    if (U_FAILURE(status)) {
        return text;
    }
    std::string utf8;
    out.toUTF8String(utf8);
    return utf8;
}

const std::regex& literalLineBreakEscapeRe() {
    static const std::regex re(R"(\\r\\n|\\n|\\r)");
    return re;
}

const std::regex& whitespaceRe() {
    static const std::regex re(R"(\s+)");
    return re;
}

bool isEmojiCodepoint(UChar32 c) {
    return (c >= 0x1F300 && c <= 0x1FAFF) || (c >= 0x2700 && c <= 0x27BF) ||
           (c >= 0x2600 && c <= 0x26FF) || (c >= 0x2300 && c <= 0x23FF) ||
           (c >= 0x1F600 && c <= 0x1F64F) || (c >= 0x1F680 && c <= 0x1F6FF) ||
           (c >= 0x1F1E0 && c <= 0x1F1FF) || (c >= 0xE0020 && c <= 0xE007F);
}

bool isControlCodepoint(UChar32 c) {
    if (c <= 0x1F || c == 0x7F || (c >= 0x80 && c <= 0x9F)) {
        return true;
    }
    return (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) ||
           (c >= 0x2066 && c <= 0x2069) || c == 0xFEFF || (c >= 0xFFF9 && c <= 0xFFFB);
}

std::string filterCodepoints(
    const std::string& text,
    const std::function<bool(UChar32)>& drop) {
    icu::UnicodeString u = icu::UnicodeString::fromUTF8(text);
    icu::UnicodeString out;
    for (int32_t i = 0; i < u.length();) {
        UChar32 c = u.char32At(i);
        if (!drop(c)) {
            out.append(c);
        }
        i = u.moveIndex32(i, 1);
    }
    std::string utf8;
    out.toUTF8String(utf8);
    return utf8;
}

std::string stripEmojis(std::string text) {
    return filterCodepoints(text, isEmojiCodepoint);
}

std::string stripLiteralLineBreakEscapes(std::string text) {
    return std::regex_replace(text, literalLineBreakEscapeRe(), " ");
}

std::string expandLiteralLineBreakEscapes(std::string text) {
    return std::regex_replace(text, literalLineBreakEscapeRe(), "\n");
}

std::string stripTrailingBackslash(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '\\') {
        text.pop_back();
    }
    return text;
}

std::string collapseWhitespace(std::string text) {
    text = std::regex_replace(text, whitespaceRe(), " ");
    auto begin = text.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return "";
    }
    auto end = text.find_last_not_of(' ');
    return text.substr(begin, end - begin + 1);
}

std::string stripControlChars(std::string text) {
    return filterCodepoints(text, isControlCodepoint);
}

std::string stripMarkdownCodeTicks(std::string text) {
    for (const char* pat : {"```", "`"}) {
        size_t pos = 0;
        const size_t n = std::strlen(pat);
        while ((pos = text.find(pat, pos)) != std::string::npos) {
            text.replace(pos, n, pat[0] == '`' && n == 3 ? " " : " ");
            pos += 1;
        }
    }
    return text;
}

std::string expandIdentifierUnderscores(std::string text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '_' && i > 0 && i + 1 < text.size()) {
            const unsigned char prev = static_cast<unsigned char>(text[i - 1]);
            const unsigned char next = static_cast<unsigned char>(text[i + 1]);
            if (std::isalnum(prev) && std::isalnum(next)) {
                out += " underscore ";
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

std::string normalizeProgrammingTokensEn(std::string text) {
    static const std::regex cppRe(R"(C\+\+)");
    static const std::regex csRe(R"(C#)");
    text = std::regex_replace(text, cppRe, "C plus plus");
    text = std::regex_replace(text, csRe, "C sharp");
    return text;
}

std::string splitSqlGluedIdentifiers(std::string text) {
    static const std::pair<std::regex, std::string> rules[] = {
        {std::regex(R"(\bTRUNCDATE\b)", std::regex::icase), "TRUNC DATE"},
        {std::regex(R"(\bTRUNCMONTH\b)", std::regex::icase), "TRUNC MONTH"},
        {std::regex(R"(\bTRUNCYEAR\b)", std::regex::icase), "TRUNC YEAR"},
        {std::regex(R"(\bGROUPBY\b)", std::regex::icase), "GROUP BY"},
        {std::regex(R"(\bORDERBY\b)", std::regex::icase), "ORDER BY"},
    };
    for (const auto& rule : rules) {
        text = std::regex_replace(text, rule.first, rule.second);
    }
    return text;
}

std::string collapseEmptySlashSegments(std::string text) {
    text = std::regex_replace(text, std::regex(R"((?:\s*/\s*){2,})"), " / ");
    text = std::regex_replace(text, std::regex(R"(\s*/\s*$)"), "");
    return text;
}

std::string normalizeNumericDashRanges(std::string text) {
    static const std::regex re(R"((\d{1,2})\s*[-–—]\s*(\d{1,2}))");
    return std::regex_replace(text, re, "$1 to $2");
}

bool needsClauseBreakPunct(const std::string& segment, const std::unordered_set<char32_t>& acceptable) {
    std::string s = segment;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    if (s.size() >= 3 && s.substr(s.size() - 3) == "...") {
        return false;
    }
    if (s.empty()) {
        return false;
    }
    icu::UnicodeString u = icu::UnicodeString::fromUTF8(std::string(1, s.back()));
    char32_t ch = static_cast<char32_t>(u.char32At(u.length() - 1));
    return acceptable.count(ch) == 0;
}

std::string newlineClauseBreakChar(const std::string& segment) {
    auto chars = utf8Chars(segment);
    while (!chars.empty() && chars.back() == " ") {
        chars.pop_back();
    }
    if (!chars.empty() && isCjkChar(chars.back())) {
        return "。";
    }
    return ".";
}

std::string normalizeClauseBreakPunct(
    std::string text,
    const std::unordered_set<char32_t>& acceptable) {
    static const std::regex nlRe(R"(\r\n|\r|\n)");
    static const std::regex paraRe(R"([ \t]{2,})");
    for (const auto* splitPat : {&nlRe, &paraRe}) {
        std::sregex_token_iterator it(text.begin(), text.end(), *splitPat, -1);
        std::sregex_token_iterator end;
        std::vector<std::string> parts(it, end);
        if (parts.size() <= 1) {
            continue;
        }
        std::string merged;
        for (size_t i = 0; i < parts.size(); ++i) {
            std::string segment = parts[i];
            while (!segment.empty() && std::isspace(static_cast<unsigned char>(segment.back()))) {
                segment.pop_back();
            }
            if (i + 1 < parts.size() && !segment.empty() &&
                needsClauseBreakPunct(segment, acceptable)) {
                segment += newlineClauseBreakChar(segment);
            }
            if (!segment.empty()) {
                if (!merged.empty()) {
                    merged += ' ';
                }
                merged += segment;
            }
        }
        text = merged;
    }

    std::string out;
    const auto chars = utf8Chars(text);
    size_t i = 0;
    while (i < chars.size()) {
        if (chars[i] != " ") {
            out += chars[i];
            ++i;
            continue;
        }
        size_t j = i;
        while (j < chars.size() && chars[j] == " ") {
            ++j;
        }
        std::string left = out;
        std::string right;
        for (size_t k = j; k < chars.size(); ++k) {
            right += chars[k];
        }
        if (!left.empty() && !right.empty()) {
            auto lchars = utf8Chars(left);
            auto rchars = utf8Chars(right);
            while (!lchars.empty() && lchars.back() == " ") {
                lchars.pop_back();
            }
            while (!rchars.empty() && rchars.front() == " ") {
                rchars.erase(rchars.begin());
            }
            if (!lchars.empty() && !rchars.empty() && isCjkChar(lchars.back()) && isCjkChar(rchars.front())) {
                char32_t lc = 0;
                icu::UnicodeString lu = icu::UnicodeString::fromUTF8(lchars.back());
                lc = lu.char32At(0);
                if (acceptable.count(lc) == 0) {
                    out += "，";
                }
            }
        }
        out += ' ';
        i = j;
    }
    return out;
}

std::string stripStructuralQuotes(
    std::string text,
    const std::unordered_set<char32_t>& singleQuotes,
    const std::unordered_set<char32_t>& jpQuotes,
    const std::unordered_set<char32_t>& doubleQuotes) {
    std::string out;
    const auto chars = utf8Chars(text);
    for (size_t i = 0; i < chars.size(); ++i) {
        icu::UnicodeString u = icu::UnicodeString::fromUTF8(chars[i]);
        char32_t ch = static_cast<char32_t>(u.char32At(0));
        if (jpQuotes.count(ch) || doubleQuotes.count(ch)) {
            continue;
        }
        if (singleQuotes.count(ch)) {
            bool prevLetter = i > 0 && std::isalpha(static_cast<unsigned char>(chars[i - 1][0]));
            bool nextLetter = i + 1 < chars.size() && std::isalpha(static_cast<unsigned char>(chars[i + 1][0]));
            if (prevLetter && nextLetter) {
                out += chars[i];
            }
            continue;
        }
        out += chars[i];
    }
    return out;
}

std::string translateChars(const std::string& text, const std::unordered_map<char32_t, char32_t>& charMap) {
    std::string out;
    for (const auto& ch : utf8Chars(text)) {
        icu::UnicodeString u = icu::UnicodeString::fromUTF8(ch);
        char32_t c = static_cast<char32_t>(u.char32At(0));
        auto it = charMap.find(c);
        if (it != charMap.end()) {
            icu::UnicodeString repl(static_cast<UChar32>(it->second));
            std::string replUtf8;
            repl.toUTF8String(replUtf8);
            out += replUtf8;
        } else {
            out += ch;
        }
    }
    return out;
}

std::string removeDashes(std::string text, const std::unordered_set<char32_t>& dashChars) {
    for (const auto& ch : utf8Chars(text)) {
        icu::UnicodeString u = icu::UnicodeString::fromUTF8(ch);
        char32_t c = static_cast<char32_t>(u.char32At(0));
        if (dashChars.count(c)) {
            for (size_t i = 0; i < text.size();) {
                if (text.compare(i, ch.size(), ch) == 0) {
                    text.replace(i, ch.size(), " ");
                    i += 1;
                } else {
                    ++i;
                }
            }
        }
    }
    return text;
}

std::string dedupeTrailingPunctuation(std::string text, const std::unordered_set<char32_t>& exempt) {
    size_t end = text.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    size_t start = end;
    while (start > 0) {
        icu::UnicodeString u = icu::UnicodeString::fromUTF8(text.substr(start - 1, 1));
        if (u.length() != 1) {
            break;
        }
        char32_t ch = static_cast<char32_t>(u.char32At(0));
        if (std::isspace(static_cast<unsigned char>(text[start - 1])) || exempt.count(ch)) {
            break;
        }
        if (std::isalnum(static_cast<unsigned char>(text[start - 1])) ||
            text[start - 1] == '_') {
            break;
        }
        --start;
    }
    if (start >= end) {
        return text;
    }
    std::vector<char32_t> punct;
    for (size_t i = start; i < end;) {
        size_t len = 1;
        if ((text[i] & 0xE0) == 0xC0) {
            len = 2;
        } else if ((text[i] & 0xF0) == 0xE0) {
            len = 3;
        } else if ((text[i] & 0xF8) == 0xF0) {
            len = 4;
        }
        icu::UnicodeString u = icu::UnicodeString::fromUTF8(text.substr(i, len));
        char32_t ch = static_cast<char32_t>(u.char32At(0));
        if (!exempt.count(ch)) {
            punct.push_back(ch);
        }
        i += len;
    }
    if (punct.size() <= 1) {
        return text;
    }
  bool allSame = true;
    for (size_t k = 1; k < punct.size(); ++k) {
        if (punct[k] != punct[0]) {
            allSame = false;
            break;
        }
    }
    if (!allSame) {
        return text;
    }
    icu::UnicodeString one(static_cast<UChar32>(punct[0]));
    std::string oneUtf8;
    one.toUTF8String(oneUtf8);
    return text.substr(0, start) + oneUtf8 + text.substr(end);
}

std::string ensureTrailingSentencePunct(
    std::string text,
    const std::unordered_set<char32_t>& acceptable) {
    std::string stripped = text;
    while (!stripped.empty() && std::isspace(static_cast<unsigned char>(stripped.back()))) {
        stripped.pop_back();
    }
    if (stripped.empty()) {
        return text;
    }
    if (stripped.size() >= 3 && stripped.substr(stripped.size() - 3) == "...") {
        return text;
    }
    icu::UnicodeString u = icu::UnicodeString::fromUTF8(std::string(1, stripped.back()));
    char32_t last = static_cast<char32_t>(u.char32At(0));
    if (acceptable.count(last)) {
        return text;
    }
    if (std::isalnum(static_cast<unsigned char>(stripped.back()))) {
        return stripped + " .";
    }
    return stripped + ".";
}

std::string normalizeEllipsis(std::string text, const std::vector<std::string>& aliases, const std::string& target) {
    for (const auto& alias : aliases) {
        size_t pos = 0;
        while ((pos = text.find(alias, pos)) != std::string::npos) {
            text.replace(pos, alias.size(), target);
            pos += target.size();
        }
    }
    return text;
}

std::string replaceVerticalLinePunct(
    std::string text,
    const std::unordered_set<char32_t>& sourceChars,
    const std::string& replacement) {
    for (const auto& ch : utf8Chars(text)) {
        icu::UnicodeString u = icu::UnicodeString::fromUTF8(ch);
        if (sourceChars.count(static_cast<char32_t>(u.char32At(0)))) {
            size_t pos = 0;
            while ((pos = text.find(ch, pos)) != std::string::npos) {
                text.replace(pos, ch.size(), replacement);
                pos += replacement.size();
            }
        }
    }
    if (!replacement.empty()) {
        std::string doubled = replacement + replacement;
        size_t pos = 0;
        while ((pos = text.find(doubled, pos)) != std::string::npos) {
            text.replace(pos, doubled.size(), replacement);
            pos += replacement.size();
        }
    }
    return text;
}

bool isEnglishApostrophe(const std::string& text, size_t byteIndex) {
    if (byteIndex >= text.size()) {
        return false;
    }
    char ch = text[byteIndex];
    if (ch != '\'' && static_cast<unsigned char>(ch) != 0xE2) {
        return false;
    }
    return byteIndex > 0 && std::isalpha(static_cast<unsigned char>(text[byteIndex - 1])) &&
           byteIndex + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[byteIndex + 1]));
}

std::string stripZhStructuralPunct(
    std::string text,
    const std::unordered_set<char32_t>& structural,
    const std::unordered_set<char32_t>& acceptable) {
    std::string out;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        char ch = text[i];
        if (ch == '(') {
            size_t close = text.find(')', i + 1);
            if (close != std::string::npos) {
                bool hasContent = false;
                for (size_t k = i + 1; k < close; ++k) {
                    icu::UnicodeString u = icu::UnicodeString::fromUTF8(std::string(1, text[k]));
                    char32_t c = static_cast<char32_t>(u.char32At(0));
                    if (!structural.count(c) && !std::isspace(static_cast<unsigned char>(text[k]))) {
                        hasContent = true;
                        break;
                    }
                }
                if (hasContent && needsClauseBreakPunct(out, acceptable)) {
                    out += "，";
                }
            }
            ++i;
            continue;
        }
        if (ch == ')') {
            if (needsClauseBreakPunct(out, acceptable)) {
                size_t j = i + 1;
                while (j < n && std::isspace(static_cast<unsigned char>(text[j]))) {
                    ++j;
                }
                if (j < n) {
                    icu::UnicodeString u = icu::UnicodeString::fromUTF8(std::string(1, text[j]));
                    char32_t next = static_cast<char32_t>(u.char32At(0));
                    if (!acceptable.count(next) && !structural.count(next)) {
                        out += "，";
                    }
                }
            }
            ++i;
            continue;
        }
        bool skip = false;
        if (i < n) {
            size_t len = 1;
            if ((text[i] & 0xE0) == 0xC0) {
                len = 2;
            } else if ((text[i] & 0xF0) == 0xE0) {
                len = 3;
            }
            icu::UnicodeString u = icu::UnicodeString::fromUTF8(text.substr(i, len));
            char32_t c = static_cast<char32_t>(u.char32At(0));
            if (structural.count(c)) {
                if (isEnglishApostrophe(text, i)) {
                    out += text.substr(i, len);
                }
                skip = true;
                i += len;
            }
        }
        if (!skip) {
            out += text[i];
            ++i;
        }
    }
    return out;
}

std::string stripMarkdownAsterisks(std::string text) {
    text = std::regex_replace(text, std::regex(R"(\*\*([^*]+)\*\*)"), "$1");
    text = std::regex_replace(text, std::regex(R"(\*([^*]+)\*)"), "$1");
    text = std::regex_replace(text, std::regex(R"(\(\s*\*\s*\))"), "()");
    return text;
}

std::string resolvePatternDef(const nlohmann::json& resourcesDoc, const std::string& name) {
    if (!resourcesDoc.contains("resources")) {
        return "";
    }
    const auto& defs = resourcesDoc["resources"].value("pattern_defs", nlohmann::json::object());
    if (!defs.contains(name) || !defs[name].is_string()) {
        return "";
    }
    return defs[name].get<std::string>();
}

std::string collapseLetterDotAbbrevs(const std::string& text, const std::string& pattern) {
    if (text.empty() || pattern.empty()) {
        return text;
    }
    const std::regex re(pattern);
    std::string out;
    out.reserve(text.size());
    auto it = std::sregex_iterator(text.begin(), text.end(), re);
    const std::sregex_iterator end;
    size_t pos = 0;
    for (; it != end; ++it) {
        const std::smatch& m = *it;
        out.append(text, pos, static_cast<size_t>(m.position()) - pos);
        for (char ch : m.str()) {
            if (std::isalpha(static_cast<unsigned char>(ch))) {
                out += ch;
            }
        }
        pos = static_cast<size_t>(m.position() + m.length());
    }
    out.append(text, pos, std::string::npos);
    return out;
}

std::string replaceAllLiteral(
    std::string text,
    const std::string& from,
    const std::string& to) {
    if (from.empty()) {
        return text;
    }
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string replaceCharSet(
    const std::string& text,
    const std::unordered_set<char32_t>& chars,
    const std::string& replacement) {
    std::string out;
    out.reserve(text.size());
    for (const auto& ch : utf8Chars(text)) {
        icu::UnicodeString u = icu::UnicodeString::fromUTF8(ch);
        if (chars.count(static_cast<char32_t>(u.char32At(0)))) {
            out += replacement;
        } else {
            out += ch;
        }
    }
    return out;
}

std::string collapseLiteralRuns(std::string text, const std::string& literal) {
    if (literal.empty()) {
        return text;
    }
    std::string out;
    out.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size()) {
        if (text.compare(pos, literal.size(), literal) != 0) {
            out.push_back(text[pos]);
            ++pos;
            continue;
        }
        out += literal;
        pos += literal.size();
        while (pos < text.size() && text.compare(pos, literal.size(), literal) == 0) {
            pos += literal.size();
        }
    }
    return out;
}

std::string trimSpaces(std::string text) {
    const size_t begin = text.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return "";
    }
    const size_t end = text.find_last_not_of(' ');
    return text.substr(begin, end - begin + 1);
}

bool getResource(
    const nlohmann::json& resourcesDoc,
    const std::string& bucket,
    const std::string& name,
    const nlohmann::json*& value,
    std::string& errOut) {
    if (!resourcesDoc.contains("resources") || !resourcesDoc["resources"].is_object()) {
        errOut = "missing object resource root 'resources'";
        return false;
    }
    const auto& resources = resourcesDoc["resources"];
    if (!resources.contains(bucket) || !resources[bucket].is_object()) {
        errOut = "missing object resource bucket 'resources." + bucket + "'";
        return false;
    }
    const auto& values = resources[bucket];
    if (!values.contains(name)) {
        errOut = "missing resource 'resources." + bucket + "." + name + "'";
        return false;
    }
    value = &values[name];
    return true;
}

bool getStringParam(
    const nlohmann::json& params,
    const std::string& key,
    std::string& value,
    std::string& errOut) {
    if (!params.contains(key)) {
        errOut = "missing required string parameter '" + key + "'";
        return false;
    }
    if (!params[key].is_string()) {
        errOut = "parameter '" + key + "' must be a string";
        return false;
    }
    value = params[key].get<std::string>();
    return true;
}

bool validateOptionalStringParam(
    const nlohmann::json& params,
    const std::string& key,
    std::string& errOut) {
    if (params.contains(key) && !params[key].is_string()) {
        errOut = "parameter '" + key + "' must be a string";
        return false;
    }
    return true;
}

bool validateCharSetResource(
    const nlohmann::json& resourcesDoc,
    const std::string& name,
    std::string& errOut) {
    const nlohmann::json* value = nullptr;
    if (!getResource(resourcesDoc, "char_sets", name, value, errOut)) {
        return false;
    }
    if (value->is_string()) {
        return true;
    }
    if (!value->is_array()) {
        errOut = "resource 'resources.char_sets." + name + "' must be a string or string array";
        return false;
    }
    for (const auto& item : *value) {
        if (!item.is_string()) {
            errOut = "resource 'resources.char_sets." + name + "' must contain only strings";
            return false;
        }
    }
    return true;
}

bool validateCharMapResource(
    const nlohmann::json& resourcesDoc,
    const std::string& name,
    std::string& errOut) {
    const nlohmann::json* value = nullptr;
    if (!getResource(resourcesDoc, "char_maps", name, value, errOut)) {
        return false;
    }
    if (!value->is_object()) {
        errOut = "resource 'resources.char_maps." + name + "' must be an object";
        return false;
    }
    for (auto it = value->begin(); it != value->end(); ++it) {
        if (!it.value().is_string()) {
            errOut = "resource 'resources.char_maps." + name + "' values must be strings";
            return false;
        }
        const icu::UnicodeString from = icu::UnicodeString::fromUTF8(it.key());
        const icu::UnicodeString to =
            icu::UnicodeString::fromUTF8(it.value().get<std::string>());
        if (from.countChar32() != 1 || to.countChar32() != 1) {
            errOut = "resource 'resources.char_maps." + name +
                     "' keys and values must each contain exactly one Unicode code point";
            return false;
        }
    }
    return true;
}

bool validateConstantResource(
    const nlohmann::json& resourcesDoc,
    const std::string& name,
    std::string& errOut) {
    const nlohmann::json* value = nullptr;
    if (!getResource(resourcesDoc, "constants", name, value, errOut)) {
        return false;
    }
    if (!value->is_string()) {
        errOut = "resource 'resources.constants." + name + "' must be a string";
        return false;
    }
    return true;
}

bool validatePatternResource(
    const nlohmann::json& resourcesDoc,
    const std::string& name,
    std::string& errOut) {
    const nlohmann::json* value = nullptr;
    if (!getResource(resourcesDoc, "pattern_defs", name, value, errOut)) {
        return false;
    }
    if (!value->is_string()) {
        errOut = "resource 'resources.pattern_defs." + name + "' must be a string";
        return false;
    }
    try {
        (void)std::regex(value->get<std::string>());
    } catch (const std::regex_error& e) {
        errOut = "resource 'resources.pattern_defs." + name + "' is not a valid regex: " +
                 std::string(e.what());
        return false;
    }
    return true;
}

bool validateNamedCharSetParam(
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params,
    const std::string& paramName,
    const std::string& defaultName,
    std::string& errOut) {
    if (!validateOptionalStringParam(params, paramName, errOut)) {
        return false;
    }
    return validateCharSetResource(resourcesDoc, params.value(paramName, defaultName), errOut);
}

bool validateRequiredCharSetParam(
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params,
    const std::string& paramName,
    std::string& errOut) {
    std::string name;
    return getStringParam(params, paramName, name, errOut) &&
           validateCharSetResource(resourcesDoc, name, errOut);
}

std::regex_constants::syntax_option_type regexFlags(const nlohmann::json& params) {
    auto flags = std::regex_constants::ECMAScript;
    if (!params.contains("flags")) {
        return flags;
    }
    const auto& raw = params["flags"];
    if (raw.is_string() && raw.get<std::string>() == "icase") {
        flags |= std::regex_constants::icase;
    } else if (raw.is_array()) {
        for (const auto& item : raw) {
            if (item.is_string() && item.get<std::string>() == "icase") {
                flags |= std::regex_constants::icase;
            }
        }
    }
    return flags;
}

bool validateRegexFlags(const nlohmann::json& params, std::string& errOut) {
    if (!params.contains("flags")) {
        return true;
    }
    const auto& raw = params["flags"];
    if (raw.is_string()) {
        if (raw.get<std::string>().empty() || raw.get<std::string>() == "icase") {
            return true;
        }
        errOut = "parameter 'flags' supports only 'icase'";
        return false;
    }
    if (raw.is_array()) {
        for (const auto& item : raw) {
            if (!item.is_string() || item.get<std::string>() != "icase") {
                errOut = "parameter 'flags' array supports only the string 'icase'";
                return false;
            }
        }
        return true;
    }
    errOut = "parameter 'flags' must be the string 'icase' or an array containing it";
    return false;
}

std::vector<std::string> resolveReplaceManyValues(
    const nlohmann::json& resourcesDoc,
    const std::string& name) {
    const auto& raw = resourcesDoc["resources"]["char_sets"][name];
    if (raw.is_array()) {
        std::vector<std::string> out;
        out.reserve(raw.size());
        for (const auto& item : raw) {
            out.push_back(item.get<std::string>());
        }
        return out;
    }
    return utf8Chars(raw.get<std::string>());
}

std::string replaceManyValuesResourceName(const nlohmann::json& params) {
    const char* key = params.contains("values") ? "values" : "aliases";
    return params.at(key).get<std::string>();
}

std::string resolveReplaceManyReplacement(
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params) {
    if (params.contains("replacement")) {
        return params.at("replacement").get<std::string>();
    }
    const char* key = params.contains("replacement_constant")
        ? "replacement_constant"
        : "target";
    return resolveConstant(resourcesDoc, params.at(key).get<std::string>());
}

using FrontendOpHandler = std::function<std::string(
    const std::string&,
    const nlohmann::json&,
    const nlohmann::json&)>;
using FrontendOpValidator = std::function<bool(
    const nlohmann::json&,
    const nlohmann::json&,
    std::string&)>;

struct FrontendOpDefinition {
    FrontendOpHandler handler;
    FrontendOpValidator validator;
};

bool validateNoConfig(
    const nlohmann::json&,
    const nlohmann::json&,
    std::string&) {
    return true;
}

const std::unordered_map<std::string, FrontendOpDefinition>& frontendOpRegistry() {
    static const std::unordered_map<std::string, FrontendOpDefinition> registry = {
        {"nfkc_normalize", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return nfkcNormalize(text);
            },
            validateNoConfig,
        }},
        {"strip_emojis", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return stripEmojis(text);
            },
            validateNoConfig,
        }},
        {"strip_literal_line_break_escapes", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return stripLiteralLineBreakEscapes(text);
            },
            validateNoConfig,
        }},
        {"strip_trailing_backslash", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return stripTrailingBackslash(text);
            },
            validateNoConfig,
        }},
        {"expand_literal_line_break_escapes", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return expandLiteralLineBreakEscapes(text);
            },
            validateNoConfig,
        }},
        {"normalize_clause_break_punct", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return normalizeClauseBreakPunct(
                    text,
                    resolveCharSet(resources, params.value("acceptable_trailing", "acceptable_trailing_punct")));
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                return validateNamedCharSetParam(
                    resources, params, "acceptable_trailing", "acceptable_trailing_punct", err);
            },
        }},
        {"strip_structural_quotes", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return stripStructuralQuotes(
                    text,
                    resolveCharSet(resources, params.value("single_quote_chars", "structural_single_quote_chars")),
                    resolveCharSet(resources, params.value("jp_quote_chars", "jp_single_quote_chars")),
                    resolveCharSet(resources, params.value("double_quote_chars", "structural_double_quote_chars")));
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                return validateNamedCharSetParam(
                           resources, params, "single_quote_chars", "structural_single_quote_chars", err) &&
                       validateNamedCharSetParam(
                           resources, params, "jp_quote_chars", "jp_single_quote_chars", err) &&
                       validateNamedCharSetParam(
                           resources, params, "double_quote_chars", "structural_double_quote_chars", err);
            },
        }},
        {"strip_structural_single_quotes", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return stripStructuralQuotes(
                    text,
                    resolveCharSet(resources, params.value("quote_chars", "structural_single_quote_chars")),
                    resolveCharSet(resources, params.value("jp_quote_chars", "jp_single_quote_chars")),
                    {});
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                return validateNamedCharSetParam(
                           resources, params, "quote_chars", "structural_single_quote_chars", err) &&
                       validateNamedCharSetParam(
                           resources, params, "jp_quote_chars", "jp_single_quote_chars", err);
            },
        }},
        {"collapse_empty_slash_segments", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return collapseEmptySlashSegments(text);
            },
            validateNoConfig,
        }},
        {"normalize_numeric_dash_ranges", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return normalizeNumericDashRanges(text);
            },
            validateNoConfig,
        }},
        {"collapse_letter_dot_abbrevs", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                const std::string patternName = params.value("pattern", "letter_dot_abbrev");
                std::string pattern = resolvePatternDef(resources, patternName);
                if (pattern.empty()) {
                    pattern = R"(\b(?:[A-Z][a-z]?\.)+(?:[A-Z][a-z]?)?\.)";
                }
                return collapseLetterDotAbbrevs(text, pattern);
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                if (!validateOptionalStringParam(params, "pattern", err)) {
                    return false;
                }
                return validatePatternResource(resources, params.value("pattern", "letter_dot_abbrev"), err);
            },
        }},
        {"replace_vertical_line_punct", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return replaceVerticalLinePunct(
                    text,
                    resolveCharSet(resources, params.at("char_set").get<std::string>()),
                    params.value("replacement", "，"));
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                return validateRequiredCharSetParam(resources, params, "char_set", err) &&
                       validateOptionalStringParam(params, "replacement", err);
            },
        }},
        {"remove_dashes", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return removeDashes(
                    text, resolveCharSet(resources, params.at("char_set").get<std::string>()));
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                return validateRequiredCharSetParam(resources, params, "char_set", err);
            },
        }},
        {"collapse_whitespace", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return collapseWhitespace(text);
            },
            validateNoConfig,
        }},
        {"dedupe_trailing_punctuation", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return dedupeTrailingPunctuation(
                    text,
                    resolveCharSet(resources, params.value("exempt_chars", "dedupe_punct_exempt")));
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                return validateNamedCharSetParam(
                    resources, params, "exempt_chars", "dedupe_punct_exempt", err);
            },
        }},
        {"ensure_trailing_sentence_punct", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return ensureTrailingSentencePunct(
                    text,
                    resolveCharSet(resources, params.value("acceptable_trailing", "acceptable_trailing_punct")));
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                return validateNamedCharSetParam(
                    resources, params, "acceptable_trailing", "acceptable_trailing_punct", err);
            },
        }},
        {"normalize_ellipsis", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return normalizeEllipsis(
                    text,
                    resolveReplaceManyValues(resources, params.at("aliases").get<std::string>()),
                    resolveConstant(resources, params.at("target").get<std::string>()));
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                std::string aliases;
                std::string target;
                return getStringParam(params, "aliases", aliases, err) &&
                       validateCharSetResource(resources, aliases, err) &&
                       getStringParam(params, "target", target, err) &&
                       validateConstantResource(resources, target, err);
            },
        }},
        {"strip_markdown_asterisks", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return stripMarkdownAsterisks(text);
            },
            validateNoConfig,
        }},
        {"strip_markdown_code_ticks", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return stripMarkdownCodeTicks(text);
            },
            validateNoConfig,
        }},
        {"expand_identifier_underscores", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return expandIdentifierUnderscores(text);
            },
            validateNoConfig,
        }},
        {"normalize_programming_tokens_en", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return normalizeProgrammingTokensEn(text);
            },
            validateNoConfig,
        }},
        {"split_sql_glued_identifiers", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return splitSqlGluedIdentifiers(text);
            },
            validateNoConfig,
        }},
        {"strip_control_chars", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return stripControlChars(text);
            },
            validateNoConfig,
        }},
        {"translate_chars", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return translateChars(
                    text, resolveCharMap(resources, params.at("char_map").get<std::string>()));
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                std::string name;
                return getStringParam(params, "char_map", name, err) &&
                       validateCharMapResource(resources, name, err);
            },
        }},
        {"strip_zh_structural_punct", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return stripZhStructuralPunct(
                    text,
                    resolveCharSet(resources, params.value("structural_chars", "structural_punct_chars")),
                    resolveCharSet(resources, params.value("acceptable_trailing", "acceptable_trailing_punct")));
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                return validateNamedCharSetParam(
                           resources, params, "structural_chars", "structural_punct_chars", err) &&
                       validateNamedCharSetParam(
                           resources, params, "acceptable_trailing", "acceptable_trailing_punct", err);
            },
        }},
        {"regex_replace", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json& params) {
                return std::regex_replace(
                    text,
                    std::regex(params.at("pattern").get<std::string>(), regexFlags(params)),
                    params.at("replacement").get<std::string>());
            },
            [](const nlohmann::json&, const nlohmann::json& params, std::string& err) {
                std::string pattern;
                std::string replacement;
                if (!getStringParam(params, "pattern", pattern, err) ||
                    !getStringParam(params, "replacement", replacement, err) ||
                    !validateRegexFlags(params, err)) {
                    return false;
                }
                try {
                    (void)std::regex(pattern, regexFlags(params));
                } catch (const std::regex_error& e) {
                    err = "parameter 'pattern' is not a valid regex: " + std::string(e.what());
                    return false;
                }
                return true;
            },
        }},
        {"literal_replace", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json& params) {
                return replaceAllLiteral(
                    text,
                    params.at("from").get<std::string>(),
                    params.at("to").get<std::string>());
            },
            [](const nlohmann::json&, const nlohmann::json& params, std::string& err) {
                std::string from;
                std::string to;
                if (!getStringParam(params, "from", from, err) ||
                    !getStringParam(params, "to", to, err)) {
                    return false;
                }
                if (from.empty()) {
                    err = "parameter 'from' must not be empty";
                    return false;
                }
                return true;
            },
        }},
        {"replace_char_set", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                return replaceCharSet(
                    text,
                    resolveCharSet(resources, params.at("char_set").get<std::string>()),
                    params.at("replacement").get<std::string>());
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                std::string replacement;
                return validateRequiredCharSetParam(resources, params, "char_set", err) &&
                       getStringParam(params, "replacement", replacement, err);
            },
        }},
        {"collapse_literal_runs", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json& params) {
                return collapseLiteralRuns(text, params.at("literal").get<std::string>());
            },
            [](const nlohmann::json&, const nlohmann::json& params, std::string& err) {
                std::string literal;
                if (!getStringParam(params, "literal", literal, err)) {
                    return false;
                }
                if (literal.empty()) {
                    err = "parameter 'literal' must not be empty";
                    return false;
                }
                return true;
            },
        }},
        {"trim_spaces", {
            [](const std::string& text, const nlohmann::json&, const nlohmann::json&) {
                return trimSpaces(text);
            },
            validateNoConfig,
        }},
        {"replace_many", {
            [](const std::string& text, const nlohmann::json& resources, const nlohmann::json& params) {
                const std::string valuesName = replaceManyValuesResourceName(params);
                const std::string replacement =
                    resolveReplaceManyReplacement(resources, params);
                std::string out = text;
                for (const auto& value : resolveReplaceManyValues(resources, valuesName)) {
                    out = replaceAllLiteral(std::move(out), value, replacement);
                }
                return out;
            },
            [](const nlohmann::json& resources, const nlohmann::json& params, std::string& err) {
                const bool hasValues = params.contains("values");
                const bool hasAliases = params.contains("aliases");
                if (!hasValues && !hasAliases) {
                    err = "parameter 'values' (or legacy alias 'aliases') is required";
                    return false;
                }
                std::string valuesName;
                if (hasValues && !getStringParam(params, "values", valuesName, err)) {
                    return false;
                }
                std::string aliasesName;
                if (hasAliases && !getStringParam(params, "aliases", aliasesName, err)) {
                    return false;
                }
                if (!hasValues) {
                    valuesName = aliasesName;
                } else if (hasAliases && valuesName != aliasesName) {
                    err = "parameters 'values' and legacy alias 'aliases' must match when both "
                          "are provided";
                    return false;
                }
                if (!validateCharSetResource(resources, valuesName, err)) {
                    return false;
                }

                const bool hasReplacement = params.contains("replacement");
                const bool hasConstant = params.contains("replacement_constant");
                const bool hasLegacyTarget = params.contains("target");
                if (!hasReplacement && !hasConstant && !hasLegacyTarget) {
                    err = "parameter 'replacement', 'replacement_constant', or legacy alias "
                          "'target' is required";
                    return false;
                }
                if (hasReplacement && (hasConstant || hasLegacyTarget)) {
                    err = "literal 'replacement' cannot be combined with a replacement "
                          "constant";
                    return false;
                }
                if (hasReplacement) {
                    std::string replacement;
                    return getStringParam(params, "replacement", replacement, err);
                }

                std::string constantName;
                if (hasConstant &&
                    !getStringParam(params, "replacement_constant", constantName, err)) {
                    return false;
                }
                std::string legacyTargetName;
                if (hasLegacyTarget &&
                    !getStringParam(params, "target", legacyTargetName, err)) {
                    return false;
                }
                if (!hasConstant) {
                    constantName = legacyTargetName;
                } else if (hasLegacyTarget && constantName != legacyTargetName) {
                    err = "parameters 'replacement_constant' and legacy alias 'target' must "
                          "match when both are provided";
                    return false;
                }
                return validateConstantResource(resources, constantName, err);
            },
        }},
    };
    return registry;
}

} // namespace

bool isKnownFrontendOp(const std::string& op) {
    return frontendOpRegistry().find(op) != frontendOpRegistry().end();
}

bool validateFrontendOp(
    const std::string& op,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params,
    std::string& errOut) {
    errOut.clear();
    if (!params.is_object()) {
        errOut = "frontend op '" + op + "': params must be an object";
        return false;
    }
    const auto& registry = frontendOpRegistry();
    const auto it = registry.find(op);
    if (it == registry.end()) {
        errOut = "unknown frontend op '" + op + "'";
        return false;
    }
    std::string detail;
    if (!it->second.validator(resourcesDoc, params, detail)) {
        errOut = "frontend op '" + op + "': " + detail;
        return false;
    }
    return true;
}

bool prepareFrontendOp(
    const std::string& op,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params,
    PreparedFrontendOp& out,
    std::string& errOut) {
    out = PreparedFrontendOp();
    if (!validateFrontendOp(op, resourcesDoc, params, errOut)) {
        return false;
    }

    if (op == "regex_replace") {
        std::regex pattern(params.at("pattern").get<std::string>(), regexFlags(params));
        std::string replacement = params.at("replacement").get<std::string>();
        out = [pattern = std::move(pattern), replacement = std::move(replacement)](
                  const std::string& text) {
            if (text.empty()) {
                return text;
            }
            return std::regex_replace(text, pattern, replacement);
        };
        return true;
    }

    if (op == "literal_replace") {
        std::string from = params.at("from").get<std::string>();
        std::string to = params.at("to").get<std::string>();
        out = [from = std::move(from), to = std::move(to)](const std::string& text) {
            if (text.empty()) {
                return text;
            }
            return replaceAllLiteral(text, from, to);
        };
        return true;
    }

    if (op == "replace_char_set") {
        auto chars = resolveCharSet(resourcesDoc, params.at("char_set").get<std::string>());
        std::string replacement = params.at("replacement").get<std::string>();
        out = [chars = std::move(chars), replacement = std::move(replacement)](
                  const std::string& text) {
            if (text.empty()) {
                return text;
            }
            return replaceCharSet(text, chars, replacement);
        };
        return true;
    }

    if (op == "collapse_literal_runs") {
        std::string literal = params.at("literal").get<std::string>();
        out = [literal = std::move(literal)](const std::string& text) {
            if (text.empty()) {
                return text;
            }
            return collapseLiteralRuns(text, literal);
        };
        return true;
    }

    if (op == "trim_spaces") {
        out = [](const std::string& text) {
            if (text.empty()) {
                return text;
            }
            return trimSpaces(text);
        };
        return true;
    }

    if (op == "replace_many") {
        auto values = resolveReplaceManyValues(
            resourcesDoc, replaceManyValuesResourceName(params));
        std::string replacement = resolveReplaceManyReplacement(resourcesDoc, params);
        out = [values = std::move(values), replacement = std::move(replacement)](
                  const std::string& text) {
            if (text.empty()) {
                return text;
            }
            std::string result = text;
            for (const auto& value : values) {
                result = replaceAllLiteral(std::move(result), value, replacement);
            }
            return result;
        };
        return true;
    }

    const FrontendOpHandler handler = frontendOpRegistry().at(op).handler;
    nlohmann::json resources = resourcesDoc;
    nlohmann::json capturedParams = params;
    out = [handler, resources = std::move(resources), params = std::move(capturedParams)](
              const std::string& text) {
        if (text.empty()) {
            return text;
        }
        return handler(text, resources, params);
    };
    return true;
}

std::string applyFrontendOp(
    const std::string& op,
    const std::string& text,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params) {
    std::string err;
    if (!validateFrontendOp(op, resourcesDoc, params, err)) {
        throw std::invalid_argument(err);
    }
    if (text.empty()) {
        return text;
    }
    return frontendOpRegistry().at(op).handler(text, resourcesDoc, params);
}

std::string applyPunctuationOp(
    const std::string& op,
    const std::string& text,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params) {
    return applyFrontendOp(op, text, resourcesDoc, params);
}

} // namespace frontend
