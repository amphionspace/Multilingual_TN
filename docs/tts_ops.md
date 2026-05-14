# TTS 规范化：`exec` 步骤中的 `op` 参考（交接用）

本文档说明 **JSON 规则里 `action: "exec"` → `params.steps[]` 中每个 step 的 `"op"`** 的含义与常用字段。实现位置：`tts_normalizer_engine.cpp` 中的 `TtsNormalizerEngine::applyExecStep`。

---

## 如何维护本文档（扩展性）

1. **新增 `op`（引擎能力）**  
   - 在 `applyExecStep` 增加分支并实现逻辑。  
   - **在本文件「按字母序的 `op` 一览」表中追加一行**：说明语义、主要参数、默认行为、适用语言。  
   - 若该 `op` 仅某语言使用，在「语言采用情况」列写明；通用 `op` 写 **通用**。

2. **新增语言（例如 `ja`）**  
   - 增加 `rules/<locale>.json`，按需组合现有 `op`；仅在缺 primitive 时再新增 `op`。  
   - 在本文件追加一节 **「语言采用矩阵」** 一行，或在该语言 README 中链接回本表并列出「本语言用到的 `op`」。  
   - 可选：在下方 **「变更记录」** 追加日期与摘要。

3. **规则调试日志（交接）**  
   - 设置环境变量 **`TTS_NORMALIZER_DEBUG`**（任意非空）可在 stderr 打印 **`rule_index`**、**`rule_id`**、**`action`**、**`matched`**。  
   - **`id`** 由 **`tools/generate_tts_rules_json.py`** 在写出 `en.json` / `zh.json` / `ar.json` / `bn.json` / `ru.json` 时自动写入（形如 **`en_00`**、**`zh_10`**、**`ar_05`**、**`bn_08`**、**`ru_03`**），数字部分与 **`rule_index`** 一致；手工改 JSON 时可改 `id` 字符串，但建议保持与顺序一致以免混淆。

4. **与实现保持一致**  
   - 以 `tts_normalizer_engine.cpp` 为准；参数默认值以代码中 `jgetS` / `value(..., default)` 为准。  
   - 定期对照：`grep 'if (op ==' tts_normalizer_engine.cpp` 与本表 `op` 名称。

5. **多语言 JSON 字段一览（单表）**  
   - 见 **`docs/tts_locale_field_matrix.md`** 与 **`docs/tts_locale_field_matrix.csv`**（由 `tools/export_locale_field_matrix.py` 生成，**非运行时机读取**）。

6. **JSON Schema（结构校验）**  
   - 见 **`docs/rule.schema.json`**（全语言共用一份；不要求每种语言包含全部规则类型）。  
   - 校验：`pip install jsonschema` 后运行 **`tools/validate_rules_schema.py`**。在 `applyAction` 中新增专用 `action` 时，请同步更新 Schema 里 `dedicated-action` 分支的 **`enum`**。

7. **Phase B — `pattern_defs`（乐高块）**  
   - 顶层 **`pattern_defs`**：命名 ICU 片段；**`rules[].pattern`** 可写 **`{{NAME}}`**；引擎在 **`loadRules`** 内先展开 defs 再展开每条规则（见 **`tools/pattern_blocks.py`** + **`tts_normalizer_engine.cpp`**）。  
   -  literals（TSV）一般仍为**无占位符**的完整 `pattern`。

---

## `exec` 规则长什么样（极简）

```json
{
  "pattern": "... ICU 正则 ...",
  "action": "exec",
  "params": {
    "steps": [
      { "op": "某操作名", "...其它字段..." }
    ]
  }
}
```

- 一条规则可有 **多个 step**，按数组顺序执行；每步输出拼接逻辑由规则编写方式决定（常见为单步）。  
- **捕获组**：`g`、`g1`、`hour_g` 等均为 **1-based**，与 ICU `RegexMatcher::group` 一致。

---

## 语言采用情况（当前仓库）

| 语言代码 | 规则文件 | 备注 |
|----------|-----------|------|
| `en` | `rules/en.json` | |
| `zh` | `rules/zh.json` | 另有拼音映射 `rules/zh_pinyin.json`（非 `op`，见下文） |
| `ar` | `rules/ar.json` | 月份名可选 `month_names_ar`（与 `date_iso_en` 配合） |
| `bn` | `rules/bn.json` | 月份名可选 `month_names_bn`；序数由 `bn.cpp` 回调（১–১০ 特例 + `তম`） |
| `ru` | `rules/ru.json` | 形态分析驱动数字变格（`ru_num_morph` + MorphoDiTa） |

**后续新增语言**：在本表追加一行即可。

---

## 按字母序：`op` 一览

表中 **常用参数** 列出编写规则时最常改的字段；未列出的可选字段见代码。

| `op` | 作用（大白话） | 常用参数 | 语言 / 说明 |
|------|----------------|----------|-------------|
| `aircraft_series_en` | **飞机型号数字**：基数整读易误听（如 350→three hundred fifty）；本 op 按常见读法拆段拼读。 | `variant`：`airbus_a`（三位数：百位数字 + 后两位，如 *three fifty*；末两位为 00 则整段基数）\|`boeing_7xx`（700–799：*seven* + 后两位，如 *seven eighty-seven*；整百则基数）`num_g`：数字捕获组 | 当前 **en** 规则里与 `EN_AIRBUS_A_SERIES` / `EN_BOEING_787_DREAMLINER` 配合 |
| `currency` | 货币符号 + 金额：去掉分隔符后数字拼读，再拼读法映射（如美元、元）。 | `two_group` / `sym_g`+`num_g`；或 `sym_g1`,`num_g`,`sym_g2`；`strip`；`pad_en`；`fallback_en` / `fallback_zh` / `fallback_ar` / `fallback_bn` / `fallback_ru` | 通用（映射表在对应 locale 的 JSON） |
| `date_iso_en` | ISO 风格 **YYYY-MM-DD**（及英文管道里 **DD/MM/YYYY**）→ 月份名 + 日期序数 + 年份拼读（模板仍带英文逗号空格习惯）。非法日期则退回整段原文。**`locale`=`en` 时，约 1200–1999 且末两位非 00 的年份按口语年读法**（*nineteen forty-one*）；否则整段基数拼读。 | `gy`,`gm`,`gd`（年、月、日捕获组，默认 1,2,3） | **`locale`=`ar`/`bn`/`ru` 且配置了 `month_names_ar`/`month_names_bn`/`month_names_ru` 时用对应语言月份名**，否则月份名走英文默认表 |
| `date_ymd` | **中文年月日**：三段分别可选「逐位数字」或「整段拼读」，再接后缀（年/月/日）。 | `g1`,`g2`,`g3`；`year_mode` / `month_mode` / `day_mode`：`digits` 或 `spellout`；`year_suffix` 等 | **中文**为主；逻辑通用 |
| `decade_zh` | 从匹配串里抽出阿拉伯数字，**逐位中文数字名**，再加后缀（默认「年代」）。 | `g`；`suffix` | **中文**场景 |
| `digits` | 指定捕获组内 **仅 0–9** 转为当前 locale 的数位读法（`digit_names`）。 | `g` | 通用 |
| `dms_zh` | **度分秒**：三组数字分别拼读 + 「度」「分」「秒」。 | `g1`,`g2`,`g3`；`deg`,`min`,`sec` | **中文**标签；拼读仍走回调 |
| `grp` | 取某一捕获组，再按模式处理：**拼读 / 序数 / 逐位 / 原文 / 两位年 / 英文月份全名 / 英文年份口语**。 | `g`；`as`：`spellout` \| `ordinal` \| `digits` \| `raw` \| `year_2digit` \| `month_en_full` \| `year_en_prose`（四位年：约 1200–1999 且末两位非 00 → *nineteen forty-one*；否则整段基数拼读；非 `en` locale 时等同 `spellout`） | 当前 **en** 用得较多 |
| `integral` | 定积分话术：**从 X 到 Y**（英文 `integral from … to …`；中文「从…到…积分」；阿语 `variant: ar`；孟语 `variant: bn` 默认「… থেকে … পর্যন্ত সমাকল」）。 | `g1`,`g2`；`variant`：`en` \| `zh` \| `ar` \| `bn`；阿语 `prefix_ar`/`mid_ar`/`suffix_ar`；孟语 `prefix_bn`/`mid_bn`/`suffix_bn` | 按 variant |
| `label_suffix` | **标签 + 数字 + 后缀**（如章节号），可控制标签/后缀大小写，数字拼读或逐位。 | `g1`,`g2`,`g3`；`label_case`；`number_mode`；`suffix_case` | 当前 **en** |
| `lit` | 输出固定字面量（与捕获组无关）。 | `text`（UTF-8 字符串） | 通用 |
| `loose_num` | 兜底：**整段数字拼读**；英文前后加空格便于断句。 | `g`（默认 0 表示整匹配，见代码） | 通用 |
| `minus` | 在捕获组前加负号前缀（英文 `minus `，中文 `负`，阿语 `سالب `，孟语 `মাইনাস `，俄语 `минус `）。**后面仍是原文数字串**，是否再拼读取决于其它规则。 | `g`；`minus_en`；`minus_zh`；`minus_ar`；`minus_bn`；`minus_ru` | 通用 |
| `ordinal` | 序数拼读（由 locale 的 `ordinal_spellout` 回调实现，如 `bn.cpp`）。前后可垫字符串。 | `g`；`pad_before`；`pad_after` | 依赖驱动 |
| `pow10` | 上标指数 → 普通数字，再读 **10 的幂**（英文 / 中文 / 阿语 / 孟语 / 俄语不同前缀后缀）。 | `g`；`prefix_en` / `minus_en`；`prefix_zh` / `minus_zh` / `suffix_zh`；阿语 `prefix_ar` / `minus_ar` / `suffix_ar`；孟语 `prefix_bn` / `minus_bn` / `suffix_bn`；俄语 `prefix_ru` / `minus_ru` / `suffix_ru`（见代码默认值） | 通用（文案随 locale） |
| `roman` | **罗马数字**（长度 > 1）转阿拉伯数字再拼读，并加尾随空格；过短则原样返回。 | `g` | 通用 |
| `ru_num_morph` | 俄语数字变格：按匹配 span 查 MorphoDiTa 的 `Case/Gender/Number`，命中则走 `spellout_with_morph`，否则回退普通 `spellout`。 | `g`（默认 0） | **俄语** |
| `spellout_affix` | 数字 **整段拼读**，前后加前缀/后缀（如「百分之」）。 | `g`；`prefix`；`suffix` | 通用 |
| `time_en_12h` | **英文 12 小时制**（含 am/pm），含整点、oh 分、秒、午夜/正午特例；非法则退回原文。 | `hour_g`,`minute_g`,`second_g`,`ampm_g`；多种文案键 `lead`,`oclock`,`oh`,… | **英文** |
| `time_en_24h` | **英文 24 小时制**；非法则退回原文。 | 同上（无 ampm） | **英文** |
| `time_clock` | **时:分[:秒]**：三组数字均 **spellout**，中间用可配置的 `colon` / 后缀连接（用于阿语 24h 等）。 | `hour_g`,`minute_g`,`second_g`（默认 1,2,3）；`colon`；`minute_suffix`；`second_suffix` | **阿语**等 |
| `time_zh_24` | **中文 24 小时**：时、分、秒拼读 + 「点」「分」「秒」。 | `hour_g`,`minute_g`,`second_g`；`colon` 等 | **中文** |
| `time_zh_range` | **中文时间范围**：`HH:MM[-—]HH:MM`（可带秒）→ `X点[Y分][Z秒]到A点[B分][C秒]`；整点时省略“零分”。 | `hour_g1`,`minute_g1`,`second_g1`,`hour_g2`,`minute_g2`,`second_g2`；`hour_suffix`,`minute_suffix`,`second_suffix`,`range_sep` | **中文** |
| `time_zh_ampm` | **中文 12 小时制**：根据 AM/PM 或「上午/下午」转为 24 小时拼读，再「点分秒」。 | `hour_g`,`minute_g`,`second_g`,`period_g`；`pm_alt`,`am_alt` 等 | **中文** |
| `unit` | **数 + 单位**：数字拼读后查 `unit_single` / `unit_composite` 表；未命中则数字 + 原文单位。 | `num_g`,`unit_g`；`table`：`single` \| `composite`；`pad_en` | 通用（表在 locale JSON） |
| `walk` | **逐字符**：数字逐位读；字符若在 `map` 中则替换（`map` 的值可为 `{en, zh, ar, bn}` 等对象，优先当前 `locale`）；否则保留；可选英文相邻插空格。 | `g`；`map`；`other_en_space` | 通用 |
| `walk_readout` | 类似 walk：**字母**可按间隔读出；数字逐位；其它字符查 `map`（值为 string）。 | `g`；`map`；`letter`：`spaced` 等 | **中文**型号等场景 |

---

## 非 `exec` 的 `action`（仍在引擎里）

以下写在规则的 **`"action"`** 上，**不是** `steps[].op`，但交接时需要知道：

| `action` | 作用概要 |
|----------|-----------|
| （空） + `replace` | 纯 ICU 替换模板（`$0`…`$9`）。 |
| `pinyin_han_paren_zh` | `汉字(拼音)` → 按音节查 `zh_pinyin.json`，组装输出；未命中为 `?`。 |
| `pinyin_standalone_zh` | 独立拼音串 → 查表替换；未命中保留原拼音。 |
| `emoji_clear` | 匹配段替换为空（去掉 emoji）。 |

---

## 变更记录（模板）

| 日期 | 变更 |
|------|------|
| （初次撰写） | 建立本文档，覆盖当前引擎全部 `op`。 |
| 2026-05 | 增加 `ar` 规则文件说明；补充 `time_clock`、`minus_ar` / `fallback_ar` / `pow10` 阿语分支、`integral` 的 `variant: ar`、`date_iso_en` 与 `month_names_ar`。 |
| 2026-05 | 增加 `bn`：`month_names_bn`、`integral` 的 `variant: bn`、`minus_bn` / `fallback_bn` / `pow10` 孟语分支。 |
| 2026-05 | 增加 `ru`：`ru_num_morph`、MorphoDiTa 特征回调、`month_names_ru`。 |

以后每次新增 `op`、或某语言开始/停止使用某 `op`，在本表追加一行。

---

## 相关文件

| 路径 | 说明 |
|------|------|
| `tts_normalizer_engine.cpp` | `applyExecStep`、`applyAction`、`runPipeline` |
| `tts_normalizer_engine.hpp` | `loadRules`、`TtsCallbacks` |
| `rules/en.json` / `rules/zh.json` / `rules/ar.json` / `rules/bn.json` / `rules/ru.json` | 运行时加载的规则 |
| `tools/generate_tts_rules_json.py` | 由 Python 数据生成上述 JSON |
