#include "frontend_rules_merge.hpp"

#include <filesystem>

namespace frontend {
namespace {

constexpr const char* kFallbackLang = "en";

const nlohmann::json* frontendBlock(const nlohmann::json& doc) {
    if (!doc.contains("frontend") || !doc["frontend"].is_object()) {
        return nullptr;
    }
    return &doc["frontend"];
}

nlohmann::json mergeFrontendDocs(const nlohmann::json& base, const nlohmann::json& extension) {
    nlohmann::json merged = base;
    if (!extension.contains("resources") || !extension["resources"].is_object()) {
        return merged;
    }
    auto& baseResources = merged["resources"];
    const auto& extResources = extension["resources"];
    for (const char* bucket : {"char_maps", "char_sets", "constants"}) {
        if (!extResources.contains(bucket)) {
            continue;
        }
        if (!baseResources.contains(bucket)) {
            baseResources[bucket] = nlohmann::json::object();
        }
        for (auto it = extResources[bucket].begin(); it != extResources[bucket].end(); ++it) {
            baseResources[bucket][it.key()] = it.value();
        }
    }
    if (extension.contains("pre_stages")) {
        merged["pre_stages"] = extension["pre_stages"];
    }
    if (extension.contains("post_stages")) {
        merged["post_stages"] = extension["post_stages"];
    } else if (extension.contains("post_stages_extra")) {
        nlohmann::json post = merged.value("post_stages", nlohmann::json::array());
        size_t insertIdx = post.size();
        for (size_t i = 0; i < post.size(); ++i) {
            if (post[i].value("op", "") == "ensure_trailing_sentence_punct") {
                insertIdx = i;
                break;
            }
        }
        nlohmann::json mergedPost = nlohmann::json::array();
        for (size_t i = 0; i < insertIdx; ++i) {
            mergedPost.push_back(post[i]);
        }
        for (const auto& stage : extension["post_stages_extra"]) {
            mergedPost.push_back(stage);
        }
        for (size_t i = insertIdx; i < post.size(); ++i) {
            mergedPost.push_back(post[i]);
        }
        merged["post_stages"] = mergedPost;
    }
    return merged;
}

nlohmann::json loadMergedFrontendDoc(
    const nlohmann::json& enDoc,
    const nlohmann::json& localeDoc,
    const std::string& locale) {
    const nlohmann::json* enFrontend = frontendBlock(enDoc);
    if (enFrontend == nullptr) {
        return nlohmann::json::object();
    }
    if (locale == kFallbackLang) {
        return *enFrontend;
    }
    const nlohmann::json* localeFrontend = frontendBlock(localeDoc);
    if (localeFrontend == nullptr) {
        return *enFrontend;
    }
    if (localeFrontend->contains("pre_stages")) {
        return *localeFrontend;
    }
    return mergeFrontendDocs(*enFrontend, *localeFrontend);
}

bool isInlineFeRule(const nlohmann::json& rule) {
    if (!rule.contains("id") || !rule["id"].is_string()) {
        return false;
    }
    const std::string id = rule["id"].get<std::string>();
    return id.rfind("zh_fe_", 0) == 0 || id.rfind("ru_fe_", 0) == 0;
}

bool isG2pSandhiOnlyProfile(const nlohmann::json& localeDoc) {
    if (!localeDoc.contains("metadata") || !localeDoc["metadata"].is_object()) {
        return false;
    }
    const auto& md = localeDoc["metadata"];
    if (md.value("pipeline_kind", "") == "g2p_sandhi") {
        return true;
    }
    return md.value("g2p_sandhi_only", false);
}

void appendFrontendStages(nlohmann::json& flatRules, const nlohmann::json& stages) {
    if (!stages.is_array()) {
        return;
    }
    for (const auto& stage : stages) {
        if (!stage.is_object() || !stage.contains("op")) {
            continue;
        }
        nlohmann::json rule = nlohmann::json::object();
        if (stage.contains("id")) {
            rule["id"] = stage["id"];
        }
        rule["action"] = "frontend_op";
        rule["op"] = stage["op"];
        rule["params"] = stage.value("params", nlohmann::json::object());
        flatRules.push_back(std::move(rule));
    }
}

} // namespace

bool flattenRulesV2Pipeline(
    const nlohmann::json& localeDoc,
    const nlohmann::json& enDoc,
    nlohmann::json& outRuntimeDoc,
    std::string& errOut) {
    if (!localeDoc.contains("pipeline") || !localeDoc["pipeline"].is_object()) {
        errOut = "rules_v2 missing object field: pipeline";
        return false;
    }

    const std::string locale = localeDoc.value("locale", kFallbackLang);
    // G2P sandhi runs on a pinyin token stream after TN; do not inherit TN frontend bookends.
    const nlohmann::json frontendDoc =
        isG2pSandhiOnlyProfile(localeDoc) ? nlohmann::json::object()
                                          : loadMergedFrontendDoc(enDoc, localeDoc, locale);

    nlohmann::json coreRules = nlohmann::json::array();
    for (const auto& rule : localeDoc["pipeline"].value("rules", nlohmann::json::array())) {
        if (isInlineFeRule(rule)) {
            continue;
        }
        coreRules.push_back(rule);
    }

    nlohmann::json flatRules = nlohmann::json::array();
    if (frontendDoc.contains("pre_stages")) {
        appendFrontendStages(flatRules, frontendDoc["pre_stages"]);
    }
    for (const auto& rule : coreRules) {
        flatRules.push_back(rule);
    }
    if (frontendDoc.contains("post_stages")) {
        appendFrontendStages(flatRules, frontendDoc["post_stages"]);
    }

    if (frontendDoc.contains("resources")) {
        outRuntimeDoc["frontend_resources"] = nlohmann::json{
            {"resources", frontendDoc["resources"]},
        };
    } else {
        outRuntimeDoc["frontend_resources"] = nlohmann::json::object();
    }
    outRuntimeDoc["rules"] = std::move(flatRules);
    return true;
}

} // namespace frontend
