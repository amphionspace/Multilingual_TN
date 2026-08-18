#pragma once

#include <string>

#include "third_party/nlohmann/json.hpp"

namespace frontend {

// Merge rules_v2 frontend bookends into a single pipeline.rules array (frontend_op actions).
// Named frontend.op_defs recipes are recursively expanded and validated while loading.
// Base resources/op_defs are overlaid by locale entries; locale pre_stages replace
// base pre_stages, while post_stages/post_stages_extra keep their existing semantics.
// Mirrors tn_frontend.py merge logic; drops inline zh_fe_* / ru_fe_* duplicates.
bool flattenRulesV2Pipeline(
    const nlohmann::json& localeDoc,
    const nlohmann::json& enDoc,
    nlohmann::json& outRuntimeDoc,
    std::string& errOut);

} // namespace frontend
