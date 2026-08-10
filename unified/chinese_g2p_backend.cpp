// Chinese G2P tone-sandhi backend ("chinese_g2p").
//
// Runs AFTER dictionary lookup: the input is an already looked-up pinyin sequence and
// the output is that same sequence with tone sandhi applied.
#include "backend.hpp"
#include "chinese_g2p_ops.hpp"

namespace {

class ChineseG2pBackend : public LanguageBackend {
public:
    bool configure(const nlohmann::json& /*cfg*/, const std::string& /*resDir*/, std::string& /*err*/) override {
        return true;
    }

    const TtsCallbacks& callbacks() const override { return cb_; }

    void registerOps(TtsNormalizerEngine& engine) override {
        engine.registerOp("tone3_sandhi", tts_zh_g2p_ops::opTone3Sandhi);
        engine.registerOp("py_tone_mark_to_digit", tts_zh_g2p_ops::opPyToneMarkToDigit);
    }

private:
    TtsCallbacks cb_;
};

BackendAutoRegister _reg_zh_g2p("chinese_g2p", [] { return std::unique_ptr<LanguageBackend>(new ChineseG2pBackend()); });

} // namespace
