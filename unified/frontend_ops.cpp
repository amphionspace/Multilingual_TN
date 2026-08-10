#include "frontend_ops.hpp"

#include <functional>
#include <cctype>
#include <regex>
#include <unordered_set>

#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>

namespace frontend {
namespace {

constexpr const char* kFallbackLang = "en";

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
        for (int32_t i = 0; i < u.length(); ++i) {
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
        if (from.length() == 1 && to.length() == 1) {
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

std::string joinChars(const std::vector<std::string>& chars) {
    std::string out;
    for (const auto& ch : chars) {
        out += ch;
    }
    return out;
}

bool isAsciiAlnum(const std::string& ch) {
    if (ch.empty()) {
        return false;
    }
  unsigned char b = static_cast<unsigned char>(ch[0]);
    return ch.size() == 1 && std::isalnum(b);
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

} // namespace

std::string applyPunctuationOp(
    const std::string& op,
    const std::string& text,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params) {
    if (text.empty()) {
        return text;
    }
    if (op == "nfkc_normalize") {
        return nfkcNormalize(text);
    }
    if (op == "strip_emojis") {
        return stripEmojis(text);
    }
    if (op == "strip_literal_line_break_escapes") {
        return stripLiteralLineBreakEscapes(text);
    }
    if (op == "strip_trailing_backslash") {
        return stripTrailingBackslash(text);
    }
    if (op == "expand_literal_line_break_escapes") {
        return expandLiteralLineBreakEscapes(text);
    }
    if (op == "normalize_clause_break_punct") {
        const std::string setName = params.value("acceptable_trailing", "acceptable_trailing_punct");
        return normalizeClauseBreakPunct(text, resolveCharSet(resourcesDoc, setName));
    }
    if (op == "strip_structural_quotes") {
        return stripStructuralQuotes(
            text,
            resolveCharSet(resourcesDoc, params.value("single_quote_chars", "structural_single_quote_chars")),
            resolveCharSet(resourcesDoc, params.value("jp_quote_chars", "jp_single_quote_chars")),
            resolveCharSet(resourcesDoc, params.value("double_quote_chars", "structural_double_quote_chars")));
    }
    if (op == "strip_structural_single_quotes") {
        return stripStructuralQuotes(
            text,
            resolveCharSet(resourcesDoc, params.value("quote_chars", "structural_single_quote_chars")),
            resolveCharSet(resourcesDoc, params.value("jp_quote_chars", "jp_single_quote_chars")),
            {});
    }
    if (op == "collapse_empty_slash_segments") {
        return collapseEmptySlashSegments(text);
    }
    if (op == "normalize_numeric_dash_ranges") {
        return normalizeNumericDashRanges(text);
    }
    if (op == "collapse_letter_dot_abbrevs") {
        const std::string patternName = params.value("pattern", "letter_dot_abbrev");
        std::string pattern = resolvePatternDef(resourcesDoc, patternName);
        if (pattern.empty()) {
            pattern = R"(\b(?:[A-Z][a-z]?\.)+(?:[A-Z][a-z]?)?\.)";
        }
        return collapseLetterDotAbbrevs(text, pattern);
    }
    if (op == "replace_vertical_line_punct") {
        return replaceVerticalLinePunct(
            text,
            resolveCharSet(resourcesDoc, params.at("char_set").get<std::string>()),
            params.value("replacement", "，"));
    }
    if (op == "remove_dashes") {
        return removeDashes(text, resolveCharSet(resourcesDoc, params.at("char_set").get<std::string>()));
    }
    if (op == "collapse_whitespace") {
        return collapseWhitespace(text);
    }
    if (op == "dedupe_trailing_punctuation") {
        return dedupeTrailingPunctuation(
            text, resolveCharSet(resourcesDoc, params.value("exempt_chars", "dedupe_punct_exempt")));
    }
    if (op == "ensure_trailing_sentence_punct") {
        return ensureTrailingSentencePunct(
            text, resolveCharSet(resourcesDoc, params.value("acceptable_trailing", "acceptable_trailing_punct")));
    }
    if (op == "normalize_ellipsis") {
        std::vector<std::string> aliases;
        const auto& charSets = resourcesDoc["resources"]["char_sets"];
        for (const auto& item : charSets[params.at("aliases").get<std::string>()]) {
            aliases.push_back(item.get<std::string>());
        }
        return normalizeEllipsis(text, aliases, resolveConstant(resourcesDoc, params.at("target").get<std::string>()));
    }
    if (op == "strip_markdown_asterisks") {
        return stripMarkdownAsterisks(text);
    }
    if (op == "strip_markdown_code_ticks") {
        return stripMarkdownCodeTicks(text);
    }
    if (op == "expand_identifier_underscores") {
        return expandIdentifierUnderscores(text);
    }
    if (op == "normalize_programming_tokens_en") {
        return normalizeProgrammingTokensEn(text);
    }
    if (op == "split_sql_glued_identifiers") {
        return splitSqlGluedIdentifiers(text);
    }
    if (op == "strip_control_chars") {
        return stripControlChars(text);
    }
    if (op == "translate_chars") {
        return translateChars(text, resolveCharMap(resourcesDoc, params.at("char_map").get<std::string>()));
    }
    if (op == "strip_zh_structural_punct") {
        return stripZhStructuralPunct(
            text,
            resolveCharSet(resourcesDoc, params.value("structural_chars", "structural_punct_chars")),
            resolveCharSet(resourcesDoc, params.value("acceptable_trailing", "acceptable_trailing_punct")));
    }
    return text;
}

} // namespace frontend
