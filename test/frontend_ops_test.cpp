#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "third_party/nlohmann/json.hpp"
#include "tts_normalizer_engine.hpp"
#include "unified/frontend_ops.hpp"
#include "unified/frontend_rules_merge.hpp"

namespace {

using json = nlohmann::json;

int gChecks = 0;
int gFailures = 0;

void fail(const std::string& name, const std::string& detail) {
    ++gFailures;
    std::cerr << "FAIL " << name << ": " << detail << '\n';
}

void expectTrue(const std::string& name, bool value, const std::string& detail = "expected true") {
    ++gChecks;
    if (!value) {
        fail(name, detail);
    }
}

void expectFalse(const std::string& name, bool value, const std::string& detail = "expected false") {
    ++gChecks;
    if (value) {
        fail(name, detail);
    }
}

void expectEq(const std::string& name, const std::string& actual, const std::string& expected) {
    ++gChecks;
    if (actual != expected) {
        fail(name, "expected [" + expected + "], got [" + actual + "]");
    }
}

void expectSize(const std::string& name, const json& value, std::size_t expected) {
    ++gChecks;
    if (!value.is_array() || value.size() != expected) {
        fail(name, "expected array size " + std::to_string(expected) + ", got " + value.dump());
    }
}

json resourcesDoc() {
    return {
        {"resources",
         {
             {"char_maps",
              {
                  {"supplementary_map", {{"😀", "🙂"}}},
                  {"invalid_multi_map", {{"x", "two"}}},
              }},
             {"char_sets",
              {
                  {"bars", "|｜丨"},
                  {"emoji_faces", "😀🙂"},
                  {"ellipsis_aliases", json::array({"...", "⋯", "···", "・・・"})},
                  {"wave_aliases", "~～∼"},
              }},
             {"constants", {{"ellipsis", "…"}}},
         }},
    };
}

json frontendDoc(json frontend) {
    return {
        {"format", "tts_rules_v2"},
        {"locale", "en"},
        {"pipeline", {{"rules", json::array()}}},
        {"frontend", std::move(frontend)},
    };
}

bool flatten(const json& frontend, json& runtime, std::string& err) {
    const json doc = frontendDoc(frontend);
    return frontend::flattenRulesV2Pipeline(doc, doc, runtime, err);
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("tts_frontend_ops_test_" + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void writeJsonFile(const std::filesystem::path& path, const json& value) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot create JSON fixture: " + path.string());
    }
    out << value.dump(2) << '\n';
}

std::string applyFlatRules(const json& runtime, std::string text) {
    const json resources = runtime.value("frontend_resources", json::object());
    for (const auto& rule : runtime.at("rules")) {
        if (rule.value("action", "") != "frontend_op") {
            throw std::runtime_error("test runtime contained a non-frontend rule: " + rule.dump());
        }
        text = frontend::applyFrontendOp(
            rule.at("op").get<std::string>(),
            text,
            resources,
            rule.value("params", json::object()));
    }
    return text;
}

void testRegexReplace() {
    const json resources = resourcesDoc();
    expectEq(
        "regex_replace normal/icase",
        frontend::applyFrontendOp(
            "regex_replace", "cat CAT dog", resources,
            {{"pattern", "\\bcat\\b"}, {"replacement", "fox"}, {"flags", "icase"}}),
        "fox fox dog");
    expectEq(
        "regex_replace unicode literal",
        frontend::applyFrontendOp(
            "regex_replace", "你好吗你好", resources,
            {{"pattern", "你好"}, {"replacement", "您好"}}),
        "您好吗您好");
    expectEq(
        "regex_replace boundary/no match",
        frontend::applyFrontendOp(
            "regex_replace", "xx", resources,
            {{"pattern", "^x$"}, {"replacement", "y"}}),
        "xx");
}

void testLiteralReplace() {
    const json resources = resourcesDoc();
    expectEq(
        "literal_replace normal/all occurrences",
        frontend::applyFrontendOp(
            "literal_replace", "aba", resources, {{"from", "a"}, {"to", "x"}}),
        "xbx");
    expectEq(
        "literal_replace unicode",
        frontend::applyFrontendOp(
            "literal_replace", "你好你好", resources, {{"from", "你"}, {"to", "我"}}),
        "我好我好");
    expectEq(
        "literal_replace boundary/longer needle",
        frontend::applyFrontendOp(
            "literal_replace", "abc", resources, {{"from", "abcd"}, {"to", "x"}}),
        "abc");
}

void testReplaceCharSet() {
    const json resources = resourcesDoc();
    expectEq(
        "replace_char_set normal",
        frontend::applyFrontendOp(
            "replace_char_set", "a|b｜c丨d", resources,
            {{"char_set", "bars"}, {"replacement", ","}}),
        "a,b,c,d");
    expectEq(
        "replace_char_set unicode replacement",
        frontend::applyFrontendOp(
            "replace_char_set", "甲｜乙", resources,
            {{"char_set", "bars"}, {"replacement", "，"}}),
        "甲，乙");
    expectEq(
        "replace_char_set at boundaries",
        frontend::applyFrontendOp(
            "replace_char_set", "|a丨", resources,
            {{"char_set", "bars"}, {"replacement", ","}}),
        ",a,");
    expectEq(
        "replace_char_set supplementary code points",
        frontend::applyFrontendOp(
            "replace_char_set", "a😀b🙂c", resources,
            {{"char_set", "emoji_faces"}, {"replacement", "_"}}),
        "a_b_c");
}

void testCollapseLiteralRuns() {
    const json resources = resourcesDoc();
    expectEq(
        "collapse_literal_runs normal",
        frontend::applyFrontendOp(
            "collapse_literal_runs", "a,,,b,,c", resources, {{"literal", ","}}),
        "a,b,c");
    expectEq(
        "collapse_literal_runs unicode",
        frontend::applyFrontendOp(
            "collapse_literal_runs", "啊………好……", resources, {{"literal", "…"}}),
        "啊…好…");
    expectEq(
        "collapse_literal_runs at boundaries",
        frontend::applyFrontendOp(
            "collapse_literal_runs", "..a...", resources, {{"literal", "."}}),
        ".a.");
}

void testTrimSpaces() {
    const json resources = resourcesDoc();
    expectEq(
        "trim_spaces normal",
        frontend::applyFrontendOp("trim_spaces", "  a b  ", resources),
        "a b");
    expectEq(
        "trim_spaces unicode",
        frontend::applyFrontendOp("trim_spaces", "  中文  ", resources),
        "中文");
    expectEq(
        "trim_spaces all spaces",
        frontend::applyFrontendOp("trim_spaces", "     ", resources),
        "");
    expectEq(
        "trim_spaces empty boundary",
        frontend::applyFrontendOp("trim_spaces", "", resources),
        "");
}

void testReplaceMany() {
    const json resources = resourcesDoc();
    expectEq(
        "replace_many normal/constant",
        frontend::applyFrontendOp(
            "replace_many", "a...b⋯c", resources,
            {{"values", "ellipsis_aliases"}, {"replacement_constant", "ellipsis"}}),
        "a…b…c");
    expectEq(
        "replace_many unicode char set",
        frontend::applyFrontendOp(
            "replace_many", "a～b∼c~d", resources,
            {{"values", "wave_aliases"}, {"replacement", "!"}}),
        "a!b!c!d");
    expectEq(
        "replace_many aliases at boundaries",
        frontend::applyFrontendOp(
            "replace_many", "···a...", resources,
            {{"values", "ellipsis_aliases"}, {"replacement_constant", "ellipsis"}}),
        "…a…");

    std::string err;
    const json legacyParams = {
        {"aliases", "ellipsis_aliases"},
        {"target", "ellipsis"},
    };
    const bool legacyCompatible =
        frontend::validateFrontendOp("replace_many", resources, legacyParams, err);
    expectTrue("replace_many accepts legacy aliases/target", legacyCompatible, err);
    if (legacyCompatible) {
        expectEq(
            "replace_many legacy aliases/target result",
            frontend::applyFrontendOp(
                "replace_many", "a...b⋯c", resources, legacyParams),
            "a…b…c");

        frontend::PreparedFrontendOp prepared;
        err.clear();
        const bool preparedLegacy = frontend::prepareFrontendOp(
            "replace_many", resources, legacyParams, prepared, err);
        expectTrue("prepare replace_many legacy aliases/target", preparedLegacy, err);
        if (preparedLegacy) {
            expectEq(
                "prepared replace_many legacy aliases/target result",
                prepared("···a..."),
                "…a…");
        }
    }

    const json matchingAliases = {
        {"values", "ellipsis_aliases"},
        {"aliases", "ellipsis_aliases"},
        {"replacement_constant", "ellipsis"},
        {"target", "ellipsis"},
    };
    err.clear();
    expectTrue(
        "replace_many accepts matching canonical and legacy aliases",
        frontend::validateFrontendOp("replace_many", resources, matchingAliases, err),
        err);
    expectEq(
        "replace_many matching aliases result",
        frontend::applyFrontendOp("replace_many", "a...b", resources, matchingAliases),
        "a…b");

    json conflictingAliases = matchingAliases;
    conflictingAliases["aliases"] = "wave_aliases";
    err.clear();
    expectFalse(
        "replace_many rejects conflicting values aliases",
        frontend::validateFrontendOp("replace_many", resources, conflictingAliases, err));
    json conflictingTarget = matchingAliases;
    conflictingTarget["target"] = "missing_constant";
    err.clear();
    expectFalse(
        "replace_many rejects conflicting constant aliases",
        frontend::validateFrontendOp("replace_many", resources, conflictingTarget, err));
}

void testPrimitiveValidation() {
    const json resources = resourcesDoc();
    std::string err;
    expectTrue(
        "validate known primitive",
        frontend::validateFrontendOp(
            "regex_replace", resources,
            {{"pattern", "x"}, {"replacement", "y"}}, err),
        err);
    err.clear();
    expectFalse(
        "validate unknown primitive",
        frontend::validateFrontendOp("not_a_real_op", resources, json::object(), err));
    expectTrue("unknown primitive gives error", !err.empty());
    err.clear();
    expectFalse(
        "validate empty literal",
        frontend::validateFrontendOp(
            "literal_replace", resources, {{"from", ""}, {"to", "x"}}, err));
    expectTrue("empty literal gives error", !err.empty());
    err.clear();
    expectTrue(
        "validate supplementary single-codepoint char map",
        frontend::validateFrontendOp(
            "translate_chars", resources, {{"char_map", "supplementary_map"}}, err),
        err);
    expectEq(
        "translate supplementary codepoint char map",
        frontend::applyFrontendOp(
            "translate_chars", "a😀b", resources, {{"char_map", "supplementary_map"}}),
        "a🙂b");
    err.clear();
    expectFalse(
        "reject multi-codepoint char map value",
        frontend::validateFrontendOp(
            "translate_chars", resources, {{"char_map", "invalid_multi_map"}}, err));
    expectTrue("invalid char map gives error", !err.empty());
}

void testPreparedOp() {
    const json resources = resourcesDoc();
    frontend::PreparedFrontendOp prepared;
    std::string err;
    const json params = {
        {"pattern", "\\bcat\\b"},
        {"replacement", "fox"},
        {"flags", "icase"},
    };
    expectTrue(
        "prepare valid primitive",
        frontend::prepareFrontendOp("regex_replace", resources, params, prepared, err),
        err);
    expectTrue("prepared primitive is callable", static_cast<bool>(prepared));
    if (prepared) {
        expectEq(
            "prepared primitive matches direct apply",
            prepared("cat CAT"),
            frontend::applyFrontendOp("regex_replace", "cat CAT", resources, params));
    }

    err.clear();
    expectFalse(
        "prepare invalid primitive fails",
        frontend::prepareFrontendOp(
            "literal_replace", resources, {{"from", ""}, {"to", "x"}}, prepared, err));
    expectFalse(
        "failed prepare clears callable",
        static_cast<bool>(prepared),
        "prepared callback remained set after validation failure");
    expectTrue("failed prepare gives error", !err.empty());
}

void testNestedRecipeAndExplicitUse() {
    json runtime;
    std::string err;
    const json frontend = {
        {"resources", json::object()},
        {"op_defs",
         {
             {"inner",
              {{"stages",
                json::array({
                    {{"op", "literal_replace"}, {"params", {{"to", "内"}}}},
                })}}},
             {"outer",
              json::array({
                  {{"use", "inner"}, {"params", {{"from", "X"}}}},
                  {{"op", "trim_spaces"}},
              })},
         }},
        {"pre_stages",
         json::array({
             {{"use", "outer"}, {"params", {{"from", "wrong"}, {"to", "wrong"}}}},
         })},
        {"post_stages", json::array()},
    };
    expectTrue("nested recipe flattens", flatten(frontend, runtime, err), err);
    if (!runtime.contains("rules")) {
        fail("nested recipe runtime", "missing rules: " + runtime.dump());
        return;
    }
    expectSize("nested recipe leaf count", runtime["rules"], 2);
    expectEq("nested recipe explicit use result", applyFlatRules(runtime, "  X  "), "内");
    if (runtime["rules"].size() == 2) {
        expectEq("nested recipe first leaf", runtime["rules"][0].value("op", ""), "literal_replace");
        expectEq("nested recipe second leaf", runtime["rules"][1].value("op", ""), "trim_spaces");
    }
}

void testLocaleOverlayAndPostAnchor() {
    const json base = frontendDoc({
        {"resources",
         {{"char_sets", {{"acceptable", ".!?"}}}}},
        {"op_defs",
         {
             {"shared",
              json::array({{{"op", "literal_replace"},
                            {"params", {{"from", "X"}, {"to", "base"}}}}})},
             {"ensure_recipe",
              json::array({{{"op", "ensure_trailing_sentence_punct"},
                            {"params", {{"acceptable_trailing", "acceptable"}}}}})},
         }},
        {"pre_stages", json::array({{{"use", "shared"}}})},
        {"post_stages", json::array({{{"id", "fe_007"}, {"use", "ensure_recipe"}}})},
    });
    json locale = frontendDoc({
        {"op_defs",
         {{"shared",
           json::array({{{"op", "literal_replace"},
                         {"params", {{"from", "X"}, {"to", "locale"}}}}})}}},
        {"post_stages_extra", json::array({{{"op", "trim_spaces"}}})},
    });
    locale["locale"] = "zh";

    json runtime;
    std::string err;
    expectTrue(
        "locale overlays recipe without replacing base stages",
        frontend::flattenRulesV2Pipeline(locale, base, runtime, err),
        err);
    expectSize("locale overlay flattened rule count", runtime["rules"], 3);
    if (runtime["rules"].size() == 3) {
        expectEq("locale recipe override applied", runtime["rules"][0].value("op", ""), "literal_replace");
        expectEq("post extra stays before explicit-use ensure", runtime["rules"][1].value("op", ""), "trim_spaces");
        expectEq(
            "explicit-use ensure remains final",
            runtime["rules"][2].value("op", ""),
            "ensure_trailing_sentence_punct");
        expectEq("locale recipe output", applyFlatRules(runtime, " X "), "locale .");
    }
}

void testSandhiProfileSkipsFrontendBookends() {
    json base = frontendDoc(json::object());
    base["frontend"] = json::array({"intentionally malformed but unused"});
    json sandhi = frontendDoc(json::object());
    sandhi["locale"] = "zh";
    sandhi["metadata"] = {{"pipeline_kind", "g2p_sandhi"}};

    json runtime;
    std::string err;
    expectTrue(
        "sandhi profile does not load TN frontend bookends",
        frontend::flattenRulesV2Pipeline(sandhi, base, runtime, err),
        err);
    expectSize("sandhi profile has no frontend rules", runtime["rules"], 0);

    json invalidMetadata = sandhi;
    invalidMetadata["metadata"]["g2p_sandhi_only"] = "true";
    err.clear();
    expectFalse(
        "merge rejects non-boolean g2p_sandhi_only",
        frontend::flattenRulesV2Pipeline(invalidMetadata, base, runtime, err));
    expectTrue(
        "invalid sandhi metadata gives actionable merge error",
        err.find("boolean") != std::string::npos,
        err);
}

void testMissingEnglishBaseRules() {
    TemporaryDirectory fixture;
    const json regular = {
        {"format", "tts_rules_v2"},
        {"locale", "zh"},
        {"pipeline", {{"rules", json::array()}}},
    };
    const std::filesystem::path regularPath = fixture.path() / "zh.full.json";
    writeJsonFile(regularPath, regular);

    json invalidMetadata = regular;
    invalidMetadata["metadata"] = {{"pipeline_kind", 7}};
    const std::filesystem::path invalidPath = fixture.path() / "invalid.full.json";
    writeJsonFile(invalidPath, invalidMetadata);
    TtsNormalizerEngine invalidMetadataEngine;
    std::string err;
    expectFalse(
        "engine rejects non-string pipeline_kind",
        invalidMetadataEngine.loadRulesV2(invalidPath.string(), err));
    expectTrue(
        "invalid pipeline_kind gives actionable engine error",
        err.find("string") != std::string::npos,
        err);

    TtsNormalizerEngine regularEngine;
    err.clear();
    expectFalse(
        "non-sandhi rules require shared en.full.json",
        regularEngine.loadRulesV2(regularPath.string(), err));
    expectTrue(
        "missing en.full.json gives actionable error",
        err.find("en.full.json") != std::string::npos,
        "error did not mention en.full.json: " + err);

    const std::filesystem::path enPath = fixture.path() / "en.full.json";
    writeJsonFile(enPath, {{"format", "tts_rules_v2"}});
    TtsNormalizerEngine missingFrontendEngine;
    err.clear();
    expectFalse(
        "shared en rules require frontend object",
        missingFrontendEngine.loadRulesV2(regularPath.string(), err));
    expectTrue(
        "missing shared frontend gives actionable error",
        err.find("frontend") != std::string::npos,
        "error did not mention frontend: " + err);

    writeJsonFile(enPath, {{"format", "tts_rules_v2"}, {"frontend", json::object()}});
    TtsNormalizerEngine emptyFrontendEngine;
    err.clear();
    expectFalse(
        "shared en frontend requires active stages",
        emptyFrontendEngine.loadRulesV2(regularPath.string(), err));
    expectTrue(
        "empty shared frontend gives actionable error",
        err.find("stages") != std::string::npos,
        "error did not mention stages: " + err);

    json sandhi = regular;
    sandhi["metadata"] = {{"pipeline_kind", "g2p_sandhi"}};
    const std::filesystem::path sandhiPath = fixture.path() / "zh_g2p_sandhi.json";
    writeJsonFile(sandhiPath, sandhi);

    TtsNormalizerEngine sandhiEngine;
    err.clear();
    expectTrue(
        "sandhi-only rules may load without en.full.json",
        sandhiEngine.loadRulesV2(sandhiPath.string(), err),
        err);
}

void testLegacyNameRecipeOverrideAndEquivalence() {
    const json frontend = {
        {"resources", {{"char_sets", {{"bars", "|｜丨"}}}}},
        {"op_defs",
         {
             {"replace_vertical_line_punct",
              json::array({
                  {{"op", "replace_char_set"},
                   {"params", {{"char_set", "bars"}, {"replacement", "，"}}}},
                  {{"op", "collapse_literal_runs"}, {"params", {{"literal", "，"}}}},
              })},
             {"collapse_empty_slash_segments",
              json::array({
                  {{"op", "regex_replace"},
                   {"params",
                    {{"pattern", "(?:\\s*/\\s*){2,}"}, {"replacement", " / "}}}},
                  {{"op", "regex_replace"},
                   {"params", {{"pattern", "\\s*/\\s*$"}, {"replacement", ""}}}},
              })},
         }},
        {"pre_stages",
         json::array({
             {{"op", "replace_vertical_line_punct"}},
             {{"op", "collapse_empty_slash_segments"}},
         })},
        {"post_stages", json::array()},
    };

    json runtime;
    std::string err;
    expectTrue("legacy-name recipes flatten", flatten(frontend, runtime, err), err);
    if (!runtime.contains("rules")) {
        fail("legacy-name recipe runtime", "missing rules: " + runtime.dump());
        return;
    }
    expectSize("legacy names expand instead of dispatch", runtime["rules"], 4);
    for (const auto& rule : runtime["rules"]) {
        const std::string op = rule.value("op", "");
        expectFalse(
            "legacy op absent after recipe expansion",
            op == "replace_vertical_line_punct" || op == "collapse_empty_slash_segments",
            "unexpanded op: " + op);
    }

    const json oldResources = {
        {"resources", {{"char_sets", {{"bars", "|｜丨"}}}}},
    };
    const std::vector<std::string> cases = {
        "alpha||beta / / gamma / ",
        "|中文丨丨测试//尾部/",
        "plain text",
        "a / b",
    };
    for (std::size_t i = 0; i < cases.size(); ++i) {
        std::string legacy = frontend::applyPunctuationOp(
            "replace_vertical_line_punct", cases[i], oldResources,
            {{"char_set", "bars"}, {"replacement", "，"}});
        legacy = frontend::applyPunctuationOp(
            "collapse_empty_slash_segments", legacy, oldResources, json::object());
        expectEq(
            "legacy recipe byte equivalence " + std::to_string(i),
            applyFlatRules(runtime, cases[i]), legacy);
    }

    // The production rule intentionally keeps this legacy op for now: its
    // non-overlapping pair pass turns three adjacent separators into two,
    // whereas the generic collapse primitive correctly turns the run into one.
    // Keeping the difference explicit prevents a structural refactor from
    // silently changing existing normalization output.
    expectEq(
        "legacy vertical triple remains byte-compatible",
        frontend::applyPunctuationOp(
            "replace_vertical_line_punct", "||｜", oldResources,
            {{"char_set", "bars"}, {"replacement", "，"}}),
        "，，");
    expectEq(
        "generic collapse normalizes full run",
        frontend::applyFrontendOp(
            "collapse_literal_runs", "，，，", oldResources,
            {{"literal", "，"}}),
        "，");
}

void expectFlattenFailure(const std::string& name, const json& frontend) {
    json runtime;
    std::string err;
    const bool ok = flatten(frontend, runtime, err);
    expectFalse(name, ok, "expected initialization failure, runtime=" + runtime.dump());
    expectTrue(name + " error message", !err.empty());
}

json readJsonFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open JSON fixture: " + path);
    }
    json value;
    in >> value;
    return value;
}

json findConfiguredStage(const json& frontend, const std::string& op) {
    for (const char* key : {"pre_stages", "post_stages", "post_stages_extra"}) {
        for (const auto& stage : frontend.value(key, json::array())) {
            if (stage.value("op", "") == op) {
                return stage;
            }
        }
    }
    throw std::runtime_error("configured frontend stage not found: " + op);
}

void expectConfiguredRecipeEquivalent(
    const std::string& op,
    const json& configuredFrontend,
    const std::vector<std::string>& inputs) {
    if (!configuredFrontend.contains("op_defs") ||
        !configuredFrontend["op_defs"].contains(op)) {
        fail("configured recipe " + op, "missing op_defs entry");
        return;
    }
    const json configuredStage = findConfiguredStage(configuredFrontend, op);
    const json params = configuredStage.value("params", json::object());
    const json frontend = {
        {"resources", configuredFrontend.value("resources", json::object())},
        {"op_defs", {{op, configuredFrontend["op_defs"][op]}}},
        {"pre_stages", json::array({configuredStage})},
        {"post_stages", json::array()},
    };
    json runtime;
    std::string err;
    expectTrue("configured recipe flattens " + op, flatten(frontend, runtime, err), err);
    if (!runtime.contains("rules")) {
        fail("configured recipe runtime " + op, "missing rules");
        return;
    }
    const json legacyResources = {
        {"resources", configuredFrontend.value("resources", json::object())},
    };
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        expectEq(
            "configured recipe byte equivalence " + op + " " + std::to_string(i),
            applyFlatRules(runtime, inputs[i]),
            frontend::applyPunctuationOp(op, inputs[i], legacyResources, params));
    }
}

void testCheckedInRecipeEquivalence(const std::string& root) {
    const json enFrontend = readJsonFile(root + "/rules_v2/en.full.json").at("frontend");
    const json zhFrontend = readJsonFile(root + "/rules_v2/zh.full.json").at("frontend");

    expectConfiguredRecipeEquivalent(
        "expand_literal_line_break_escapes", enFrontend,
        {"alpha\\nbeta", "alpha\\r\\nbeta", "plain"});
    expectConfiguredRecipeEquivalent(
        "strip_literal_line_break_escapes", enFrontend,
        {"alpha\\nbeta", "alpha\\rbeta", "plain"});
    expectConfiguredRecipeEquivalent(
        "strip_markdown_asterisks", enFrontend,
        {"**bold** and *italic*", "(*)", "plain * bullet"});
    expectConfiguredRecipeEquivalent(
        "strip_markdown_code_ticks", enFrontend,
        {"```code```", "say `code` now", "plain"});
    expectConfiguredRecipeEquivalent(
        "normalize_programming_tokens_en", enFrontend,
        {"C++ and C#", "C++C#", "c++ and c#"});
    expectConfiguredRecipeEquivalent(
        "split_sql_glued_identifiers", enFrontend,
        {"GROUPBY orderby TRUNCDATE", "xGROUPBYy", "TRUNCMONTH TRUNCYEAR"});
    expectConfiguredRecipeEquivalent(
        "collapse_empty_slash_segments", enFrontend,
        {"a / / b / ", "a//b///", "a / b"});
    expectConfiguredRecipeEquivalent(
        "normalize_numeric_dash_ranges", enFrontend,
        {"3-5", "12 — 14", "123-456", "中文3–5"});
    expectConfiguredRecipeEquivalent(
        "remove_dashes", enFrontend,
        {"a-b—c–d", "-edge-", "plain"});
    expectConfiguredRecipeEquivalent(
        "collapse_whitespace", enFrontend,
        {"  a\t b\n c  ", "     ", "中文  text"});
    expectConfiguredRecipeEquivalent(
        "normalize_ellipsis", zhFrontend,
        {"a...b", "⋯···・・・", "plain"});
}

void testInitializationFailures() {
    expectFlattenFailure(
        "unknown op initialization failure",
        {
            {"resources", json::object()},
            {"pre_stages", json::array({{{"op", "does_not_exist"}}})},
            {"post_stages", json::array()},
        });

    expectFlattenFailure(
        "unused recipe unknown op initialization failure",
        {
            {"resources", json::object()},
            {"op_defs",
             {{"unused_bad_recipe", json::array({{{"op", "does_not_exist"}}})}}},
            {"pre_stages", json::array()},
            {"post_stages", json::array()},
        });

    expectFlattenFailure(
        "empty recipe initialization failure",
        {
            {"resources", json::object()},
            {"op_defs", {{"empty", json::array()}}},
            {"pre_stages", json::array({{{"use", "empty"}}})},
            {"post_stages", json::array()},
        });

    expectFlattenFailure(
        "malformed op_defs initialization failure",
        {
            {"resources", json::object()},
            {"op_defs", json::array()},
            {"pre_stages", json::array()},
            {"post_stages", json::array()},
        });

    expectFlattenFailure(
        "conflicting op and use initialization failure",
        {
            {"resources", json::object()},
            {"op_defs", {{"named", json::array({{{"op", "trim_spaces"}}})}}},
            {"pre_stages", json::array({{{"op", "trim_spaces"}, {"use", "named"}}})},
            {"post_stages", json::array()},
        });

    expectFlattenFailure(
        "non-object params initialization failure",
        {
            {"resources", json::object()},
            {"pre_stages",
             json::array({{{"op", "trim_spaces"}, {"params", json::array()}}})},
            {"post_stages", json::array()},
        });

    expectFlattenFailure(
        "missing resource initialization failure",
        {
            {"resources", {{"char_sets", json::object()}}},
            {"pre_stages",
             json::array({
                 {{"op", "replace_char_set"},
                  {"params", {{"char_set", "missing"}, {"replacement", ","}}}},
             })},
            {"post_stages", json::array()},
        });

    expectFlattenFailure(
        "recipe cycle initialization failure",
        {
            {"resources", json::object()},
            {"op_defs",
             {
                 {"a", json::array({{{"use", "b"}}})},
                 {"b", json::array({{{"use", "a"}}})},
             }},
            {"pre_stages", json::array({{{"use", "a"}}})},
            {"post_stages", json::array()},
        });

    json opDefs = json::object();
    constexpr int kDepth = 512;
    for (int i = 0; i < kDepth; ++i) {
        opDefs["d" + std::to_string(i)] =
            json::array({{{"use", "d" + std::to_string(i + 1)}}});
    }
    opDefs["d" + std::to_string(kDepth)] = json::array({{{"op", "trim_spaces"}}});
    expectFlattenFailure(
        "recipe depth initialization failure",
        {
            {"resources", json::object()},
            {"op_defs", std::move(opDefs)},
            {"pre_stages", json::array({{{"use", "d0"}}})},
            {"post_stages", json::array()},
        });
}

} // namespace

int main(int argc, char** argv) {
    try {
        testRegexReplace();
        testLiteralReplace();
        testReplaceCharSet();
        testCollapseLiteralRuns();
        testTrimSpaces();
        testReplaceMany();
        testPrimitiveValidation();
        testPreparedOp();
        testNestedRecipeAndExplicitUse();
        testLocaleOverlayAndPostAnchor();
        testSandhiProfileSkipsFrontendBookends();
        testMissingEnglishBaseRules();
        testLegacyNameRecipeOverrideAndEquivalence();
        testInitializationFailures();
        if (argc != 2) {
            fail("checked-in recipe fixtures", "expected repository root argument");
        } else {
            testCheckedInRecipeEquivalence(argv[1]);
        }
    } catch (const std::exception& e) {
        fail("uncaught exception", e.what());
    }

    if (gFailures != 0) {
        std::cerr << gFailures << " failure(s), " << gChecks << " check(s)\n";
        return 1;
    }
    std::cout << "PASS frontend ops: " << gChecks << " checks\n";
    return 0;
}
