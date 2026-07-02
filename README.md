# ICU TTS Text Normalization / ICU TTS 文本归一化

## English

This project is a config-driven multilingual text normalization prototype for TTS frontends. Rules are stored as **JSON** under `rules_v2/`, so most product-side pattern, replacement, unit, and currency tweaks do not require editing C++.

**Named pattern fragments:** each locale file has a top-level **`resources`** map of named ICU fragments. **`rules[].pattern`** strings may embed **`{{NAME}}`** placeholders; `TtsNormalizerEngine::loadRulesV2` resolves them after stabilizing mutual references among defs.

### Layout

| Path | Role |
|------|------|
| `en.cpp` / `zh.cpp` / `ar.cpp` / `bn.cpp` / `ru.cpp` | Thin drivers: ICU `RuleBasedNumberFormat` + load JSON + run pipeline |
| `tts_normalizer_engine.hpp` / `tts_normalizer_engine.cpp` | ICU regex runner; pipeline actions are mostly `action: "exec"` with JSON `steps`. Only pinyin and emoji remain as dedicated C++ actions. |
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

**All binaries (`en` / `zh` / `ar` / `bn` / `ru`) load `rules_v2/<locale>.full.json`.** This is the only supported format — the legacy v1 generator and the `rules/*.json` tree have been removed.

> [!IMPORTANT]
> `rules_v2/*.full.json` is the hand-maintained source of truth for delivery. Make all rule changes by editing these files directly.

### Build Example

Paths depend on your ICU install, such as `brew install icu4c` on macOS or Conda.

```bash
cd ICU
g++ -std=c++17 -O2 en.cpp tts_normalizer_engine.cpp \
  -I. -Ithird_party \
  $(pkg-config --cflags --libs icu-icu) \
  -o en_tts

g++ -std=c++17 -O2 zh.cpp tts_normalizer_engine.cpp \
  -I. -Ithird_party \
  $(pkg-config --cflags --libs icu-icu) \
  -o zh_tts

g++ -std=c++17 -O2 ar.cpp tts_normalizer_engine.cpp \
  -I. -Ithird_party \
  $(pkg-config --cflags --libs icu-icu) \
  -o ar_tts

g++ -std=c++17 -O2 bn.cpp tts_normalizer_engine.cpp \
  -I. -Ithird_party \
  $(pkg-config --cflags --libs icu-icu) \
  -o bn_tts

g++ -std=c++17 -O2 ru.cpp tts_normalizer_engine.cpp \
  original/morphodita/src_lib_only/morphodita.cpp \
  -I. -Ithird_party \
  -Ioriginal/morphodita/src_lib_only \
  $(pkg-config --cflags --libs icu-icu) \
  -lpthread \
  -o ru_tts
```

`ru_tts` requires a MorphoDiTa model path at runtime:

```bash
./ru_tts --morph-model original/morphodita/models/russian-syntagrus-morphodita-only.tagger
```

Model format (not PyTorch `.pt` or FST): see `docs/linguist_delivery/morphodita_tagger_model_reference.md`.

Rule files resolve relative to the source paths (via `__FILE__`), so the binaries find `rules_v2/` regardless of the current working directory.

### Debug Logging

Set `TTS_NORMALIZER_DEBUG` to any non-empty value to print one line to stderr for each regex hit, including `locale`, `rule_index`, `rule_id`, `action`, and the matched UTF-8 snippet.

```bash
export TTS_NORMALIZER_DEBUG=1
./zh_tts
```

## 中文

本项目是一个面向 TTS 前端的多语言文本归一化原型。大多数 pattern、replacement、单位和货币表都放在 `rules_v2/` 下的 **JSON** 中，因此产品侧调整规则时通常不需要改 C++。

**命名正则片段：** 每个语言规则文件都有顶层的 **`resources`**，用于定义命名 ICU 正则片段。**`rules[].pattern`** 可以通过 **`{{NAME}}`** 引用这些片段；`TtsNormalizerEngine::loadRulesV2` 会先稳定解析这些定义之间的互相引用，再展开到最终规则中。

### 目录结构

| 路径 | 作用 |
|------|------|
| `en.cpp` / `zh.cpp` / `ar.cpp` / `bn.cpp` / `ru.cpp` | 各语言的轻量 driver：使用 ICU `RuleBasedNumberFormat`，加载 JSON 规则并执行 pipeline |
| `tts_normalizer_engine.hpp` / `tts_normalizer_engine.cpp` | ICU regex 执行引擎；pipeline 规则基本都是 `action: "exec"` 加 JSON `steps`。当前只有拼音和 emoji 仍是专门的 C++ action |
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

**所有二进制（`en` / `zh` / `ar` / `bn` / `ru`）都加载 `rules_v2/<locale>.full.json`。** 这是唯一支持的格式——遗留的 v1 生成器和 `rules/*.json` 目录已被移除。

> [!IMPORTANT]
> `rules_v2/*.full.json` 是手工维护的交付唯一可信源，所有规则改动都直接编辑这些文件。

### 构建示例

路径取决于本机 ICU 安装位置。macOS 可通过 `brew install icu4c` 安装，也可以使用 Conda 中的 ICU。构建命令见上方英文部分，俄语 `ru_tts` 运行时需要指定 MorphoDiTa 模型：

```bash
./ru_tts --morph-model original/morphodita/models/russian-syntagrus-morphodita-only.tagger
```

`.tagger` 模型格式说明（对照 `.pt` / FST）：`docs/linguist_delivery/morphodita_tagger_model_reference.md`。

规则文件按源码路径（`__FILE__`）解析，因此无论在哪个工作目录运行，二进制都能找到 `rules_v2/`。

### 匹配日志（调试）

设置 `TTS_NORMALIZER_DEBUG` 为任意非空值后，每次 regex 命中都会向 stderr 打一行日志，包括 `locale`、`rule_index`、`rule_id`、`action` 和 UTF-8 的 `matched` 片段。

```bash
export TTS_NORMALIZER_DEBUG=1
./zh_tts
```
