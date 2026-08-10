#pragma once

#include <string>

#include "third_party/nlohmann/json.hpp"

namespace frontend {

// Merge rules_v2 frontend bookends into a single pipeline.rules array (frontend_op actions).
// Mirrors tn_frontend.py merge logic; drops inline zh_fe_* / ru_fe_* duplicates.
bool flattenRulesV2Pipeline(
    const nlohmann::json& localeDoc,
    const nlohmann::json& enDoc,
    nlohmann::json& outRuntimeDoc,
    std::string& errOut);

} // namespace frontend
