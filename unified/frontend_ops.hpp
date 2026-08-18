#pragma once

#include <functional>
#include <string>

#include "third_party/nlohmann/json.hpp"

namespace frontend {

using PreparedFrontendOp = std::function<std::string(const std::string&)>;

// Return whether a name is registered as a native/atomic frontend op. This is
// used to validate every named composition, including definitions not selected
// by the current locale stages.
bool isKnownFrontendOp(const std::string& op);

// Validate one frontend bookend op and its resource references. Unknown ops,
// missing parameters, and missing resources are reported through errOut.
bool validateFrontendOp(
    const std::string& op,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params,
    std::string& errOut);

// Validate and prepare an op for repeated execution. Resource lookups and
// primitive regex compilation happen here rather than once per input line.
bool prepareFrontendOp(
    const std::string& op,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params,
    PreparedFrontendOp& out,
    std::string& errOut);

// Apply one bookend op from rules_v2 frontend JSON. Throws
// std::invalid_argument when the op or its configuration is invalid.
std::string applyFrontendOp(
    const std::string& op,
    const std::string& text,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params = nlohmann::json::object());

// Backward-compatible name retained for existing callers.
std::string applyPunctuationOp(
    const std::string& op,
    const std::string& text,
    const nlohmann::json& resourcesDoc,
    const nlohmann::json& params = nlohmann::json::object());

} // namespace frontend
