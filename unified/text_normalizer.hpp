#pragma once
// Phase 0: the single, language-agnostic front door the customer asked for.
//   TextNormalizer tn;
//   tn.Init("data/ru");                 // picks backend + rules from config.json
//   std::string out = tn.normalizeLine(line);
// Adding a language that fits an existing backend needs only a new data/xx directory.

#include <memory>
#include <string>

#include "backend.hpp"
#include "tts_normalizer_engine.hpp"

class TextNormalizer {
public:
    // Reads <resourceDir>/config.json, instantiates the named backend, loads rules and
    // any pinyin map, and wires everything together. Returns false and sets error() on failure.
    bool Init(const std::string& resourceDir);

    std::string normalizeLine(const std::string& inputUtf8);

    const std::string& error() const { return err_; }
    const std::string& locale() const { return locale_; }

private:
    TtsNormalizerEngine engine_;
    std::unique_ptr<LanguageBackend> backend_;
    std::string locale_;
    std::string err_;
};
