#pragma once
// Small stateless helpers shared by backend op handlers. These mirror the
// engine's internal (anonymous-namespace) utilities; duplicated here so backends
// need no access to engine internals. Kept in namespace `opu`; backend files do
// `using namespace opu;` so verbatim-moved op bodies compile unchanged.
#include <cctype>
#include <string>

#include <unicode/unistr.h>

#include "third_party/nlohmann/json.hpp"

namespace opu {

inline std::string jgetS(const nlohmann::json& j, const char* key, const std::string& fallback = "") {
    if (!j.contains(key)) {
        return fallback;
    }
    if (j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return fallback;
}

inline int parseIntUStr(const icu::UnicodeString& s) {
    std::string utf8;
    s.toUTF8String(utf8);
    try {
        return std::stoi(utf8);
    } catch (...) {
        return 0;
    }
}

inline void replaceAllInPlace(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

inline std::string trimAsciiSpaces(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

} // namespace opu
