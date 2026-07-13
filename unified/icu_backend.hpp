#pragma once
// Reusable ICU-RBNF spellout base. Provides cardinal + ordinal callbacks selected by
// config (locale + ordinal mode). English/Arabic/Chinese backends inherit this and add
// their own ops/hooks; a plain instance ("icu") serves any new ICU-covered language.
#include <cstdlib>
#include <string>

#include <unicode/locid.h>
#include <unicode/rbnf.h>
#include <unicode/unistr.h>

#include "backend.hpp"

class IcuSpelloutBackend : public LanguageBackend {
public:
    ~IcuSpelloutBackend() override { delete spellout_; }

    bool configure(const nlohmann::json& cfg, const std::string& resDir, std::string& err) override {
        if (!configureIcu(cfg, err)) {
            return false;
        }
        return configureLang(cfg, resDir, err);
    }

    const TtsCallbacks& callbacks() const override { return cb_; }

protected:
    // Set up the ICU spellout formatter + cardinal/ordinal callbacks. Called by configure().
    bool configureIcu(const nlohmann::json& cfg, std::string& err) {
        locale_ = cfg.value("locale", "en");
        ordinalMode_ = cfg.value("ordinal", "icu_int"); // icu_int | icu_double | cardinal

        UErrorCode status = U_ZERO_ERROR;
        spellout_ = new icu::RuleBasedNumberFormat(icu::URBNF_SPELLOUT, icu::Locale(locale_.c_str()), status);
        if (U_FAILURE(status)) {
            err = "IcuSpelloutBackend: failed to init spellout formatter for locale " + locale_;
            return false;
        }

        cb_.spellout = [this](const icu::UnicodeString& n) { return spelloutCardinal(n); };
        if (ordinalMode_ == "cardinal") {
            cb_.ordinal_spellout = [this](const icu::UnicodeString& n, UErrorCode& st) {
                (void)st;
                return spelloutCardinal(n);
            };
        } else if (ordinalMode_ == "icu_double") {
            cb_.ordinal_spellout = [this](const icu::UnicodeString& n, UErrorCode& st) {
                return spelloutOrdinalDouble(n, st);
            };
        } else {
            cb_.ordinal_spellout = [this](const icu::UnicodeString& n, UErrorCode& st) {
                return spelloutOrdinalInt(n, st);
            };
        }
        return true;
    }

    // Language-specific extension point (extra callbacks, resource loading). Default: no-op.
    virtual bool configureLang(const nlohmann::json& /*cfg*/, const std::string& /*resDir*/, std::string& /*err*/) {
        return true;
    }

    // Mirrors en/zh/ar numberTo<Lang>(): stod -> default spellout ruleset.
    icu::UnicodeString spelloutCardinal(const icu::UnicodeString& numStr) {
        UErrorCode status = U_ZERO_ERROR;
        std::string utf8;
        numStr.toUTF8String(utf8);
        try {
            const double val = std::stod(utf8);
            icu::UnicodeString result;
            spellout_->format(val, result, status);
            return result;
        } catch (...) {
            return numStr;
        }
    }

    // Mirrors en numberToOrdinalEnglish(): integer path via %spellout-ordinal.
    icu::UnicodeString spelloutOrdinalInt(const icu::UnicodeString& numStr, UErrorCode& status) {
        if (U_FAILURE(status)) {
            return numStr;
        }
        std::string utf8;
        numStr.toUTF8String(utf8);
        int64_t value = 0;
        try {
            value = std::stoll(utf8);
        } catch (...) {
            status = U_PARSE_ERROR;
            return icu::UnicodeString();
        }
        const icu::UnicodeString ruleSet("%spellout-ordinal");
        icu::UnicodeString result;
        icu::FieldPosition pos(icu::FieldPosition::DONT_CARE);
        spellout_->format(value, ruleSet, result, pos, status);
        return result;
    }

    // Mirrors ar numberToOrdinalArabic(): double path via %spellout-ordinal.
    icu::UnicodeString spelloutOrdinalDouble(const icu::UnicodeString& numStr, UErrorCode& status) {
        if (U_FAILURE(status)) {
            return numStr;
        }
        const icu::UnicodeString ruleSet("%spellout-ordinal");
        std::string utf8;
        numStr.toUTF8String(utf8);
        const double value = std::atof(utf8.c_str());
        icu::UnicodeString result;
        icu::FieldPosition pos(icu::FieldPosition::DONT_CARE);
        spellout_->format(value, ruleSet, result, pos, status);
        return result;
    }

    icu::RuleBasedNumberFormat* spellout_ = nullptr;
    std::string locale_;
    std::string ordinalMode_;
    TtsCallbacks cb_;
};
