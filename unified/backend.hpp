#pragma once
// Phase 0 scaffolding: a per-language backend supplies the runtime callbacks (and,
// later, custom exec-op handlers) for one locale. Backends are created by name from
// a resource dir's config.json, so adding a language becomes "drop in data/xx".

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "third_party/nlohmann/json.hpp"
#include "tts_normalizer_engine.hpp"

class LanguageBackend {
public:
    virtual ~LanguageBackend() = default;

    // cfg  = parsed config.json for this language.
    // resDir = directory that held config.json (used to resolve relative resource paths).
    // Returns false and fills err on failure.
    virtual bool configure(const nlohmann::json& cfg, const std::string& resDir, std::string& err) = 0;

    // Callbacks handed to engine.runPipeline(). Must stay valid for the backend's lifetime.
    virtual const TtsCallbacks& callbacks() const = 0;

    // Optional per-line preparation (e.g. Russian morphological analysis) run before the
    // pipeline. Default: no-op.
    virtual void prepare(const std::string& inputUtf8) { (void)inputUtf8; }

    // Optional: register backend-owned exec-op handlers on the engine (Phase 2+).
    virtual void registerOps(TtsNormalizerEngine& engine) { (void)engine; }
};

using BackendFactory = std::function<std::unique_ptr<LanguageBackend>()>;

// Process-wide registry of backend factories keyed by id (the "backend" field in config.json).
class BackendRegistry {
public:
    static BackendRegistry& instance();
    void add(const std::string& id, BackendFactory f);
    std::unique_ptr<LanguageBackend> create(const std::string& id) const; // nullptr if unknown
    std::vector<std::string> ids() const;

private:
    std::unordered_map<std::string, BackendFactory> factories_;
};

// Helper for static self-registration: place one at file scope in each backend .cpp.
struct BackendAutoRegister {
    BackendAutoRegister(const std::string& id, BackendFactory f) {
        BackendRegistry::instance().add(id, std::move(f));
    }
};
