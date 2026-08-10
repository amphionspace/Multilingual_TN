#include "text_normalizer.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <unicode/unistr.h>

namespace {

// Resolve a path from config.json relative to the config directory; absolute paths pass through.
std::string resolvePath(const std::string& resDir, const std::string& p) {
    if (p.empty()) {
        return p;
    }
    std::filesystem::path path(p);
    if (path.is_absolute()) {
        return path.string();
    }
    return (std::filesystem::path(resDir) / path).lexically_normal().string();
}

} // namespace

bool TextNormalizer::Init(const std::string& resourceDir) {
    const std::string cfgPath = (std::filesystem::path(resourceDir) / "config.json").string();
    std::ifstream in(cfgPath);
    if (!in) {
        err_ = "cannot open config: " + cfgPath;
        return false;
    }
    nlohmann::json cfg;
    try {
        std::stringstream ss;
        ss << in.rdbuf();
        cfg = nlohmann::json::parse(ss.str());
    } catch (const std::exception& e) {
        err_ = std::string("config parse error: ") + e.what();
        return false;
    }

    const std::string backendId = cfg.value("backend", "");
    if (backendId.empty()) {
        err_ = "config missing \"backend\"";
        return false;
    }
    locale_ = cfg.value("locale", "");

    backend_ = BackendRegistry::instance().create(backendId);
    if (!backend_) {
        err_ = "unknown backend id: " + backendId;
        return false;
    }
    if (!backend_->configure(cfg, resourceDir, err_)) {
        return false;
    }

    const std::string rules = resolvePath(resourceDir, cfg.value("rules", ""));
    if (rules.empty()) {
        err_ = "config missing \"rules\"";
        return false;
    }
    if (!engine_.loadRulesV2(rules, err_)) {
        return false;
    }

    const std::string pinyin = cfg.value("pinyin", "");
    if (!engine_.loadPinyinMap(pinyin.empty() ? "" : resolvePath(resourceDir, pinyin), err_)) {
        return false;
    }

    backend_->registerOps(engine_);
    return true;
}

std::string TextNormalizer::normalizeLine(const std::string& inputUtf8) {
    backend_->prepare(inputUtf8);
    if (backend_->handlesNormalizationDirectly()) {
        return backend_->normalizeDirect(inputUtf8, engine_);
    }
    icu::UnicodeString text = icu::UnicodeString::fromUTF8(inputUtf8);
    text = engine_.runPipeline(text, backend_->callbacks());
    std::string out;
    text.toUTF8String(out);
    return out;
}
