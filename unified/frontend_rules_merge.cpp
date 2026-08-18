#include "frontend_rules_merge.hpp"

#include "frontend_ops.hpp"

#include <vector>

namespace frontend {
namespace {

constexpr const char* kFallbackLang = "en";
constexpr size_t kMaxCompositeDepth = 32;

const nlohmann::json* frontendBlock(const nlohmann::json& doc) {
    if (!doc.contains("frontend") || !doc["frontend"].is_object()) {
        return nullptr;
    }
    return &doc["frontend"];
}

bool validateFrontendBlockShape(
    const nlohmann::json& doc,
    const std::string& label,
    std::string& errOut) {
    if (!doc.contains("frontend")) {
        return true;
    }
    if (!doc["frontend"].is_object()) {
        errOut = label + ".frontend must be an object";
        return false;
    }
    const auto& frontend = doc["frontend"];
    for (const char* key : {"pre_stages", "post_stages", "post_stages_extra"}) {
        if (frontend.contains(key) && !frontend[key].is_array()) {
            errOut = label + ".frontend." + key + " must be an array";
            return false;
        }
    }
    if (frontend.contains("op_defs") && !frontend["op_defs"].is_object()) {
        errOut = label + ".frontend.op_defs must be an object";
        return false;
    }
    if (!frontend.contains("resources")) {
        return true;
    }
    if (!frontend["resources"].is_object()) {
        errOut = label + ".frontend.resources must be an object";
        return false;
    }
    const auto& resources = frontend["resources"];
    for (const char* bucket : {"char_maps", "char_sets", "constants", "pattern_defs"}) {
        if (resources.contains(bucket) && !resources[bucket].is_object()) {
            errOut = label + ".frontend.resources." + bucket + " must be an object";
            return false;
        }
    }
    return true;
}

void mergeObjectEntries(
    nlohmann::json& destination,
    const nlohmann::json& source,
    const char* key) {
    if (!source.contains(key) || !source[key].is_object()) {
        return;
    }
    if (!destination.contains(key) || !destination[key].is_object()) {
        destination[key] = nlohmann::json::object();
    }
    for (auto it = source[key].begin(); it != source[key].end(); ++it) {
        destination[key][it.key()] = it.value();
    }
}

void mergeOpDefs(nlohmann::json& destination, const nlohmann::json& source) {
    if (source.contains("op_defs") && !source["op_defs"].is_object()) {
        // Keep malformed locale data visible to the validation pass instead of
        // silently falling back to the base definitions.
        destination["op_defs"] = source["op_defs"];
        return;
    }
    mergeObjectEntries(destination, source, "op_defs");
}

bool isEnsureTrailingStage(const nlohmann::json& stage) {
    if (!stage.is_object()) {
        return false;
    }
    if (stage.contains("id") && stage["id"].is_string() &&
        stage["id"].get<std::string>() == "fe_007") {
        return true;
    }
    for (const char* key : {"op", "use"}) {
        if (stage.contains(key) && stage[key].is_string() &&
            stage[key].get<std::string>() == "ensure_trailing_sentence_punct") {
            return true;
        }
    }
    return false;
}

nlohmann::json mergeFrontendDocs(const nlohmann::json& base, const nlohmann::json& extension) {
    nlohmann::json merged = base;
    if (extension.contains("resources") && !extension["resources"].is_object()) {
        // Preserve the invalid value so flattenRulesV2Pipeline can reject it.
        merged["resources"] = extension["resources"];
    } else if (extension.contains("resources")) {
        if (!merged.contains("resources") || !merged["resources"].is_object()) {
            merged["resources"] = nlohmann::json::object();
        }
        auto& baseResources = merged["resources"];
        const auto& extResources = extension["resources"];
        for (const char* bucket : {"char_maps", "char_sets", "constants", "pattern_defs"}) {
            if (extResources.contains(bucket) && !extResources[bucket].is_object()) {
                baseResources[bucket] = extResources[bucket];
            } else {
                mergeObjectEntries(baseResources, extResources, bucket);
            }
        }
    }
    mergeOpDefs(merged, extension);
    if (extension.contains("pre_stages")) {
        merged["pre_stages"] = extension["pre_stages"];
    }
    if (extension.contains("post_stages")) {
        merged["post_stages"] = extension["post_stages"];
    } else if (extension.contains("post_stages_extra")) {
        nlohmann::json post = merged.value("post_stages", nlohmann::json::array());
        size_t insertIdx = post.size();
        for (size_t i = 0; i < post.size(); ++i) {
            if (isEnsureTrailingStage(post[i])) {
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
    // Resources and named recipes are always base+locale overlays.  Locale
    // pre_stages, when present, replace only the base pre_stages; post_stages
    // and post_stages_extra retain their existing override/extension behavior.
    return mergeFrontendDocs(*enFrontend, *localeFrontend);
}

bool isInlineFeRule(const nlohmann::json& rule) {
    if (!rule.contains("id") || !rule["id"].is_string()) {
        return false;
    }
    const std::string id = rule["id"].get<std::string>();
    return id.rfind("zh_fe_", 0) == 0 || id.rfind("ru_fe_", 0) == 0;
}

bool readG2pSandhiOnlyProfile(
    const nlohmann::json& localeDoc,
    bool& sandhiOnly,
    std::string& errOut) {
    sandhiOnly = false;
    if (!localeDoc.contains("metadata")) {
        return true;
    }
    if (!localeDoc["metadata"].is_object()) {
        errOut = "rules_v2 field 'metadata' must be an object";
        return false;
    }
    const auto& md = localeDoc["metadata"];
    if (md.contains("pipeline_kind") && !md["pipeline_kind"].is_string()) {
        errOut = "rules_v2 metadata.pipeline_kind must be a string";
        return false;
    }
    if (md.contains("g2p_sandhi_only") && !md["g2p_sandhi_only"].is_boolean()) {
        errOut = "rules_v2 metadata.g2p_sandhi_only must be a boolean";
        return false;
    }
    sandhiOnly = md.value("pipeline_kind", "") == "g2p_sandhi" ||
                  md.value("g2p_sandhi_only", false);
    return true;
}

const nlohmann::json* compositeStages(
    const nlohmann::json& opDefs,
    const std::string& name,
    std::string& errOut) {
    if (!opDefs.contains(name)) {
        errOut = "unknown frontend op definition: " + name;
        return nullptr;
    }
    const auto& definition = opDefs[name];
    if (definition.is_array()) {
        if (definition.empty()) {
            errOut = "frontend.op_defs." + name + " must contain at least one stage";
            return nullptr;
        }
        return &definition;
    }
    if (definition.is_object() && definition.contains("stages") &&
        definition["stages"].is_array()) {
        if (definition["stages"].empty()) {
            errOut = "frontend.op_defs." + name + " must contain at least one stage";
            return nullptr;
        }
        return &definition["stages"];
    }
    errOut = "frontend.op_defs." + name + " must be an array or an object with array field 'stages'";
    return nullptr;
}

std::string compositePath(const std::vector<std::string>& stack, const std::string& next) {
    std::string out;
    for (const auto& name : stack) {
        if (!out.empty()) {
            out += " -> ";
        }
        out += name;
    }
    if (!out.empty()) {
        out += " -> ";
    }
    out += next;
    return out;
}

bool stackContains(const std::vector<std::string>& stack, const std::string& name) {
    for (const auto& item : stack) {
        if (item == name) {
            return true;
        }
    }
    return false;
}

bool parseStageTarget(
    const nlohmann::json& stage,
    std::string& target,
    bool& explicitReference,
    std::string& errOut) {
    if (!stage.is_object()) {
        errOut = "frontend stage must be an object";
        return false;
    }
    const bool hasOp = stage.contains("op");
    const bool hasUse = stage.contains("use");
    if (hasOp == hasUse) {
        errOut = hasOp ? "frontend stage must not contain both 'op' and 'use'"
                       : "frontend stage missing 'op' or 'use'";
        return false;
    }
    const char* key = hasUse ? "use" : "op";
    if (!stage[key].is_string() || stage[key].get<std::string>().empty()) {
        errOut = std::string("frontend stage '") + key + "' must be a non-empty string";
        return false;
    }
    if (stage.contains("params") && !stage["params"].is_object()) {
        errOut = "frontend stage 'params' must be an object";
        return false;
    }
    if (stage.contains("id") && !stage["id"].is_string()) {
        errOut = "frontend stage 'id' must be a string";
        return false;
    }
    target = stage[key].get<std::string>();
    explicitReference = hasUse;
    return true;
}

nlohmann::json mergeParams(
    const nlohmann::json& inherited,
    const nlohmann::json& local) {
    nlohmann::json merged = inherited;
    for (auto it = local.begin(); it != local.end(); ++it) {
        merged[it.key()] = it.value();
    }
    return merged;
}

bool validateCompositeGraph(
    const std::string& name,
    const nlohmann::json& opDefs,
    std::vector<std::string>& stack,
    size_t depth,
    std::string& errOut) {
    if (depth >= kMaxCompositeDepth) {
        errOut = "frontend op definition nesting exceeds maximum depth 32: " +
                 compositePath(stack, name);
        return false;
    }
    if (stackContains(stack, name)) {
        errOut = "cyclic frontend op definition: " + compositePath(stack, name);
        return false;
    }
    const nlohmann::json* stages = compositeStages(opDefs, name, errOut);
    if (stages == nullptr) {
        return false;
    }
    stack.push_back(name);
    for (const auto& stage : *stages) {
        std::string target;
        bool explicitReference = false;
        if (!parseStageTarget(stage, target, explicitReference, errOut)) {
            errOut = "frontend.op_defs." + name + ": " + errOut;
            stack.pop_back();
            return false;
        }
        if (explicitReference && !opDefs.contains(target)) {
            errOut = "frontend.op_defs." + name + " uses unknown definition: " + target;
            stack.pop_back();
            return false;
        }
        if (opDefs.contains(target)) {
            if (!validateCompositeGraph(target, opDefs, stack, depth + 1, errOut)) {
                stack.pop_back();
                return false;
            }
        } else if (!isKnownFrontendOp(target)) {
            errOut = "frontend.op_defs." + name + " contains unknown frontend op: " + target;
            stack.pop_back();
            return false;
        }
    }
    stack.pop_back();
    return true;
}

bool validateAllCompositeDefs(const nlohmann::json& opDefs, std::string& errOut) {
    if (!opDefs.is_object()) {
        errOut = "frontend.op_defs must be an object";
        return false;
    }
    for (auto it = opDefs.begin(); it != opDefs.end(); ++it) {
        if (it.key().empty()) {
            errOut = "frontend.op_defs names must not be empty";
            return false;
        }
        std::vector<std::string> stack;
        if (!validateCompositeGraph(it.key(), opDefs, stack, 0, errOut)) {
            return false;
        }
    }
    return true;
}

bool expandFrontendStage(
    nlohmann::json& flatRules,
    const nlohmann::json& stage,
    const nlohmann::json& opDefs,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& inheritedParams,
    const std::string& inheritedId,
    std::vector<std::string>& stack,
    size_t depth,
    std::string& errOut) {
    std::string target;
    bool explicitReference = false;
    if (!parseStageTarget(stage, target, explicitReference, errOut)) {
        return false;
    }
    const nlohmann::json params =
        mergeParams(inheritedParams, stage.value("params", nlohmann::json::object()));
    const std::string stageId =
        stage.contains("id") ? stage["id"].get<std::string>() : inheritedId;

    const bool isComposite = opDefs.contains(target);
    if (explicitReference && !isComposite) {
        errOut = "unknown frontend op definition: " + target;
        return false;
    }
    if (isComposite) {
        if (depth >= kMaxCompositeDepth) {
            errOut = "frontend op definition nesting exceeds maximum depth 32: " +
                     compositePath(stack, target);
            return false;
        }
        if (stackContains(stack, target)) {
            errOut = "cyclic frontend op definition: " + compositePath(stack, target);
            return false;
        }
        const nlohmann::json* children = compositeStages(opDefs, target, errOut);
        if (children == nullptr) {
            return false;
        }
        stack.push_back(target);
        for (const auto& child : *children) {
            if (!expandFrontendStage(
                    flatRules,
                    child,
                    opDefs,
                    resourcesDoc,
                    params,
                    stageId,
                    stack,
                    depth + 1,
                    errOut)) {
                stack.pop_back();
                return false;
            }
        }
        stack.pop_back();
        return true;
    }

    if (!validateFrontendOp(target, resourcesDoc, params, errOut)) {
        if (errOut.empty()) {
            errOut = "invalid frontend op: " + target;
        }
        return false;
    }
    nlohmann::json rule = nlohmann::json::object();
    if (!stageId.empty()) {
        rule["id"] = stageId;
    }
    rule["action"] = "frontend_op";
    rule["op"] = target;
    rule["params"] = params;
    flatRules.push_back(std::move(rule));
    return true;
}

bool appendFrontendStages(
    nlohmann::json& flatRules,
    const nlohmann::json& stages,
    const nlohmann::json& opDefs,
    const nlohmann::json& resourcesDoc,
    std::string& errOut) {
    if (!stages.is_array()) {
        errOut = "frontend stages must be an array";
        return false;
    }
    for (const auto& stage : stages) {
        std::vector<std::string> stack;
        if (!expandFrontendStage(
                flatRules,
                stage,
                opDefs,
                resourcesDoc,
                nlohmann::json::object(),
                "",
                stack,
                0,
                errOut)) {
            return false;
        }
    }
    return true;
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
    bool sandhiOnly = false;
    if (!readG2pSandhiOnlyProfile(localeDoc, sandhiOnly, errOut)) {
        return false;
    }
    if (!sandhiOnly &&
        (!validateFrontendBlockShape(enDoc, "base", errOut) ||
         !validateFrontendBlockShape(localeDoc, "locale", errOut))) {
        return false;
    }
    // G2P sandhi runs on a pinyin token stream after TN; do not inherit TN frontend bookends.
    const nlohmann::json frontendDoc =
        sandhiOnly ? nlohmann::json::object()
                   : loadMergedFrontendDoc(enDoc, localeDoc, locale);

    const nlohmann::json opDefs =
        frontendDoc.value("op_defs", nlohmann::json::object());
    if (!validateAllCompositeDefs(opDefs, errOut)) {
        return false;
    }
    nlohmann::json frontendResources = nlohmann::json::object();
    if (frontendDoc.contains("resources")) {
        if (!frontendDoc["resources"].is_object()) {
            errOut = "frontend.resources must be an object";
            return false;
        }
        frontendResources = nlohmann::json{
            {"resources", frontendDoc["resources"]},
        };
    }

    nlohmann::json coreRules = nlohmann::json::array();
    for (const auto& rule : localeDoc["pipeline"].value("rules", nlohmann::json::array())) {
        if (isInlineFeRule(rule)) {
            continue;
        }
        coreRules.push_back(rule);
    }

    nlohmann::json flatRules = nlohmann::json::array();
    if (frontendDoc.contains("pre_stages")) {
        if (!appendFrontendStages(
                flatRules,
                frontendDoc["pre_stages"],
                opDefs,
                frontendResources,
                errOut)) {
            return false;
        }
    }
    for (const auto& rule : coreRules) {
        flatRules.push_back(rule);
    }
    if (frontendDoc.contains("post_stages")) {
        if (!appendFrontendStages(
                flatRules,
                frontendDoc["post_stages"],
                opDefs,
                frontendResources,
                errOut)) {
            return false;
        }
    }

    outRuntimeDoc["frontend_resources"] = std::move(frontendResources);
    outRuntimeDoc["rules"] = std::move(flatRules);
    return true;
}

} // namespace frontend
