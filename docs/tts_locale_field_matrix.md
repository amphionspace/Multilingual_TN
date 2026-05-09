# 多语言规则文件：统一字段矩阵（参考）

本文件由 `tools/export_locale_field_matrix.py` **根据当前 `rules/*.json` 自动生成**，仅供查阅与交接；**运行时不会读取**本文件或 CSV。

表中 **✓** 表示该语言的 JSON **根对象**或**至少一条规则**使用了该字段；空白表示当前仓库中**未使用**（将来可填）。新增语言时：

1. 增加 `rules/<locale>.json`；
2. 在脚本 `LOCALE_FILES` 或 `FUTURE_LOCALE_COLUMNS` 中加入列名；
3. 重新运行 `python3 tools/export_locale_field_matrix.py`。

## 根级字段（`rules/<locale>.json` 顶层）

| 字段 | 说明 | en | zh | ar | bn | ru | ja | de | ko | fr |
|------|------|--------|--------|--------|--------|--------|--------|--------|--------|--------|
| `currency_map` | Object: currency symbol / code → spoken suffix after spellout amount. | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `digit_names` | Length-10 array: names for digits 0–9 (used by `digits` / `walk` / `date_ymd` digit modes). | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `locale` | BCP-47-ish tag consumed by the engine (`localeTag_`). | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `month_names_ar` | Optional length-12 array for Arabic Gregorian month names (`date_iso_en` when locale is ar). |   |   | ✓ |   |   |   |   |   |   |
| `month_names_bn` | Optional length-12 array for Bengali Gregorian month names (`date_iso_en` when locale is bn). |   |   |   | ✓ |   |   |   |   |   |
| `month_names_en` | Optional length-12 array overriding English month names (Jan–Dec); engine default if omitted. |   |   |   |   |   |   |   |   |   |
| `month_names_ru` | Optional length-12 array for Russian month names (`date_iso_en` when locale is ru). |   |   |   |   | ✓ |   |   |   |   |
| `pattern_defs` | Phase B: map NAME → ICU substring; rules may use {{NAME}}; expanded in loadRules. | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `rules` | Ordered array of rule objects; order is pipeline order. | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `unit_composite` | Object: composite unit token → spoken suffix after spellout number. | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `unit_single` | Object: single-letter or short unit → spoken suffix after spellout number. | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |

## 规则对象字段（`rules[]` 每一项可出现键的并集）

同一语言里并非每条规则都包含全部键（例如 `exec` 用 `params`，纯替换规则用 `replace` 且无 `action`）。

| 字段 | 说明 | en | zh | ar | bn | ru | ja | de | ko | fr |
|------|------|--------|--------|--------|--------|--------|--------|--------|--------|--------|
| `action` | Handler name: `exec`, `pinyin_han_paren_zh`, `pinyin_standalone_zh`, `emoji_clear`, or omit for replace-only. | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `id` | Stable rule id for logs / handoff (generator: `en_00`, `zh_10`, `ar_05`, `bn_08`, `ru_03`, …). | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `params` | Object; for `action: exec`, must include `steps` (array of `{op, ...}`). | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `pattern` | ICU regular expression; required on every rule. | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |
| `replace` | Replacement template with `$0`…`$9`; used when `action` is absent. | ✓ | ✓ | ✓ | ✓ | ✓ |   |   |   |   |

## 附属数据（非 `rules/<locale>.json` 主文档）

| 文件 | 说明 | en | zh | ja | de | ko | fr |
|------|------|----|----|----|----|----|----|
| `rules/zh_pinyin.json` | 带声调拼音音节 → 单字（供 `pinyin_*` 规则查表）。 | | ✓ |  |  |  |  |

## 单表 CSV

便于在 Excel / Numbers 中筛选：见同目录 **`tts_locale_field_matrix.csv`**（UTF-8）。

