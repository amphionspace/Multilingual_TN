#pragma once

#include <string>

#include "third_party/nlohmann/json.hpp"

namespace frontend {

// Apply one bookend op from rules_v2 frontend JSON (mirrors punctuation.py).
std::string applyPunctuationOp(
    const std::string& op,
    const std::string& text,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params = nlohmann::json::object());

} // namespace frontend
