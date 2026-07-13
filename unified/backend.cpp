#include "backend.hpp"

BackendRegistry& BackendRegistry::instance() {
    static BackendRegistry reg;
    return reg;
}

void BackendRegistry::add(const std::string& id, BackendFactory f) {
    factories_[id] = std::move(f);
}

std::unique_ptr<LanguageBackend> BackendRegistry::create(const std::string& id) const {
    auto it = factories_.find(id);
    if (it == factories_.end()) {
        return nullptr;
    }
    return it->second();
}

std::vector<std::string> BackendRegistry::ids() const {
    std::vector<std::string> out;
    out.reserve(factories_.size());
    for (const auto& kv : factories_) {
        out.push_back(kv.first);
    }
    return out;
}
