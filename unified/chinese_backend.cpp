// Chinese backend ("chinese"): ICU cardinal spellout + the zh-specific op/actions that
// Phase 2 moved out of the engine — time_zh_ampm (exec op) and the two pinyin actions.
// The pinyin map is owned here (loaded from config "pinyin"), not by the engine.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <unicode/regex.h>
#include <unicode/unistr.h>

#include "backend.hpp"
#include "icu_backend.hpp"
#include "op_helpers.hpp"

namespace {

using namespace opu; // jgetS, parseIntUStr

// Split a pinyin run into syllables (verbatim from engine's pinyin actions).
std::vector<std::string> splitPinyin(const icu::UnicodeString& input) {
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
}

class ChineseBackend : public IcuSpelloutBackend {
protected:
    bool configureLang(const nlohmann::json& cfg, const std::string& resDir, std::string& err) override {
        std::string pinyin = cfg.value("pinyin", "");
        if (pinyin.empty()) {
            err = "ChineseBackend: config missing \"pinyin\"";
            return false;
        }
        if (!std::filesystem::path(pinyin).is_absolute()) {
            pinyin = (std::filesystem::path(resDir) / pinyin).lexically_normal().string();
        }
        std::ifstream in(pinyin);
        if (!in) {
            err = "ChineseBackend: cannot open pinyin map: " + pinyin;
            return false;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(buffer.str());
        } catch (const std::exception& e) {
            err = std::string("ChineseBackend: pinyin JSON parse error: ") + e.what();
            return false;
        }
        if (!j.is_object()) {
            err = "ChineseBackend: pinyin map must be a JSON object";
            return false;
        }
        for (auto it = j.begin(); it != j.end(); ++it) {
            pinyinMap_[it.key()] = it.value().get<std::string>();
        }
        return true;
    }

    void registerOps(TtsNormalizerEngine& engine) override {
        engine.registerOp("time_zh_ampm", [](const nlohmann::json& step, icu::RegexMatcher& m,
                                             UErrorCode& st, const TtsCallbacks& cb) -> icu::UnicodeString {
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
        });

        // Pinyin actions read this backend's own map; capture `this` (stable unique_ptr).
        engine.registerAction("pinyin_han_paren_zh", [this](const nlohmann::json& /*params*/, icu::RegexMatcher& m,
                                                            UErrorCode& st, const TtsCallbacks& /*cb*/) -> icu::UnicodeString {
            icu::UnicodeString hanPart = m.group(1, st);
            icu::UnicodeString pyPart = m.group(2, st);
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
        });

        engine.registerAction("pinyin_standalone_zh", [this](const nlohmann::json& /*params*/, icu::RegexMatcher& m,
                                                             UErrorCode& st, const TtsCallbacks& /*cb*/) -> icu::UnicodeString {
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
        });
    }

private:
    std::unordered_map<std::string, std::string> pinyinMap_;
};

BackendAutoRegister _reg_zh("chinese", [] { return std::unique_ptr<LanguageBackend>(new ChineseBackend()); });

} // namespace
