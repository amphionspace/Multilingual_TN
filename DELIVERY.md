# 交付说明 / Delivery Note — 统一 TextNormalizer 重构

## 中文

### 目标
按客户意见，将原先「每个语种一个 `TextNormalizer`（新增语种即改动态库）」重构为**所有语种共用一个 `TextNormalizer`**：统一流程 `normalizeLine`，初始化时按资源目录选语种——`TextNormalizer::Init("data/ru")` 得到俄语、`Init("data/en")` 得到英语。

### 已实现
- **单一入口**：`TextNormalizer::Init(resourceDir)` 读取 `data/<locale>/config.json`（locale、规则文件、拼音、序数模式、俄语 MorphoDiTa 模型路径等），装配对应后端。统一 CLI：`tts_cli --data data/<locale>`。
- **瘦引擎 + 后端注册表**：`tts_normalizer_engine` 已不含任何语种专属逻辑或词表；各语种逻辑落在 `unified/*_backend.cpp`，经 `registerOp`/`registerAction` 注册。
- **产物形态**：由「5 个二进制」变为「1 引擎 + N 后端 + 1 CLI」。
- **新增常规语种零代码**：若目标语种的数字读法 ICU 已覆盖，仅需新增 `data/xx/config.json`（`backend:"icu"`）+ 规则文件，**不改引擎、不改动态库**。

### 边界（本质复杂度，非架构缺陷）
- **ru**：仍依赖 MorphoDiTa 形态分析器（数字格位归一化的前提），随俄语后端保留、行为不变。
- **bn**：ICU 无可用孟加拉语读数，采用内置手写读数后端（印度记数法 lakh/crore）。
- 因此，一个「ICU 无法读数、又需要复杂形态」的**全新**语种仍需编写一个后端；只有 ICU 覆盖的常规语种才是真正零代码。

### 验证（无回退）
- 两个真实测试集，新架构与原架构（同一 `rules_v2` 规则、同一输入）输出**逐字节一致**：
  - transsion 1000 句样本 × 5 语言（en/zh/ar/bn/ru）：全部 MD5 相同。
  - transsion_cmos × 3 语言（en/zh/ru）：全部 MD5 相同。
- 结论：Phase 0→3 重构对所有受测语言**零行为差异**。

### 构建与运行
```bash
ICU_ROOT=/opt/homebrew/opt/icu4c bash test/scripts/build_unified.sh
./test/bin/tts_cli --data data/en < input.txt
./test/bin/tts_cli --data data/ru < input.txt   # 俄语模型路径由 data/ru/config.json 指定
```
后端架构与 config 字段详见 `unified/README.md`。

### 说明
- 旧的 5 个单体 `.cpp`（en/zh/ar/bn/ru）及 golden 测试集（`test/expected/`）已移除；统一入口取而代之。

---

## English

### Goal
Per the customer's request, replace the previous "one `TextNormalizer` per language (adding a language means changing the shared library)" with **a single shared `TextNormalizer`**: one `normalizeLine` flow, language chosen at init by resource directory — `TextNormalizer::Init("data/ru")` yields Russian, `Init("data/en")` yields English.

### What was delivered
- **Single entry point**: `TextNormalizer::Init(resourceDir)` reads `data/<locale>/config.json` (locale, rules file, pinyin, ordinal mode, Russian MorphoDiTa model path, …) and wires up the matching backend. Unified CLI: `tts_cli --data data/<locale>`.
- **Thin engine + backend registry**: `tts_normalizer_engine` no longer holds any language-specific logic or vocabulary; per-language logic lives in `unified/*_backend.cpp`, registered via `registerOp`/`registerAction`.
- **Product shape**: from "5 binaries" to "1 engine + N backends + 1 CLI".
- **Zero-code for ordinary new languages**: if ICU already covers the number spellout, add only `data/xx/config.json` (`backend:"icu"`) + a rules file — **no engine change, no shared-library change**.

### Boundaries (intrinsic complexity, not an architecture gap)
- **ru**: still depends on the MorphoDiTa analyzer (required for case-correct numeral normalization); kept in the Russian backend, behavior unchanged.
- **bn**: ICU has no usable Bengali spellout, so a hand-written Indic (lakh/crore) backend is used.
- Therefore a brand-new language that ICU can't spell out *and* needs rich morphology still requires writing one backend; only ICU-covered ordinary languages are truly zero-code.

### Verification (no regression)
- On two real test sets, the new architecture and the original architecture (same `rules_v2` rules, same inputs) produce **byte-identical** output:
  - transsion 1000-sentence sample × 5 languages (en/zh/ar/bn/ru): all MD5s match.
  - transsion_cmos × 3 languages (en/zh/ru): all MD5s match.
- Conclusion: the Phase 0→3 refactor has **zero behavioral difference** across all tested languages.

### Build & run
```bash
ICU_ROOT=/opt/homebrew/opt/icu4c bash test/scripts/build_unified.sh
./test/bin/tts_cli --data data/en < input.txt
./test/bin/tts_cli --data data/ru < input.txt   # Russian model path comes from data/ru/config.json
```
See `unified/README.md` for the backend architecture and config fields.

### Notes
- The five legacy monolithic drivers (en/zh/ar/bn/ru `.cpp`) and the golden test set (`test/expected/`) were removed; the unified entry point replaces them.
