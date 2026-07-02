// Plain ICU backend ("icu"): the zero-code default for any new language whose number
// spellout ICU covers. Behavior comes entirely from config (locale + ordinal mode).
#include "icu_backend.hpp"

namespace {
BackendAutoRegister _reg_icu("icu", [] { return std::unique_ptr<LanguageBackend>(new IcuSpelloutBackend()); });
} // namespace
