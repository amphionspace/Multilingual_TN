#include "tts_normalizer_engine.hpp"
// Phase 1: engine no longer hard-links Russian year code; parseRussianSpokenYear is
// now supplied by the Russian backend via TtsCallbacks::parse_spoken_year.

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
        if (c >= 0xFF10 && c <= 0xFF19) {
            normal += static_cast<char>('0' + (c - 0xFF10));
        } else if (c >= 0x0660 && c <= 0x0669) {
            normal += static_cast<char>('0' + (c - 0x0660));
        } else if (c >= 0x06F0 && c <= 0x06F9) {
            normal += static_cast<char>('0' + (c - 0x06F0));
        } else if (c >= '0' && c <= '9') {
            normal += static_cast<UChar>(c);
        } else if (c >= 0x2080 && c <= 0x2089) {
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

static icu::UnicodeString pickMapStr(const njson& v, const std::string& fallbackUtf8) {
    if (v.is_string()) {
        return icu::UnicodeString::fromUTF8(v.get<std::string>());
    }
    if (v.is_object()) {
        if (v.contains("value") && v["value"].is_string()) {
            return icu::UnicodeString::fromUTF8(v["value"].get<std::string>());
        }
        if (v.contains("default") && v["default"].is_string()) {
            return icu::UnicodeString::fromUTF8(v["default"].get<std::string>());
        }
        static const char* kFallbackLocaleOrder[] = {"en", "zh", "ar", "bn", "ru"};
        for (const char* key : kFallbackLocaleOrder) {
            if (v.contains(key) && v[key].is_string()) {
                return icu::UnicodeString::fromUTF8(v[key].get<std::string>());
            }
        }
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it.value().is_string()) {
                return icu::UnicodeString::fromUTF8(it.value().get<std::string>());
            }
        }
    }
    return icu::UnicodeString::fromUTF8(fallbackUtf8);
}

// English month token (full name or common abbreviation) → 1–12; else 0.
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
    return loadRulesFromJson(j, errOut);
}

bool TtsNormalizerEngine::loadRulesV2(const std::string& jsonPath, std::string& errOut) {
    std::ifstream in(jsonPath);
    if (!in) {
        errOut = "Cannot open rules_v2 file: " + jsonPath;
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    nlohmann::json jv2;
    try {
        jv2 = nlohmann::json::parse(buffer.str());
    } catch (const std::exception& e) {
        errOut = std::string("JSON parse error in rules_v2: ") + e.what();
        return false;
    }
    if (!jv2.is_object()) {
        errOut = "rules_v2 root must be an object";
        return false;
    }
    if (jv2.value("format", "") != "tts_rules_v2") {
        errOut = "rules_v2 requires format=\"tts_rules_v2\"";
        return false;
    }
    if (!jv2.contains("pipeline") || !jv2["pipeline"].is_object()) {
        errOut = "rules_v2 missing object field: pipeline";
        return false;
    }

    // Skeleton adapter: map rules_v2 to the runtime document used by loadRulesFromJson.
    nlohmann::json j = nlohmann::json::object();
    j["locale"] = jv2.value("locale", "en");
    const auto resources = jv2.value("resources", nlohmann::json::object());
    j["digit_names"] = resources.value("digit_names", nlohmann::json::array());
    j["currency_map"] = resources.value("currency_map", nlohmann::json::object());
    j["unit_composite"] = resources.value("unit_composite", nlohmann::json::object());
    j["unit_single"] = resources.value("unit_single", nlohmann::json::object());
    if (resources.contains("month_names")) {
        j["month_names"] = resources["month_names"];
    }
    if (resources.contains("pattern_defs")) {
        j["pattern_defs"] = resources["pattern_defs"];
    }
    j["rules"] = jv2["pipeline"].value("rules", nlohmann::json::array());
    return loadRulesFromJson(j, errOut);
}

bool TtsNormalizerEngine::loadRulesFromJson(const nlohmann::json& j, std::string& errOut) {

    localeTag_ = j.value("locale", "en");

    monthNames_.assign(13, icu::UnicodeString());
    static const char* kDefMonths[] = {"", "January", "February", "March", "April", "May", "June",
                                       "July", "August", "September", "October", "November", "December"};
    for (int i = 1; i <= 12; ++i) {
        monthNames_[static_cast<size_t>(i)] = icu::UnicodeString::fromUTF8(kDefMonths[i]);
    }
    auto loadMonthNames = [&](const char* key) -> bool {
        if (!j.contains(key) || !j[key].is_array()) {
            return false;
        }
        const auto& arr = j[key];
        bool loaded = false;
        for (size_t i = 0; i < arr.size() && i < 12; ++i) {
            if (arr[i].is_string()) {
                monthNames_[1 + i] = icu::UnicodeString::fromUTF8(arr[i].get<std::string>());
                loaded = true;
            }
        }
        return loaded;
    };
    // Locale-specific month naming is rule-owned data. Prefer generic `month_names`,
    // keep legacy keys for backward compatibility with existing JSON.
    if (!loadMonthNames("month_names")) {
        loadMonthNames("month_names_en") || loadMonthNames("month_names_ar") || loadMonthNames("month_names_bn") ||
            loadMonthNames("month_names_ru");
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
    // Phase 0: a backend-registered handler wins over the built-in branch of the same name.
    {
        auto it = customOps_.find(op);
        if (it != customOps_.end()) {
            return it->second(step, m, st, cb);
        }
    }
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
            // Phase 2: English year prose delegated to the backend via callback.
            return cb.year_prose ? cb.year_prose(y) : cb.spellout(part);
        }
        if (as == "month_en_full") {
            // Phase 2: English month naming delegated to the backend via callback.
            return cb.month_en_full ? cb.month_en_full(part) : part;
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
        const bool otherSpace = step.value("other_space", step.value("other_en_space", false));
        const std::string letterStyle = jgetS(step, "letter", "keep");
        icu::UnicodeString res;
        for (int i = 0; i < raw.length(); ++i) {
            UChar c = raw.charAt(i);
            // The map takes precedence over the built-in Latin/digit defaults, so a
            // map can spell out Latin letters by name (e.g. "a" -> "эй") or drop
            // separators ("." -> ""). Existing maps only key punctuation, so this
            // does not change their behavior.
            icu::UnicodeString oneChar;
            oneChar += c;
            std::string keyUtf8;
            oneChar.toUTF8String(keyUtf8);
            if (map.contains(keyUtf8)) {
                res += pickMapStr(map[keyUtf8], keyUtf8);
                continue;
            }
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
            res += c;
            if (otherSpace) {
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
            const bool singular = step.value("singular", step.value("singular_en", false)) && numG > 0 &&
                                  numericIsOne(m.group(numG, st));
            if (singular) {
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
        const bool singular = step.value("singular", false) && numG > 0 && numericIsOne(m.group(numG, st));
        const auto& tbl = (table == "composite") ? unitComposite_ : unitSingle_;
        auto it = tbl.find(u8key);
        if (it != tbl.end()) {
            return singular ? toSingularEn(it->second) : it->second;
        }
        return u;
    }
    if (op == "ordinal") {
        const int g = step.value("g", 1);
        icu::UnicodeString ord = cb.ordinal_spellout(m.group(g, st), st);
        return icu::UnicodeString::fromUTF8(jgetS(step, "pad_before", " ")) + ord +
               icu::UnicodeString::fromUTF8(jgetS(step, "pad_after", " "));
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
        const bool pad = step.value("pad", false);
        if (pad) {
            return " " + cb.spellout(cleaned) + " ";
        }
        return cb.spellout(cleaned);
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
    return m.group(0, st);
}

icu::UnicodeString TtsNormalizerEngine::applyAction(const std::string& action,
                                                    const nlohmann::json& params,
                                                    icu::RegexMatcher& m,
                                                    UErrorCode& st,
                                                    const TtsCallbacks& cb) const {
    // Phase 2: a backend-registered action handler wins over the built-in branch.
    {
        auto it = customActions_.find(action);
        if (it != customActions_.end()) {
            return it->second(params, m, st, cb);
        }
    }
    if (action == "exec") {
        return applyExec(params, m, st, cb);
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
