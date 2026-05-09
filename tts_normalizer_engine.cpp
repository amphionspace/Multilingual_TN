#include "tts_normalizer_engine.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
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
        icu::UnicodeString res;
        for (int i = 0; i < raw.length(); ++i) {
            UChar c = raw.charAt(i);
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
    if (op == "roman") {
        const int g = step.value("g", 0);
        icu::UnicodeString roman = m.group(g, st);
        if (roman.length() <= 1) {
            return roman;
        }
        icu::UnicodeString arabicStr = romanToArabicStr(roman);
        return cb.spellout(arabicStr) + " ";
    }
    if (op == "integral") {
        const std::string variant = jgetS(step, "variant", "en");
        const int g1 = step.value("g1", 1);
        const int g2 = step.value("g2", 2);
        icu::UnicodeString X_val = toNormalDigitsMixed(m.group(g1, st));
        icu::UnicodeString Y_val = toNormalDigitsMixed(m.group(g2, st));
        if (variant == "zh") {
            icu::UnicodeString result = "从";
            result += X_val;
            result += "到";
            result += Y_val;
            result += "积分";
            return result;
        }
        if (variant == "ar") {
            icu::UnicodeString result = icu::UnicodeString::fromUTF8(jgetS(step, "prefix_ar", "من "));
            result += X_val;
            result += icu::UnicodeString::fromUTF8(jgetS(step, "mid_ar", " إلى "));
            result += Y_val;
            result += icu::UnicodeString::fromUTF8(jgetS(step, "suffix_ar", " تكامل"));
            return result;
        }
        if (variant == "bn") {
            icu::UnicodeString result = icu::UnicodeString::fromUTF8(jgetS(step, "prefix_bn", ""));
            result += X_val;
            result += icu::UnicodeString::fromUTF8(jgetS(step, "mid_bn", " থেকে "));
            result += Y_val;
            result += icu::UnicodeString::fromUTF8(jgetS(step, "suffix_bn", " পর্যন্ত সমাকল"));
            return result;
        }
        icu::UnicodeString result = "integral from ";
        result += X_val;
        result += " to ";
        result += Y_val;
        return result;
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
        return " " + monthNameForIsoDate(mon) + " " + dayOrdinal + ", " + cb.spellout(m.group(gy, st)) + " ";
    }
    if (op == "date_ymd") {
        const std::string yearMode = jgetS(step, "year_mode", "spellout");
        const std::string monthMode = jgetS(step, "month_mode", "spellout");
        const std::string dayMode = jgetS(step, "day_mode", "spellout");
        const std::string yearSuffix = jgetS(step, "year_suffix", "年");
        const std::string monthSuffix = jgetS(step, "month_suffix", "月");
        const std::string daySuffix = jgetS(step, "day_suffix", "日");
        const int g1 = step.value("g1", 1);
        const int g2 = step.value("g2", 2);
        const int g3 = step.value("g3", 3);

        auto renderPart = [&](const icu::UnicodeString& part, const std::string& mode) {
            if (mode == "digits") {
                icu::UnicodeString out;
                for (int i = 0; i < part.length(); ++i) {
                    out += digitNameChar(part.charAt(i));
                }
                return out;
            }
            return cb.spellout(part);
        };

        return renderPart(m.group(g1, st), yearMode) + icu::UnicodeString::fromUTF8(yearSuffix) +
               renderPart(m.group(g2, st), monthMode) + icu::UnicodeString::fromUTF8(monthSuffix) +
               renderPart(m.group(g3, st), dayMode) + icu::UnicodeString::fromUTF8(daySuffix);
    }
    if (op == "minus") {
        const int g = step.value("g", 1);
        const std::string enp = jgetS(step, "minus_en", "minus ");
        const std::string zhp = jgetS(step, "minus_zh", "负");
        const std::string arp = jgetS(step, "minus_ar", "سالب ");
        const std::string bnp = jgetS(step, "minus_bn", "মাইনাস ");
        const std::string rup = jgetS(step, "minus_ru", "минус ");
        icu::UnicodeString prefix = icu::UnicodeString::fromUTF8(zhp);
        if (localeTag_ == "en") {
            prefix = icu::UnicodeString::fromUTF8(enp);
        } else if (localeTag_ == "ar") {
            prefix = icu::UnicodeString::fromUTF8(arp);
        } else if (localeTag_ == "bn") {
            prefix = icu::UnicodeString::fromUTF8(bnp);
        } else if (localeTag_ == "ru") {
            prefix = icu::UnicodeString::fromUTF8(rup);
        }
        return prefix + m.group(g, st);
    }
    if (op == "spellout_affix") {
        const int g = step.value("g", 1);
        const std::string pre = jgetS(step, "prefix", "");
        const std::string suf = jgetS(step, "suffix", "");
        return icu::UnicodeString::fromUTF8(pre) + cb.spellout(m.group(g, st)) + icu::UnicodeString::fromUTF8(suf);
    }
    if (op == "currency") {
        icu::UnicodeString sym;
        icu::UnicodeString rawNum;
        if (step.value("two_group", false)) {
            sym = m.group(step.value("sym_g", 1), st);
            rawNum = m.group(step.value("num_g", 2), st);
        } else {
            const int sym1 = step.value("sym_g1", 1);
            const int numg = step.value("num_g", 2);
            const int sym2 = step.value("sym_g2", 3);
            icu::UnicodeString symBefore = m.group(sym1, st);
            rawNum = m.group(numg, st);
            icu::UnicodeString symAfter = m.group(sym2, st);
            sym = (symBefore.length() > 0) ? symBefore : symAfter;
        }
        if (sym.isEmpty()) {
            return m.group(0, st);
        }
        icu::UnicodeString cleanNum;
        const std::string stripChars = jgetS(step, "strip", ",");
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
        icu::UnicodeString num = cb.spellout(cleanNum);
        std::string symUtf8;
        sym.toUTF8String(symUtf8);
        auto it = currencyMap_.find(symUtf8);
        if (it != currencyMap_.end()) {
            if (localeTag_ == "en" && step.value("pad_en", true)) {
                return " " + num + " " + it->second + " ";
            }
            return num + it->second;
        }
        if (localeTag_ == "en") {
            return " " + num + icu::UnicodeString::fromUTF8(jgetS(step, "fallback_en", " dollars "));
        }
        if (localeTag_ == "ar") {
            return num + icu::UnicodeString::fromUTF8(jgetS(step, "fallback_ar", "درهم "));
        }
        if (localeTag_ == "bn") {
            return num + icu::UnicodeString::fromUTF8(jgetS(step, "fallback_bn", "টাকা "));
        }
        if (localeTag_ == "ru") {
            return num + icu::UnicodeString::fromUTF8(jgetS(step, "fallback_ru", "рублей"));
        }
        return num + icu::UnicodeString::fromUTF8(jgetS(step, "fallback_zh", "元"));
    }
    if (op == "unit") {
        const int numG = step.value("num_g", 1);
        const int unitG = step.value("unit_g", 2);
        const std::string table = jgetS(step, "table", "single");
        icu::UnicodeString n = cb.spellout(m.group(numG, st));
        icu::UnicodeString u = m.group(unitG, st);
        std::string u8;
        u.toUTF8String(u8);
        if (table == "composite") {
            auto it = unitComposite_.find(u8);
            if (it != unitComposite_.end()) {
                if (localeTag_ == "en" && step.value("pad_en", true)) {
                    return " " + n + it->second;
                }
                return n + it->second;
            }
            if (localeTag_ == "en" && step.value("pad_en", true)) {
                return " " + n + u + " ";
            }
            return n + u;
        }
        auto it = unitSingle_.find(u8);
        if (it != unitSingle_.end()) {
            if (localeTag_ == "en" && step.value("pad_en", true)) {
                return " " + n + it->second;
            }
            return n + it->second;
        }
        if (localeTag_ == "en" && step.value("pad_en", true)) {
            return " " + n + u + " ";
        }
        return n + u;
    }
    if (op == "ordinal") {
        const int g = step.value("g", 1);
        icu::UnicodeString ord = cb.ordinal_spellout(m.group(g, st), st);
        return icu::UnicodeString::fromUTF8(jgetS(step, "pad_before", " ")) + ord +
               icu::UnicodeString::fromUTF8(jgetS(step, "pad_after", " "));
    }
    if (op == "loose_num") {
        const int g = step.value("g", 0);
        if (localeTag_ == "en") {
            return " " + cb.spellout(m.group(g, st)) + " ";
        }
        return cb.spellout(m.group(g, st));
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
    if (op == "decade_zh") {
        const int g = step.value("g", 1);
        const icu::UnicodeString raw = m.group(g, st);
        icu::UnicodeString digits;
        for (int i = 0; i < raw.length(); ++i) {
            UChar c = raw.charAt(i);
            if (c >= '0' && c <= '9') {
                digits += digitNameChar(c);
            }
        }
        return digits + icu::UnicodeString::fromUTF8(jgetS(step, "suffix", "年代"));
    }
    if (op == "walk_readout") {
        const int g = step.value("g", 1);
        const icu::UnicodeString raw = m.group(g, st);
        const nlohmann::json map = step.value("map", nlohmann::json::object());
        const std::string letterStyle = jgetS(step, "letter", "spaced");
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
            } else if (c >= '0' && c <= '9') {
                res += digitNameChar(c);
            } else {
                icu::UnicodeString oneChar;
                oneChar += c;
                std::string keyUtf8;
                oneChar.toUTF8String(keyUtf8);
                if (map.contains(keyUtf8) && map[keyUtf8].is_string()) {
                    res += icu::UnicodeString::fromUTF8(map[keyUtf8].get<std::string>());
                } else {
                    res += c;
                }
            }
        }
        return res;
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
    if (op == "dms_zh") {
        const int g1 = step.value("g1", 1);
        const int g2 = step.value("g2", 2);
        const int g3 = step.value("g3", 3);
        icu::UnicodeString deg = cb.spellout(m.group(g1, st));
        icu::UnicodeString minute = cb.spellout(m.group(g2, st));
        icu::UnicodeString second = cb.spellout(m.group(g3, st));
        return deg + icu::UnicodeString::fromUTF8(jgetS(step, "deg", "度")) + minute +
               icu::UnicodeString::fromUTF8(jgetS(step, "min", "分")) + second +
               icu::UnicodeString::fromUTF8(jgetS(step, "sec", "秒"));
    }
    if (op == "pow10") {
        const int g = step.value("g", 1);
        icu::UnicodeString superscripts = m.group(g, st);
        bool neg = false;
        icu::UnicodeString normalNum = superscriptsToNormal(superscripts, neg);
        if (localeTag_ == "en") {
            icu::UnicodeString result = icu::UnicodeString::fromUTF8(jgetS(step, "prefix_en", "ten to the power of "));
            if (neg) {
                result += icu::UnicodeString::fromUTF8(jgetS(step, "minus_en", "minus "));
            }
            result += cb.spellout(normalNum);
            return result;
        }
        if (localeTag_ == "ar") {
            icu::UnicodeString result = icu::UnicodeString::fromUTF8(jgetS(step, "prefix_ar", "عشرة أس "));
            if (neg) {
                result += icu::UnicodeString::fromUTF8(jgetS(step, "minus_ar", "سالب "));
            }
            result += cb.spellout(normalNum);
            result += icu::UnicodeString::fromUTF8(jgetS(step, "suffix_ar", ""));
            return result;
        }
        if (localeTag_ == "bn") {
            icu::UnicodeString result = icu::UnicodeString::fromUTF8(jgetS(step, "prefix_bn", "দশের ঘাত "));
            if (neg) {
                result += icu::UnicodeString::fromUTF8(jgetS(step, "minus_bn", "ঋণাত্মক "));
            }
            result += cb.spellout(normalNum);
            result += icu::UnicodeString::fromUTF8(jgetS(step, "suffix_bn", ""));
            return result;
        }
        if (localeTag_ == "ru") {
            icu::UnicodeString result = icu::UnicodeString::fromUTF8(jgetS(step, "prefix_ru", "десять в степени "));
            if (neg) {
                result += icu::UnicodeString::fromUTF8(jgetS(step, "minus_ru", "минус "));
            }
            result += cb.spellout(normalNum);
            result += icu::UnicodeString::fromUTF8(jgetS(step, "suffix_ru", ""));
            return result;
        }
        icu::UnicodeString result = icu::UnicodeString::fromUTF8(jgetS(step, "prefix_zh", "十的"));
        if (neg) {
            result += icu::UnicodeString::fromUTF8(jgetS(step, "minus_zh", "负"));
        }
        result += cb.spellout(normalNum);
        result += icu::UnicodeString::fromUTF8(jgetS(step, "suffix_zh", "次方"));
        return result;
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
    if (op == "time_zh_24") {
        const int gh = step.value("hour_g", 1);
        const int gm = step.value("minute_g", 2);
        const int gs = step.value("second_g", 3);
        icu::UnicodeString hourStr = m.group(gh, st);
        icu::UnicodeString minuteStr = m.group(gm, st);
        icu::UnicodeString secondStr = m.group(gs, st);
        icu::UnicodeString res = cb.spellout(hourStr) + icu::UnicodeString::fromUTF8(jgetS(step, "colon", "点")) +
                                 cb.spellout(minuteStr) + icu::UnicodeString::fromUTF8(jgetS(step, "minute_suffix", "分"));
        if (!secondStr.isEmpty()) {
            res += cb.spellout(secondStr) + icu::UnicodeString::fromUTF8(jgetS(step, "second_suffix", "秒"));
        }
        return res;
    }
    if (op == "time_clock") {
        const int gh = step.value("hour_g", 1);
        const int gm = step.value("minute_g", 2);
        const int gs = step.value("second_g", 3);
        icu::UnicodeString hourStr = m.group(gh, st);
        icu::UnicodeString minuteStr = m.group(gm, st);
        icu::UnicodeString secondStr = m.group(gs, st);
        const std::string colon = jgetS(step, "colon", " ");
        const std::string minSuf = jgetS(step, "minute_suffix", "");
        const std::string secSuf = jgetS(step, "second_suffix", "");
        icu::UnicodeString res =
            cb.spellout(hourStr) + icu::UnicodeString::fromUTF8(colon) + cb.spellout(minuteStr) + icu::UnicodeString::fromUTF8(minSuf);
        if (!secondStr.isEmpty()) {
            res += cb.spellout(secondStr) + icu::UnicodeString::fromUTF8(secSuf);
        }
        return res;
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
