# 统一 TextNormalizer（Phase 0 + Phase 1）

单一 `TextNormalizer` + 后端注册表。语种在运行期由资源目录决定，新增语种只需新增 `data/xx/`（若能复用已有后端则零代码）。

```cpp
TextNormalizer tn;
tn.Init("data/ru");                       // 读 data/ru/config.json，装配俄语后端
std::string out = tn.normalizeLine(line);
```

## 组成
- `backend.hpp` / `backend.cpp` —— `LanguageBackend` 接口 + `BackendRegistry`（id→工厂，自注册）。
- `text_normalizer.hpp/.cpp` —— 统一门面 `Init(resourceDir)`：读 `config.json` → 建后端 → 载规则/拼音 → 装配。
- `icu_backend.hpp/.cpp` —— 通用 ICU-RBNF 基类 `IcuSpelloutBackend`（`backend:"icu"`），新语种默认后端；en/ar/zh 后端都继承它。
- `english_backend.cpp` —— en：ICU + Phase 2 从引擎搬出的英语 `grp` 模式（`year_en_prose`/`month_en_full`，经回调注入）。
- `arabic_backend.cpp` —— ar：ICU + `ar_num_morph`/`ar_date_day` op（含从引擎逐字搬来的阿语形态词表）。
- `chinese_backend.cpp` —— zh：ICU + 自持 pinyin 表 + `time_zh_ampm` op + 两个 pinyin action。
- `bengali_backend.cpp` —— bn 手写读数（印度记数法 lakh/crore），ICU 无可用 bn 数据。
- `russian_backend.cpp` —— ru：MorphoDiTa 形态分析 + 按格/性/数选 RBNF；Phase 2 追加 8 个 `ru_*` op。**形态分析器保留且必需。**
- `op_helpers.hpp` —— 后端 op 共用的无状态小工具（jgetS/parseIntUStr/replaceAllInPlace/trimAsciiSpaces）。
- `tts_cli.cpp` —— 统一入口，`--data <dir>` 选语种，替代原 5 个 `main()`。

## config.json 字段
| 字段 | 说明 |
|---|---|
| `locale` | ICU locale 标签 |
| `backend` | 注册表 id：`icu` / `bengali` / `russian` |
| `rules` | rules_v2 规则文件路径（相对 config.json 目录） |
| `pinyin` | 可选，拼音表（zh） |
| `ordinal` | icu 系后端序数模式：`icu_int`(en) / `icu_double`(ar) / `cardinal`(zh) |
| `morph_model` | russian 后端的 MorphoDiTa 模型路径 |

当前各语种 `backend`：en→`english`、ar→`arabic`、zh→`chinese`、bn→`bengali`、ru→`russian`。

## 构建与验证
```bash
ICU_ROOT=/opt/homebrew/opt/icu4c bash test/scripts/build_unified.sh
./test/bin/tts_cli --data data/en < test/in/en.txt
```
本次交付已验证：统一二进制对 en/zh/ar/bn/ru 的输出与改造前的 5 个原始二进制**逐字节一致**。

## Phase 说明
- **Phase 0（脚手架）**：以上后端/注册表/`Init` 全部到位并跑通。
- **Phase 1（拆硬依赖）**：引擎删除 `#include "ru_year_spellout.hpp"`，`ru_year_fixup` 改经 `TtsCallbacks::parse_spoken_year` 回调取值。非俄语二进制现在**无需链接** `ru_year_spellout.cpp`。俄语形态分析能力不受影响。
- **Phase 2（专属 op 归位，已完成）**：`ru_*`（9 个）、`ar_*`（2 个）、`time_zh_ampm`、pinyin 两个 action、英语 `year_en_prose`/`month_en_full` 模式,连同其内联词表(阿语形态、俄语分数/年份、英文月份)全部从引擎删除,搬入各后端。引擎新增 `registerOp`/`registerAction` 供后端覆写;`grp` 的英语模式改为经 `cb.year_prose`/`cb.month_en_full` 回调委托。死代码 `fraction_en`/`date_iso_en`/`time_zh_range`(任何 JSON 均未使用)一并删除。引擎现已**不含任何语种专属逻辑或词表**。

## Phase 3(统一入口,已完成)
5 个旧单体 driver(`en/zh/ar/bn/ru.cpp`,各自带 `main()` + `TextNormalizer`)已全部删除,连同遗留的 `test/scripts/build.sh`。唯一入口是 `tts_cli --data data/xx`;`test/scripts/run_all.sh` 也改为驱动统一二进制。产物形态:**1 个引擎(`tts_normalizer_engine`)+ N 个后端(`unified/*_backend.cpp`)+ 1 个 CLI(`tts_cli`)**。
