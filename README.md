# ICU TTS Text Normalization / ICU TTS 文本归一化

## English

This project is a config-driven multilingual text normalization prototype for TTS frontends. Rules are stored as **JSON** under `rules_v2/`, so most product-side pattern, replacement, unit, and currency tweaks do not require editing C++.

**Named pattern fragments:** each locale file has a top-level **`resources`** map of named ICU fragments. **`rules[].pattern`** strings may embed **`{{NAME}}`** placeholders; `TtsNormalizerEngine::loadRulesV2` resolves them after stabilizing mutual references among defs.

### Layout

| Path | Role |
|------|------|
| `unified/` + `tts_cli` | **Unified single-binary front end** — one `TextNormalizer::Init("data/xx")` for every language via backend registry (see `unified/README.md`). This is the only entry point; the per-language drivers were removed in Phase 3. |
| `tts_normalizer_engine.hpp` / `tts_normalizer_engine.cpp` | Language-agnostic ICU regex runner; pipeline actions are mostly `action: "exec"` with JSON `steps`. Language-specific ops/actions live in `unified/*_backend.cpp`, registered via `registerOp`/`registerAction`. |
| `rules_v2/en.full.json` | English pipeline and literal rules |
| `rules_v2/zh.full.json` | Chinese pipeline and literal rules |
| `rules_v2/ar.full.json` | Arabic pipeline and literal rules |
| `rules_v2/bn.full.json` | Bengali pipeline and literal rules |
| `rules_v2/ru.full.json` | Russian pipeline and literal rules, including morph-aware numerals |
| `rules_v2/zh_pinyin.json` | Tone-marked pinyin to Chinese character mapping |
| `docs/tts_ops.md` | Handoff document for every `exec` step `op` and dedicated action |
| `third_party/nlohmann/json.hpp` | Single-header JSON parser, MIT license |

### Editing Rules

**All rule changes are made by hand-editing `rules_v2/<locale>.full.json` directly — there is no generator step.** A locale file has a `resources` map (named ICU pattern fragments) and a `pipeline.rules` list of ordered rules. Rule order matters.

1. **Simple find/replace:** add a `{ "id": ..., "pattern": ..., "replace": ... }` rule.
2. **Structured rewrites (numbers, dates, units, currency):** use `{ "action": "exec", "params": { "steps": [...] } }`; see `docs/tts_ops.md` for every available `op`.
3. **New primitive:** add C++ only when a new op is needed inside `applyExecStep`.

Give each hand-added rule a descriptive `id` (e.g. `zh_list_dash_silent_1`).

### rules_v2 is the delivery format (ALL locales)

**Every locale loads `rules_v2/<locale>.full.json`.** This is the only supported format — the legacy v1 generator and the `rules/*.json` tree have been removed.

> [!IMPORTANT]
> `rules_v2/*.full.json` is the hand-maintained source of truth for delivery. Make all rule changes by editing these files directly.

### Build

The unified binary (all languages) is built by `test/scripts/build_unified.sh`; run it with `--data data/<locale>`:

```bash
ICU_ROOT=/opt/homebrew/opt/icu4c bash test/scripts/build_unified.sh
./test/bin/tts_cli --data data/en < input.txt
./test/bin/tts_cli --data data/ru < input.txt   # ru also needs the MorphoDiTa model (see data/ru/config.json)
```

See `unified/README.md` for the backend architecture and config format.

The Russian MorphoDiTa model path is read from `data/ru/config.json` (`morph_model`), so no CLI flag is needed:

```bash
./test/bin/tts_cli --data data/ru < input.txt
```

Model format (not PyTorch `.pt` or FST): see `docs/linguist_delivery/morphodita_tagger_model_reference.md`.

Rule paths resolve relative to each config directory, and unified backends resolve resources from `data/<locale>/config.json`.

### Debug Logging

Set `TTS_NORMALIZER_DEBUG` to any non-empty value to print one line to stderr for each regex hit, including `locale`, `rule_index`, `rule_id`, `action`, and the matched UTF-8 snippet.

```bash
export TTS_NORMALIZER_DEBUG=1
./test/bin/tts_cli --data data/zh
```

## 中文

本项目是一个面向 TTS 前端的多语言文本归一化原型。大多数 pattern、replacement、单位和货币表都放在 `rules_v2/` 下的 **JSON** 中，因此产品侧调整规则时通常不需要改 C++。

**命名正则片段：** 每个语言规则文件都有顶层的 **`resources`**，用于定义命名 ICU 正则片段。**`rules[].pattern`** 可以通过 **`{{NAME}}`** 引用这些片段；`TtsNormalizerEngine::loadRulesV2` 会先稳定解析这些定义之间的互相引用，再展开到最终规则中。

### 目录结构

| 路径 | 作用 |
|------|------|
| `unified/` + `tts_cli` | **统一单二进制入口** —— 一个 `TextNormalizer::Init("data/xx")` 经后端注册表驱动所有语种（见 `unified/README.md`）。唯一入口；各语种独立 driver 已在 Phase 3 移除 |
| `tts_normalizer_engine.hpp` / `tts_normalizer_engine.cpp` | 与语种无关的 ICU regex 执行引擎；语种专属 op/action 已移到 `unified/*_backend.cpp`，经 `registerOp`/`registerAction` 注册 |
| `rules_v2/en.full.json` | 英文 pipeline 和 literal 规则 |
| `rules_v2/zh.full.json` | 中文 pipeline 和 literal 规则 |
| `rules_v2/ar.full.json` | 阿拉伯语 pipeline 和 literal 规则 |
| `rules_v2/bn.full.json` | 孟加拉语 pipeline 和 literal 规则 |
| `rules_v2/ru.full.json` | 俄语 pipeline 和 literal 规则，包含形态感知的数字处理 |
| `rules_v2/zh_pinyin.json` | 带声调拼音到汉字的映射，供拼音规则使用 |
| `docs/tts_ops.md` | 交接文档：列出所有 `exec` step 的 `op` 和专用 action |
| `third_party/nlohmann/json.hpp` | 单头文件 JSON parser，MIT 许可证 |

### 如何编辑规则

**所有规则改动都直接手动编辑 `rules_v2/<locale>.full.json`，没有生成器环节。** 每个语言文件包含 `resources`（命名 ICU 正则片段）和一个有序的 `pipeline.rules` 列表，规则顺序很重要。

1. **简单查找/替换：** 新增一条 `{ "id": ..., "pattern": ..., "replace": ... }` 规则。
2. **结构化改写（数字 / 日期 / 单位 / 货币）：** 使用 `{ "action": "exec", "params": { "steps": [...] } }`，所有可用 `op` 见 `docs/tts_ops.md`。
3. **新 primitive：** 只有当现有 op 无法表达新算法时，才在 `applyExecStep` 中新增 C++。

手工新增的规则请用语义化 `id`（如 `zh_list_dash_silent_1`）。

### rules_v2 是交付格式（所有语言）

**每个语种都加载 `rules_v2/<locale>.full.json`。** 这是唯一支持的格式——遗留的 v1 生成器和 `rules/*.json` 目录已被移除。

> [!IMPORTANT]
> `rules_v2/*.full.json` 是手工维护的交付唯一可信源，所有规则改动都直接编辑这些文件。

### 构建

统一二进制（覆盖所有语种）由 `test/scripts/build_unified.sh` 构建，运行时用 `--data data/<locale>` 选语种：

```bash
ICU_ROOT=/opt/homebrew/opt/icu4c bash test/scripts/build_unified.sh
./test/bin/tts_cli --data data/en < input.txt
./test/bin/tts_cli --data data/ru < input.txt   # 俄语的 MorphoDiTa 模型路径由 data/ru/config.json 指定
```

`.tagger` 模型格式说明（对照 `.pt` / FST）：`docs/linguist_delivery/morphodita_tagger_model_reference.md`。

后端架构与 config 格式见 `unified/README.md`。

### 匹配日志（调试）

设置 `TTS_NORMALIZER_DEBUG` 为任意非空值后，每次 regex 命中都会向 stderr 打一行日志，包括 `locale`、`rule_index`、`rule_id`、`action` 和 UTF-8 的 `matched` 片段。

```bash
export TTS_NORMALIZER_DEBUG=1
./test/bin/tts_cli --data data/zh
```
