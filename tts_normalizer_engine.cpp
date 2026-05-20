#include "tts_normalizer_engine.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include <unicode/regex.h>

#include "third_party/nlohmann/json.hpp"

namespace {

bool ttsNormalizerDebugEnabled() {
    const char* v = std::getenv("TTS_NORMALIZER_DEBUG");
    return v != nullptr && v[0] != '\0';
}

std::string truncateForLog(const std::string& utf8, size_t maxBytes) {
    if (utf8.size() <= maxBytes) {
        return utf8;
    }
    return utf8.substr(0, maxBytes) + "...";
}

static void trimPatternDefKey(std::string& key) {
    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) {
        key.erase(key.begin());
    }
    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) {
        key.pop_back();
    }
}

// Phase B: expand {{NAME}} using pattern_defs (values may reference other defs; stabilized first).
static bool expandPatternPlaceholders(std::string& s,
                                      const nlohmann::json& defs,
                                      std::string& errOut) {
    const int kMaxReplacements = 8192;
    int count = 0;
    while (true) {
        const size_t pos = s.find("{{");
        if (pos == std::string::npos) {
            return true;
        }
        const size_t end = s.find("}}", pos + 2);
        if (end == std::string::npos) {
            errOut = "Unclosed '{{' in pattern (missing '}}')";
            return false;
        }
        std::string key = s.substr(pos + 2, end - pos - 2);
        trimPatternDefKey(key);
        if (key.empty()) {
            errOut = "Empty pattern_def placeholder";
            return false;
        }
        if (!defs.contains(key)) {
            errOut = "Unknown pattern_def key: " + key;
            return false;
        }
        const auto& val = defs.at(key);
        if (!val.is_string()) {
            errOut = "pattern_defs[\"" + key + "\"] must be a string";
            return false;
        }
        const std::string repl = val.get<std::string>();
        s.replace(pos, end - pos + 2, repl);
        if (++count > kMaxReplacements) {
            errOut = "pattern expansion exceeded limit (possible cycle in pattern_defs)";
            return false;
        }
    }
}

static bool stabilizePatternDefs(nlohmann::json& defs, std::string& errOut) {
    if (!defs.is_object()) {
        errOut = "pattern_defs must be a JSON object";
        return false;
    }
    for (auto it = defs.begin(); it != defs.end(); ++it) {
        if (!it.value().is_string()) {
            errOut = "pattern_defs values must be strings (key: " + it.key() + ")";
            return false;
        }
    }
    const int kMaxRounds = 128;
    for (int round = 0; round < kMaxRounds; ++round) {
        bool changed = false;
        for (auto it = defs.begin(); it != defs.end(); ++it) {
            std::string v = it.value().get<std::string>();
            const std::string orig = v;
            if (!expandPatternPlaceholders(v, defs, errOut)) {
                return false;
            }
            if (v != orig) {
                it.value() = std::move(v);
                changed = true;
            }
        }
        if (!changed) {
            return true;
        }
    }
    errOut = "pattern_defs did not stabilize (possible mutual recursion)";
    return false;
}

icu::UnicodeString processRegex(
    const icu::UnicodeString& input,
    const icu::UnicodeString& pattern,
    const std::function<icu::UnicodeString(icu::RegexMatcher&, UErrorCode&)>& callback,
    const std::function<void(icu::RegexMatcher&, UErrorCode&)>* onMatch = nullptr) {
    UErrorCode status = U_ZERO_ERROR;
    icu::RegexMatcher matcher(pattern, 0, status);
    if (U_FAILURE(status)) {
        return input;
    }
    matcher.reset(input);
    icu::UnicodeString result;
    int32_t lastEnd = 0;
    while (matcher.find(status) && U_SUCCESS(status)) {
        result.append(input, lastEnd, matcher.start(status) - lastEnd);
        if (onMatch) {
            (*onMatch)(matcher, status);
        }
        result.append(callback(matcher, status));
        lastEnd = matcher.end(status);
        if (U_FAILURE(status)) {
            break;
        }
    }
    result.append(input, lastEnd, input.length() - lastEnd);
    return result;
}

int parseIntUStr(const icu::UnicodeString& s) {
    std::string utf8;
    s.toUTF8String(utf8);
    try {
        return std::stoi(utf8);
    } catch (...) {
        return 0;
    }
}

bool numericIsOne(const icu::UnicodeString& s) {
    std::string utf8;
    s.toUTF8String(utf8);
    try {
        double v = std::stod(utf8);
        return std::abs(v - 1.0) < 1e-9;
    } catch (...) {
        return false;
    }
}

// Convert the first word of a unit/currency phrase to its singular form by stripping a trailing
// 's' when present. "dollars" -> "dollar"; "kilometers per hour" -> "kilometer per hour";
// "miles" -> "mile". Leaves leading/trailing whitespace intact for downstream concatenation.
icu::UnicodeString toSingularEn(const icu::UnicodeString& plural) {
    int len = plural.length();
    int i = 0;
    while (i < len && (plural.charAt(i) == ' ' || plural.charAt(i) == '\t')) {
        i++;
    }
    int wordStart = i;
    while (i < len && plural.charAt(i) != ' ' && plural.charAt(i) != '\t' && plural.charAt(i) != '-') {
        i++;
    }
    int wordEnd = i;
    if (wordEnd <= wordStart) {
        return plural;
    }
    if (plural.charAt(wordEnd - 1) != 's') {
        return plural;
    }
    // Avoid mangling "ss" endings (rare for our maps but be defensive).
    if (wordEnd - wordStart >= 2 && plural.charAt(wordEnd - 2) == 's') {
        return plural;
    }
    // English plurals of words ending in 'ch', 'sh', or 'x' take '-es'
    // (inches -> inch, dishes -> dish, boxes -> box). Strip two chars instead of one.
    int stripLen = 1;
    if (wordEnd - wordStart >= 4 && plural.charAt(wordEnd - 2) == 'e') {
        UChar c3 = plural.charAt(wordEnd - 3);
        UChar c4 = plural.charAt(wordEnd - 4);
        if ((c3 == 'h' && (c4 == 'c' || c4 == 's')) || c3 == 'x') {
            stripLen = 2;
        }
    }
    icu::UnicodeString out;
    plural.extractBetween(0, wordEnd - stripLen, out);
    icu::UnicodeString tail;
    plural.extractBetween(wordEnd, len, tail);
    return out + tail;
}

icu::UnicodeString romanToArabicStr(const icu::UnicodeString& roman) {
    icu::UnicodeString temp = roman;
    temp.toUpper();
    std::string s;
    temp.toUTF8String(s);
    static const std::unordered_map<char, int> romanMap = {
        {'I', 1}, {'V', 5}, {'X', 10}, {'L', 50}, {'C', 100}, {'D', 500}, {'M', 1000}};
    int total = 0;
    for (size_t i = 0; i < s.length(); ++i) {
        if (romanMap.find(s[i]) == romanMap.end()) {
            continue;
        }
        if (i + 1 < s.length() && romanMap.count(s[i + 1]) && romanMap.at(s[i]) < romanMap.at(s[i + 1])) {
            total -= romanMap.at(s[i]);
        } else {
            total += romanMap.at(s[i]);
        }
    }
    if (total == 0) {
        return roman;
    }
    return icu::UnicodeString::fromUTF8(std::to_string(total));
}

icu::UnicodeString superscriptsToNormal(const icu::UnicodeString& superscripts, bool& isNegative) {
    icu::UnicodeString normalNum;
    isNegative = false;
    for (int i = 0; i < superscripts.length(); ++i) {
        UChar c = superscripts.charAt(i);
        switch (c) {
            case 0x207B:
                isNegative = true;
                break;
            case 0x2070:
                normalNum += '0';
                break;
            case 0x00B9:
                normalNum += '1';
                break;
            case 0x00B2:
                normalNum += '2';
                break;
            case 0x00B3:
                normalNum += '3';
                break;
            case 0x2074:
                normalNum += '4';
                break;
            case 0x2075:
                normalNum += '5';
                break;
            case 0x2076:
                normalNum += '6';
                break;
            case 0x2077:
                normalNum += '7';
                break;
            case 0x2078:
                normalNum += '8';
                break;
            case 0x2079:
                normalNum += '9';
                break;
            default:
                break;
        }
    }
    return normalNum;
}

icu::UnicodeString toNormalDigitsMixed(const icu::UnicodeString& src) {
    icu::UnicodeString normal;
    for (int i = 0; i < src.length(); ++i) {
        UChar c = src.charAt(i);
        if (c >= 0x2080 && c <= 0x2089) {
            normal += static_cast<char>('0' + (c - 0x2080));
        } else if (c == 0x2070) {
            normal += '0';
        } else if (c == 0x00B9) {
            normal += '1';
        } else if (c == 0x00B2) {
            normal += '2';
        } else if (c == 0x00B3) {
            normal += '3';
        } else if (c >= 0x2074 && c <= 0x2079) {
            normal += static_cast<char>('0' + (c - 0x2074 + 4));
        }
    }
    return normal;
}

using njson = nlohmann::json;

static std::string jgetS(const njson& j, const char* key, const std::string& fallback = "") {
    if (!j.contains(key)) {
        return fallback;
    }
    if (j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return fallback;
}

static icu::UnicodeString pickLocStr(const njson& v, const std::string& localeTag, const std::string& fallbackUtf8) {
    if (v.is_string()) {
        return icu::UnicodeString::fromUTF8(v.get<std::string>());
    }
    if (v.is_object()) {
        if (v.contains(localeTag) && v[localeTag].is_string()) {
            return icu::UnicodeString::fromUTF8(v[localeTag].get<std::string>());
        }
        if (v.contains("en") && v["en"].is_string()) {
            return icu::UnicodeString::fromUTF8(v["en"].get<std::string>());
        }
        if (v.contains("zh") && v["zh"].is_string()) {
            return icu::UnicodeString::fromUTF8(v["zh"].get<std::string>());
        }
    }
    return icu::UnicodeString::fromUTF8(fallbackUtf8);
}

// English month token (full name or common abbreviation) → 1–12; else 0.
static int parseEnglishMonthTokenForEn(const icu::UnicodeString& part) {
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
    if (s.empty()) {
        return 0;
    }
    if (s == "january" || s == "jan") {
        return 1;
    }
    if (s == "february" || s == "feb") {
        return 2;
    }
    if (s == "march" || s == "mar") {
        return 3;
    }
    if (s == "april" || s == "apr") {
        return 4;
    }
    if (s == "may") {
        return 5;
    }
    if (s == "june" || s == "jun") {
        return 6;
    }
    if (s == "july" || s == "jul") {
        return 7;
    }
    if (s == "august" || s == "aug") {
        return 8;
    }
    if (s == "september" || s == "sept" || s == "sep") {
        return 9;
    }
    if (s == "october" || s == "oct") {
        return 10;
    }
    if (s == "november" || s == "nov") {
        return 11;
    }
    if (s == "december" || s == "dec") {
        return 12;
    }
    return 0;
}

} // namespace

icu::UnicodeString TtsNormalizerEngine::expandReplacement(const icu::UnicodeString& templ,
                                                          icu::RegexMatcher& m,
                                                          UErrorCode& st) {
    icu::UnicodeString out;
    for (int i = 0; i < templ.length(); ++i) {
        if (templ.charAt(i) == '$' && i + 1 < templ.length()) {
            UChar d = templ.charAt(i + 1);
            if (d >= '0' && d <= '9') {
                int gi = d - '0';
                icu::UnicodeString g = m.group(gi, st);
                out += g;
                ++i;
                continue;
            }
        }
        out += templ.charAt(i);
    }
    return out;
}

bool TtsNormalizerEngine::loadRules(const std::string& jsonPath, std::string& errOut) {
    std::ifstream in(jsonPath);
    if (!in) {
        errOut = "Cannot open rules file: " + jsonPath;
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(buffer.str());
    } catch (const std::exception& e) {
        errOut = std::string("JSON parse error: ") + e.what();
        return false;
    }

    localeTag_ = j.value("locale", "en");

    monthNamesEn_.assign(13, icu::UnicodeString());
    static const char* kDefMonths[] = {"", "January", "February", "March", "April", "May", "June",
                                       "July", "August", "September", "October", "November", "December"};
    for (int i = 1; i <= 12; ++i) {
        monthNamesEn_[static_cast<size_t>(i)] = icu::UnicodeString::fromUTF8(kDefMonths[i]);
    }
    if (j.contains("month_names_en") && j["month_names_en"].is_array()) {
        const auto& arr = j["month_names_en"];
        for (size_t i = 0; i < arr.size() && i < 12; ++i) {
            if (arr[i].is_string()) {
                monthNamesEn_[1 + i] = icu::UnicodeString::fromUTF8(arr[i].get<std::string>());
            }
        }
    }

    monthNamesAr_.assign(13, icu::UnicodeString());
    if (j.contains("month_names_ar") && j["month_names_ar"].is_array()) {
        const auto& arr = j["month_names_ar"];
        for (size_t i = 0; i < arr.size() && i < 12; ++i) {
            if (arr[i].is_string()) {
                monthNamesAr_[1 + i] = icu::UnicodeString::fromUTF8(arr[i].get<std::string>());
            }
        }
    }

    monthNamesBn_.assign(13, icu::UnicodeString());
    if (j.contains("month_names_bn") && j["month_names_bn"].is_array()) {
        const auto& arr = j["month_names_bn"];
        for (size_t i = 0; i < arr.size() && i < 12; ++i) {
            if (arr[i].is_string()) {
                monthNamesBn_[1 + i] = icu::UnicodeString::fromUTF8(arr[i].get<std::string>());
            }
        }
    }

    monthNamesRu_.assign(13, icu::UnicodeString());
    if (j.contains("month_names_ru") && j["month_names_ru"].is_array()) {
        const auto& arr = j["month_names_ru"];
        for (size_t i = 0; i < arr.size() && i < 12; ++i) {
            if (arr[i].is_string()) {
                monthNamesRu_[1 + i] = icu::UnicodeString::fromUTF8(arr[i].get<std::string>());
            }
        }
    }

    digitNames_.clear();
    if (j.contains("digit_names") && j["digit_names"].is_array()) {
        for (const auto& item : j["digit_names"]) {
            digitNames_.push_back(icu::UnicodeString::fromUTF8(item.get<std::string>()));
        }
    }
    currencyMap_.clear();
    if (j.contains("currency_map") && j["currency_map"].is_object()) {
        for (auto it = j["currency_map"].begin(); it != j["currency_map"].end(); ++it) {
            currencyMap_[it.key()] = icu::UnicodeString::fromUTF8(it.value().get<std::string>());
        }
    }
    unitComposite_.clear();
    if (j.contains("unit_composite") && j["unit_composite"].is_object()) {
        for (auto it = j["unit_composite"].begin(); it != j["unit_composite"].end(); ++it) {
            unitComposite_[it.key()] = icu::UnicodeString::fromUTF8(it.value().get<std::string>());
        }
    }
    unitSingle_.clear();
    if (j.contains("unit_single") && j["unit_single"].is_object()) {
        for (auto it = j["unit_single"].begin(); it != j["unit_single"].end(); ++it) {
            unitSingle_[it.key()] = icu::UnicodeString::fromUTF8(it.value().get<std::string>());
        }
    }

    nlohmann::json patternDefs = j.value("pattern_defs", nlohmann::json::object());
    if (!patternDefs.empty()) {
        if (!stabilizePatternDefs(patternDefs, errOut)) {
            return false;
        }
    }

    rules_.clear();
    if (!j.contains("rules") || !j["rules"].is_array()) {
        errOut = "Missing 'rules' array";
        return false;
    }
    for (const auto& r : j["rules"]) {
        Rule rule;
        std::string patUtf8 = r.at("pattern").get<std::string>();
        if (!patternDefs.empty()) {
            if (!expandPatternPlaceholders(patUtf8, patternDefs, errOut)) {
                return false;
            }
            if (patUtf8.find("{{") != std::string::npos) {
                errOut = "Unresolved '{{' in rule pattern after pattern_defs expansion";
                return false;
            }
        }
        rule.pattern = icu::UnicodeString::fromUTF8(patUtf8);
        if (r.contains("replace")) {
            rule.replacement = icu::UnicodeString::fromUTF8(r.at("replace").get<std::string>());
        }
        if (r.contains("action")) {
            rule.action = r.at("action").get<std::string>();
        }
        if (r.contains("id") && r["id"].is_string()) {
            rule.id = r["id"].get<std::string>();
        }
        rule.params = r.value("params", nlohmann::json::object());
        rules_.push_back(std::move(rule));
    }
    return true;
}

bool TtsNormalizerEngine::loadPinyinMap(const std::string& jsonPath, std::string& errOut) {
    if (jsonPath.empty()) {
        return true;
    }
    std::ifstream in(jsonPath);
    if (!in) {
        errOut = "Cannot open pinyin map: " + jsonPath;
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(buffer.str());
    } catch (const std::exception& e) {
        errOut = std::string("JSON parse error: ") + e.what();
        return false;
    }
    pinyinMap_.clear();
    if (!j.is_object()) {
        errOut = "Pinyin map must be a JSON object";
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        pinyinMap_[it.key()] = it.value().get<std::string>();
    }
    return true;
}

icu::UnicodeString TtsNormalizerEngine::digitNameChar(UChar c) const {
    if (c >= '0' && c <= '9') {
        int idx = c - '0';
        if (idx >= 0 && idx < static_cast<int>(digitNames_.size())) {
            return digitNames_[static_cast<size_t>(idx)];
        }
    }
    return icu::UnicodeString(c);
}

icu::UnicodeString TtsNormalizerEngine::englishMonthName(int month) const {
    if (month < 1 || month > 12) {
        return icu::UnicodeString();
    }
    if (static_cast<size_t>(month) < monthNamesEn_.size()) {
        return monthNamesEn_[static_cast<size_t>(month)];
    }
    return icu::UnicodeString();
}

icu::UnicodeString TtsNormalizerEngine::monthNameForIsoDate(int month) const {
    if (month < 1 || month > 12) {
        return icu::UnicodeString();
    }
    const size_t idx = static_cast<size_t>(month);
    if (localeTag_ == "ar" && idx < monthNamesAr_.size() && !monthNamesAr_[idx].isEmpty()) {
        return monthNamesAr_[idx];
    }
    if (localeTag_ == "bn" && idx < monthNamesBn_.size() && !monthNamesBn_[idx].isEmpty()) {
        return monthNamesBn_[idx];
    }
    if (localeTag_ == "ru" && idx < monthNamesRu_.size() && !monthNamesRu_[idx].isEmpty()) {
        return monthNamesRu_[idx];
    }
    return englishMonthName(month);
}

icu::UnicodeString TtsNormalizerEngine::spelloutEnglishYearProse(int year, const TtsCallbacks& cb) const {
    if (localeTag_ != "en" || year < 1200 || year > 1999) {
        return cb.spellout(icu::UnicodeString::fromUTF8(std::to_string(year)));
    }
    const int lo = year % 100;
    if (lo == 0) {
        return cb.spellout(icu::UnicodeString::fromUTF8(std::to_string(year)));
    }
    const int hi = year / 100;
    std::string loStr = (lo < 10) ? (std::string("0") + std::to_string(lo)) : std::to_string(lo);
    return cb.spellout(icu::UnicodeString::fromUTF8(std::to_string(hi))) + " " +
           cb.spellout(icu::UnicodeString::fromUTF8(loStr));
}

icu::UnicodeString TtsNormalizerEngine::applyExec(const nlohmann::json& params,
                                                 icu::RegexMatcher& m,
                                                 UErrorCode& st,
                                                 const TtsCallbacks& cb) const {
    if (!params.contains("steps") || !params["steps"].is_array()) {
        return m.group(0, st);
    }
    icu::UnicodeString out;
    for (const auto& step : params["steps"]) {
        if (step.is_object()) {
            out += applyExecStep(step, m, st, cb);
        }
    }
    return out;
}

icu::UnicodeString TtsNormalizerEngine::applyExecStep(const nlohmann::json& step,
                                                     icu::RegexMatcher& m,
                                                     UErrorCode& st,
                                                     const TtsCallbacks& cb) const {
    const std::string op = jgetS(step, "op", "");
    if (op == "lit") {
        return icu::UnicodeString::fromUTF8(step.at("text").get<std::string>());
    }
    if (op == "grp") {
        const int g = step.value("g", 0);
        const std::string as = jgetS(step, "as", "spellout");
        const icu::UnicodeString part = m.group(g, st);
        if (as == "raw") {
            return part;
        }
        if (as == "spellout") {
            return cb.spellout(part);
        }
        if (as == "ordinal") {
            UErrorCode ost = U_ZERO_ERROR;
            icu::UnicodeString ord = cb.ordinal_spellout(part, ost);
            if (U_FAILURE(ost) || ord.isEmpty()) {
                return cb.spellout(part);
            }
            return ord;
        }
        if (as == "digits") {
            icu::UnicodeString out;
            for (int i = 0; i < part.length(); ++i) {
                out += digitNameChar(part.charAt(i));
            }
            return out;
        }
        if (as == "year_2digit") {
            int yy = parseIntUStr(part);
            if (yy < 0 || yy > 99) {
                return cb.spellout(part);
            }
            int full = (yy < 50) ? 2000 + yy : 1900 + yy;
            return cb.spellout(icu::UnicodeString::fromUTF8(std::to_string(full)));
        }
        if (as == "year_en_prose") {
            const int y = parseIntUStr(part);
            if (y <= 0) {
                return cb.spellout(part);
            }
            return spelloutEnglishYearProse(y, cb);
        }
        if (as == "month_en_full") {
            if (localeTag_ != "en") {
                return part;
            }
            const int mon = parseEnglishMonthTokenForEn(part);
            if (mon < 1 || mon > 12) {
                return part;
            }
            return englishMonthName(mon);
        }
        return part;
    }
    if (op == "digits") {
        const int g = step.value("g", 0);
        const icu::UnicodeString raw = m.group(g, st);
        icu::UnicodeString res;
        for (int i = 0; i < raw.length(); ++i) {
            UChar c = raw.charAt(i);
            if (c >= '0' && c <= '9') {
                res += digitNameChar(c);
            }
        }
        return res;
    }
    if (op == "walk") {
        const int g = step.value("g", 0);
        const icu::UnicodeString raw = m.group(g, st);
        const nlohmann::json map = step.value("map", nlohmann::json::object());
        const bool otherEnSpace = step.value("other_en_space", false);
        const std::string letterStyle = jgetS(step, "letter", "keep");
        icu::UnicodeString res;
        for (int i = 0; i < raw.length(); ++i) {
            UChar c = raw.charAt(i);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                if (letterStyle == "spaced") {
                    res += " ";
                    res += c;
                    res += " ";
                } else {
                    res += c;
                }
                continue;
            }
            if (c >= '0' && c <= '9') {
                res += digitNameChar(c);
                continue;
            }
            icu::UnicodeString oneChar;
            oneChar += c;
            std::string keyUtf8;
            oneChar.toUTF8String(keyUtf8);
            if (map.contains(keyUtf8)) {
                res += pickLocStr(map[keyUtf8], localeTag_, keyUtf8);
                continue;
            }
            res += c;
            if (otherEnSpace && localeTag_ == "en") {
                res += ' ';
            }
        }
        return res;
    }
    if (op == "normalize_mixed_digits") {
        const int g = step.value("g", 1);
        return toNormalDigitsMixed(m.group(g, st));
    }
    if (op == "roman") {
        const int g = step.value("g", 0);
        icu::UnicodeString roman = m.group(g, st);
        if (roman.length() <= 1) {
            return roman;
        }
        icu::UnicodeString arabicStr = romanToArabicStr(roman);
        return cb.spellout(arabicStr) + " ";
    }
    if (op == "fraction_en") {
        const int gw = step.value("whole_g", 0);
        const int gn = step.value("num_g", 1);
        const int gd = step.value("den_g", 2);
        const int num = parseIntUStr(m.group(gn, st));
        const int den = parseIntUStr(m.group(gd, st));
        if (num <= 0 || den <= 1) {
            return m.group(0, st);
        }
        icu::UnicodeString denomWord;
        if (den == 2) {
            denomWord = "half";
        } else if (den == 4) {
            denomWord = "quarter";
        } else {
            UErrorCode localStatus = U_ZERO_ERROR;
            denomWord = cb.ordinal_spellout(m.group(gd, st), localStatus);
            if (U_FAILURE(localStatus) || denomWord.isEmpty()) {
                denomWord = cb.spellout(m.group(gd, st));
            }
        }
        if (num > 1) {
            if (denomWord == "half") {
                denomWord = "halves";
            } else {
                denomWord += "s";
            }
        }
        icu::UnicodeString frac = cb.spellout(m.group(gn, st)) + " " + denomWord;
        if (gw > 0) {
            icu::UnicodeString wholeStr = m.group(gw, st);
            if (!wholeStr.isEmpty()) {
                return " " + cb.spellout(wholeStr) + " and " + frac + " ";
            }
        }
        return " " + frac + " ";
    }
    if (op == "date_iso_en") {
        const int gy = step.value("gy", 1);
        const int gm = step.value("gm", 2);
        const int gd = step.value("gd", 3);
        int y = parseIntUStr(m.group(gy, st));
        int mon = parseIntUStr(m.group(gm, st));
        int day = parseIntUStr(m.group(gd, st));
        if (y <= 0 || mon <= 0 || mon > 12 || day <= 0 || day > 31) {
            return m.group(0, st);
        }
        UErrorCode localStatus = U_ZERO_ERROR;
        icu::UnicodeString dayOrdinal = cb.ordinal_spellout(m.group(gd, st), localStatus);
        if (U_FAILURE(localStatus) || dayOrdinal.isEmpty()) {
            dayOrdinal = cb.spellout(m.group(gd, st));
        }
        icu::UnicodeString yearOut =
            (localeTag_ == "en") ? spelloutEnglishYearProse(y, cb) : cb.spellout(m.group(gy, st));
        return " " + monthNameForIsoDate(mon) + " " + dayOrdinal + ", " + yearOut + " ";
    }
    if (op == "spellout_affix") {
        const int g = step.value("g", 1);
        const std::string pre = jgetS(step, "prefix", "");
        const std::string suf = jgetS(step, "suffix", "");
        return icu::UnicodeString::fromUTF8(pre) + cb.spellout(m.group(g, st)) + icu::UnicodeString::fromUTF8(suf);
    }
    if (op == "spellout_clean") {
        const int g = step.value("g", 1);
        icu::UnicodeString rawNum = m.group(g, st);
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
        return cb.spellout(cleanNum);
    }
    if (op == "lookup_map") {
        const int g = step.value("g", 1);
        icu::UnicodeString raw = m.group(g, st);
        std::string key;
        raw.toUTF8String(key);
        const auto& mp = step["map"];
        if (mp.is_object() && mp.contains(key) && mp[key].is_string()) {
            return icu::UnicodeString::fromUTF8(mp[key].get<std::string>());
        }
        if (step.contains("fallback") && step["fallback"].is_string()) {
            return icu::UnicodeString::fromUTF8(step["fallback"].get<std::string>());
        }
        if (step.value("default_raw", false)) {
            return raw;
        }
        return icu::UnicodeString();
    }
    if (op == "lookup_currency") {
        const int g = step.value("g", 1);
        const int numG = step.value("num_g", 0);
        icu::UnicodeString sym = m.group(g, st);
        std::string symUtf8;
        sym.toUTF8String(symUtf8);
        auto it = currencyMap_.find(symUtf8);
        if (it != currencyMap_.end()) {
            icu::UnicodeString currencyWord = it->second;
            const bool singularEn =
                (localeTag_ == "en") && step.value("singular_en", true) && numG > 0 && numericIsOne(m.group(numG, st));
            if (singularEn) {
                currencyWord = toSingularEn(currencyWord);
            }
            return currencyWord;
        }
        if (step.contains("fallback") && step["fallback"].is_string()) {
            return icu::UnicodeString::fromUTF8(step["fallback"].get<std::string>());
        }
        return sym;
    }
    if (op == "spellout_superscript") {
        const int g = step.value("g", 1);
        icu::UnicodeString superscripts = m.group(g, st);
        bool neg = false;
        icu::UnicodeString normalNum = superscriptsToNormal(superscripts, neg);
        icu::UnicodeString out;
        if (neg) {
            out += icu::UnicodeString::fromUTF8(jgetS(step, "minus", ""));
        }
        out += cb.spellout(normalNum);
        return out;
    }
    if (op == "lookup_table") {
        const int g = step.value("g", 1);
        const int numG = step.value("num_g", 0);
        const std::string table = jgetS(step, "table", "single");
        icu::UnicodeString u = m.group(g, st);
        std::string u8;
        u.toUTF8String(u8);
        std::string u8key;
        u8key.reserve(u8.size());
        for (char c : u8) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                u8key.push_back(c);
            }
        }
        const bool singularEn = (localeTag_ == "en") && numG > 0 && numericIsOne(m.group(numG, st));
        const auto& tbl = (table == "composite") ? unitComposite_ : unitSingle_;
        auto it = tbl.find(u8key);
        if (it != tbl.end()) {
            return singularEn ? toSingularEn(it->second) : it->second;
        }
        return u;
    }
    if (op == "ordinal") {
        const int g = step.value("g", 1);
        icu::UnicodeString ord = cb.ordinal_spellout(m.group(g, st), st);
        return icu::UnicodeString::fromUTF8(jgetS(step, "pad_before", " ")) + ord +
               icu::UnicodeString::fromUTF8(jgetS(step, "pad_after", " "));
    }
    if (op == "aircraft_series_en") {
        const std::string variant = jgetS(step, "variant", "airbus_a");
        const int numG = step.value("num_g", 1);
        icu::UnicodeString numStr = m.group(numG, st);
        const int n = parseIntUStr(numStr);
        if (n <= 0) {
            return m.group(0, st);
        }
        icu::UnicodeString mid;
        if (variant == "boeing_7xx") {
            if (n < 700 || n > 799) {
                return m.group(0, st);
            }
            const int lo = n % 100;
            if (lo == 0) {
                mid = cb.spellout(numStr);
            } else {
                char bufLo[8];
                std::snprintf(bufLo, sizeof(bufLo), "%02d", lo);
                mid = cb.spellout(icu::UnicodeString("7")) + " " +
                      cb.spellout(icu::UnicodeString::fromUTF8(bufLo));
            }
        } else if (variant == "airbus_a") {
            if (n < 100 || n > 999) {
                return m.group(0, st);
            }
            const int lo = n % 100;
            if (lo == 0) {
                mid = cb.spellout(numStr);
            } else {
                const int hi = n / 100;
                char bufHi[8];
                char bufLo[8];
                std::snprintf(bufHi, sizeof(bufHi), "%d", hi);
                std::snprintf(bufLo, sizeof(bufLo), "%02d", lo);
                mid = cb.spellout(icu::UnicodeString::fromUTF8(bufHi)) + " " +
                      cb.spellout(icu::UnicodeString::fromUTF8(bufLo));
            }
        } else {
            return m.group(0, st);
        }
        return mid;
    }
    if (op == "loose_num") {
        const int g = step.value("g", 0);
        icu::UnicodeString raw = m.group(g, st);
        const std::string stripChars = jgetS(step, "strip", "");
        icu::UnicodeString cleaned;
        if (stripChars.empty()) {
            cleaned = raw;
        } else {
            for (int i = 0; i < raw.length(); ++i) {
                UChar c = raw.charAt(i);
                bool skip = false;
                for (size_t j = 0; j < stripChars.size(); ++j) {
                    if (c == static_cast<UChar>(static_cast<unsigned char>(stripChars[j]))) {
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    cleaned += c;
                }
            }
        }
        if (localeTag_ == "en") {
            return " " + cb.spellout(cleaned) + " ";
        }
        return cb.spellout(cleaned);
    }
    if (op == "ru_num_morph") {
        const int g = step.value("g", 0);
        const icu::UnicodeString num = m.group(g, st);
        if (num.isEmpty()) {
            return num;
        }
        const bool hasLookup = static_cast<bool>(cb.lookup_morph);
        const bool hasMorphSpellout = static_cast<bool>(cb.spellout_with_morph);
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
        // Fallback remains normal locale spellout; ru.cpp provides masculine-default spellout.
        return cb.spellout(num);
    }
    if (op == "label_suffix") {
        icu::UnicodeString label = m.group(step.value("g1", 1), st);
        icu::UnicodeString num = m.group(step.value("g2", 2), st);
        icu::UnicodeString sfx = m.group(step.value("g3", 3), st);
        const std::string labelCase = jgetS(step, "label_case", "lower");
        if (labelCase == "lower") {
            label.toLower();
        } else if (labelCase == "upper") {
            label.toUpper();
        }
        const std::string numberMode = jgetS(step, "number_mode", "spellout");
        icu::UnicodeString numOut;
        if (numberMode == "digits") {
            for (int i = 0; i < num.length(); ++i) {
                numOut += digitNameChar(num.charAt(i));
            }
        } else {
            numOut = cb.spellout(num);
        }
        const std::string sfxCase = jgetS(step, "suffix_case", "keep");
        if (sfxCase == "lower") {
            sfx.toLower();
        } else if (sfxCase == "upper") {
            sfx.toUpper();
        }
        return label + " " + numOut + " " + sfx + " ";
    }
    if (op == "time_en_12h") {
        const int gh = step.value("hour_g", 1);
        const int gm = step.value("minute_g", 2);
        const int gs = step.value("second_g", 3);
        const int gap = step.value("ampm_g", 4);
        int hour = parseIntUStr(m.group(gh, st));
        int minute = parseIntUStr(m.group(gm, st));
        int second = m.group(gs, st).isEmpty() ? -1 : parseIntUStr(m.group(gs, st));
        icu::UnicodeString ampm = m.group(gap, st);
        ampm.toLower();
        if (hour < 1 || hour > 12 || minute < 0 || minute > 59 || (second > 59)) {
            return m.group(0, st);
        }
        const std::string amTok = jgetS(step, "am_token", "am");
        const std::string pmTok = jgetS(step, "pm_token", "pm");
        icu::UnicodeString amTokU = icu::UnicodeString::fromUTF8(amTok);
        icu::UnicodeString pmTokU = icu::UnicodeString::fromUTF8(pmTok);
        amTokU.toLower();
        pmTokU.toLower();
        if (hour == 12 && minute == 0 && second == -1 && ampm == amTokU) {
            return icu::UnicodeString::fromUTF8(jgetS(step, "midnight", " midnight "));
        }
        if (hour == 12 && minute == 0 && second == -1 && ampm == pmTokU) {
            return icu::UnicodeString::fromUTF8(jgetS(step, "noon", " noon "));
        }
        icu::UnicodeString res = icu::UnicodeString::fromUTF8(jgetS(step, "lead", " ")) + cb.spellout(m.group(gh, st));
        if (minute == 0 && second == -1) {
            res += icu::UnicodeString::fromUTF8(jgetS(step, "oclock", " o'clock"));
        } else if (minute > 0 && minute < 10) {
            res += icu::UnicodeString::fromUTF8(jgetS(step, "oh", " oh ")) + cb.spellout(m.group(gm, st));
        } else if (minute != 0) {
            res += " " + cb.spellout(m.group(gm, st));
        }
        if (second != -1) {
            res += icu::UnicodeString::fromUTF8(jgetS(step, "and_sec", " and ")) + cb.spellout(m.group(gs, st)) +
                   icu::UnicodeString::fromUTF8(jgetS(step, "seconds", " seconds"));
        }
        res += (ampm == amTokU) ? icu::UnicodeString::fromUTF8(jgetS(step, "suffix_am", " AM "))
                                : icu::UnicodeString::fromUTF8(jgetS(step, "suffix_pm", " PM "));
        return res;
    }
    if (op == "time_en_24h") {
        const int gh = step.value("hour_g", 1);
        const int gm = step.value("minute_g", 2);
        const int gs = step.value("second_g", 3);
        int hour = parseIntUStr(m.group(gh, st));
        int minute = parseIntUStr(m.group(gm, st));
        int second = m.group(gs, st).isEmpty() ? -1 : parseIntUStr(m.group(gs, st));
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || (second > 59)) {
            return m.group(0, st);
        }
        if (hour == 0 && minute == 0 && second == -1) {
            return icu::UnicodeString::fromUTF8(jgetS(step, "midnight", " midnight "));
        }
        icu::UnicodeString res = icu::UnicodeString::fromUTF8(jgetS(step, "lead", " ")) + cb.spellout(m.group(gh, st));
        if (minute == 0 && second == -1) {
            res += icu::UnicodeString::fromUTF8(jgetS(step, "oclock", " o'clock"));
        } else if (minute > 0 && minute < 10) {
            res += icu::UnicodeString::fromUTF8(jgetS(step, "oh", " oh ")) + cb.spellout(m.group(gm, st));
        } else if (minute != 0) {
            res += " " + cb.spellout(m.group(gm, st));
        }
        if (second != -1) {
            res += icu::UnicodeString::fromUTF8(jgetS(step, "and_sec", " and ")) + cb.spellout(m.group(gs, st)) +
                   icu::UnicodeString::fromUTF8(jgetS(step, "seconds", " seconds"));
        }
        res += icu::UnicodeString::fromUTF8(jgetS(step, "trail", " "));
        return res;
    }
    if (op == "time_zh_ampm") {
        const int gh = step.value("hour_g", 1);
        const int gm = step.value("minute_g", 2);
        const int gs = step.value("second_g", 3);
        const int gp = step.value("period_g", 4);
        int hour = parseIntUStr(m.group(gh, st));
        icu::UnicodeString minuteStr = m.group(gm, st);
        icu::UnicodeString secondStr = m.group(gs, st);
        icu::UnicodeString period = m.group(gp, st);
        period.toUpper();
        icu::UnicodeString pmAlt = icu::UnicodeString::fromUTF8(jgetS(step, "pm_alt", "下午"));
        icu::UnicodeString amAlt = icu::UnicodeString::fromUTF8(jgetS(step, "am_alt", "上午"));
        bool isPM = (period == "PM" || period == pmAlt);
        bool isAM = (period == "AM" || period == amAlt);
        if (isAM && hour == 12) {
            hour = 0;
        }
        if (isPM && hour != 12) {
            hour += 12;
        }
        icu::UnicodeString res =
            cb.spellout(icu::UnicodeString::fromUTF8(std::to_string(hour))) +
            icu::UnicodeString::fromUTF8(jgetS(step, "colon", "点")) + cb.spellout(minuteStr) +
            icu::UnicodeString::fromUTF8(jgetS(step, "minute_suffix", "分"));
        if (!secondStr.isEmpty()) {
            res += cb.spellout(secondStr) + icu::UnicodeString::fromUTF8(jgetS(step, "second_suffix", "秒"));
        }
        return res;
    }
    if (op == "time_zh_range") {
        const int gh1 = step.value("hour_g1", 1);
        const int gm1 = step.value("minute_g1", 2);
        const int gs1 = step.value("second_g1", 3);
        const int gh2 = step.value("hour_g2", 4);
        const int gm2 = step.value("minute_g2", 5);
        const int gs2 = step.value("second_g2", 6);

        const icu::UnicodeString h1s = m.group(gh1, st);
        const icu::UnicodeString m1s = m.group(gm1, st);
        const icu::UnicodeString s1s = m.group(gs1, st);
        const icu::UnicodeString h2s = m.group(gh2, st);
        const icu::UnicodeString m2s = m.group(gm2, st);
        const icu::UnicodeString s2s = m.group(gs2, st);

        const int h1 = parseIntUStr(h1s);
        const int mm1 = parseIntUStr(m1s);
        const int ss1 = s1s.isEmpty() ? -1 : parseIntUStr(s1s);
        const int h2 = parseIntUStr(h2s);
        const int mm2 = parseIntUStr(m2s);
        const int ss2 = s2s.isEmpty() ? -1 : parseIntUStr(s2s);

        if (h1 < 0 || h1 > 23 || h2 < 0 || h2 > 23 || mm1 < 0 || mm1 > 59 || mm2 < 0 || mm2 > 59 ||
            (ss1 >= 0 && (ss1 < 0 || ss1 > 59)) || (ss2 >= 0 && (ss2 < 0 || ss2 > 59))) {
            return m.group(0, st);
        }

        auto fmtOne = [&](const icu::UnicodeString& hs,
                          const icu::UnicodeString& ms,
                          const icu::UnicodeString& ss,
                          int minute,
                          int second) {
            icu::UnicodeString out = cb.spellout(hs) + icu::UnicodeString::fromUTF8(jgetS(step, "hour_suffix", "点"));
            // Whole-hour ranges should be concise: 09:00-18:00 -> 九点到十八点.
            if (minute != 0 || !ss.isEmpty()) {
                out += cb.spellout(ms) + icu::UnicodeString::fromUTF8(jgetS(step, "minute_suffix", "分"));
            }
            if (!ss.isEmpty() && second != 0) {
                out += cb.spellout(ss) + icu::UnicodeString::fromUTF8(jgetS(step, "second_suffix", "秒"));
            }
            return out;
        };

        icu::UnicodeString left = fmtOne(h1s, m1s, s1s, mm1, ss1);
        icu::UnicodeString right = fmtOne(h2s, m2s, s2s, mm2, ss2);
        return left + icu::UnicodeString::fromUTF8(jgetS(step, "range_sep", "到")) + right;
    }

    return m.group(0, st);
}

icu::UnicodeString TtsNormalizerEngine::applyAction(const std::string& action,
                                                    const nlohmann::json& params,
                                                    icu::RegexMatcher& m,
                                                    UErrorCode& st,
                                                    const TtsCallbacks& cb) const {
    if (action == "exec") {
        return applyExec(params, m, st, cb);
    }
    if (action == "pinyin_han_paren_zh") {
        auto splitPinyin = [](const icu::UnicodeString& input) {
            std::vector<std::string> syllables;
            UErrorCode status = U_ZERO_ERROR;
            icu::UnicodeString pattern =
                "([ptknmlyjhqxfdgbzsrwc]?h?[aeiouüāáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü]+(?:n?g?|r)?)";
            icu::RegexMatcher segmenter(pattern, input, 0, status);
            while (segmenter.find() && U_SUCCESS(status)) {
                icu::UnicodeString part = segmenter.group(1, status);
                std::string s;
                part.toLower().toUTF8String(s);
                syllables.push_back(s);
            }
            return syllables;
        };
        icu::UnicodeString hanPart = m.group(1, st);
        icu::UnicodeString pyPart = m.group(2, st);
        // Regex initials are lowercase-only; normalize so e.g. Tiělǐng → tiělǐng.
        pyPart.toLower();
        std::vector<std::string> syllables = splitPinyin(pyPart);
        int n = static_cast<int>(syllables.size());
        int hanLen = hanPart.length();
        icu::UnicodeString result = (hanLen > n) ? hanPart.tempSubString(0, hanLen - n) : "";
        for (const auto& py : syllables) {
            auto it = pinyinMap_.find(py);
            if (it != pinyinMap_.end()) {
                result += icu::UnicodeString::fromUTF8(it->second);
            } else {
                result += "?";
            }
        }
        return result;
    }
    if (action == "pinyin_standalone_zh") {
        auto splitPinyin = [](const icu::UnicodeString& input) {
            std::vector<std::string> syllables;
            UErrorCode status = U_ZERO_ERROR;
            icu::UnicodeString pattern =
                "([ptknmlyjhqxfdgbzsrwc]?h?[aeiouüāáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü]+(?:n?g?|r)?)";
            icu::RegexMatcher segmenter(pattern, input, 0, status);
            while (segmenter.find() && U_SUCCESS(status)) {
                icu::UnicodeString part = segmenter.group(1, status);
                std::string s;
                part.toLower().toUTF8String(s);
                syllables.push_back(s);
            }
            return syllables;
        };
        icu::UnicodeString pyPart = m.group(1, st);
        pyPart.toLower();
        std::vector<std::string> syllables = splitPinyin(pyPart);
        icu::UnicodeString result;
        for (const auto& py : syllables) {
            auto it = pinyinMap_.find(py);
            if (it != pinyinMap_.end()) {
                result += icu::UnicodeString::fromUTF8(it->second);
            } else {
                result += icu::UnicodeString::fromUTF8(py);
            }
        }
        return result;
    }
    if (action == "emoji_clear") {
        (void)st;
        return icu::UnicodeString();
    }

    return m.group(0, st);
}
icu::UnicodeString TtsNormalizerEngine::runPipeline(const icu::UnicodeString& input,
                                                   const TtsCallbacks& cb) const {
    const bool dbg = ttsNormalizerDebugEnabled();
    std::function<void(icu::RegexMatcher&, UErrorCode&)> logHook;
    const std::function<void(icu::RegexMatcher&, UErrorCode&)>* logPtr = nullptr;
    icu::UnicodeString text = input;
    for (size_t ri = 0; ri < rules_.size(); ++ri) {
        const Rule& r = rules_[ri];
        if (dbg) {
            logHook = [this, ri, &r](icu::RegexMatcher& m, UErrorCode& st) {
                icu::UnicodeString whole = m.group(0, st);
                std::string matchedUtf8;
                whole.toUTF8String(matchedUtf8);
                std::cerr << "[tts_normalizer] locale=" << localeTag_ << " rule_index=" << ri;
                if (!r.id.empty()) {
                    std::cerr << " rule_id=" << r.id;
                }
                if (!r.action.empty()) {
                    std::cerr << " action=" << r.action;
                } else {
                    std::cerr << " action=(replace)";
                }
                std::cerr << " matched=" << truncateForLog(matchedUtf8, 160) << '\n';
            };
            logPtr = &logHook;
        } else {
            logPtr = nullptr;
        }
        if (!r.action.empty()) {
            const std::string& act = r.action;
            text = processRegex(text, r.pattern,
                                [&](icu::RegexMatcher& m, UErrorCode& s) {
                                    return applyAction(act, r.params, m, s, cb);
                                },
                                logPtr);
        } else {
            text = processRegex(text, r.pattern,
                                [&](icu::RegexMatcher& m, UErrorCode& s) {
                                    return expandReplacement(r.replacement, m, s);
                                },
                                logPtr);
        }
    }
    return text;
}
