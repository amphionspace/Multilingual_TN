# TTS 规范化：`exec`/`action` 对照文档（按当前代码）

本文档以 `tts_normalizer_engine.cpp` 为唯一真相，列出当前引擎实际支持的：
- `action: "exec"` 下 `params.steps[].op`
- 非 `exec` 的专用 `action`

> 同步基准：`TtsNormalizerEngine::applyExecStep` 与 `TtsNormalizerEngine::applyAction`。

---

## 快速维护流程

1. 在 `tts_normalizer_engine.cpp` 修改/新增 `if (op == "...")` 或 `if (action == "...")`。  
2. 同步更新本文件的对应表格。  
3. 用下面命令做对照检查：

```bash
rg 'if \(op == "' tts_normalizer_engine.cpp
rg 'if \(action == "' tts_normalizer_engine.cpp
```

---

## `exec` 规则最小形态

```json
{
  "pattern": "... ICU regex ...",
  "action": "exec",
  "params": {
    "steps": [
      { "op": "xxx", "...": "..." }
    ]
  }
}
```

- `steps` 按顺序执行并拼接输出。  
- 捕获组编号遵循 ICU 规则（0=整段匹配，1..N=分组）。  
- 未识别 `op` 会回退为 `m.group(0)`（返回原命中片段）。

---

## 语言覆盖（当前仓库）

| locale | 规则文件 | 备注 |
|---|---|---|
| `en` | `rules_v2/en.full.json` | 英文主规则 |
| `zh` | `rules_v2/zh.full.json` | 含拼音 action；映射表 `rules_v2/zh_pinyin.json` |
| `ar` | `rules_v2/ar.full.json` | 阿语规则 |
| `bn` | `rules_v2/bn.full.json` | 孟加拉语规则 |
| `ru` | `rules_v2/ru.full.json` | 含 `ru_num_morph`（MorphoDiTa 特征回调） |

---

## 当前 `op` 一览（按字母序）

| `op` | 作用 | 常用参数 | 说明 |
|---|---|---|---|
| `date_iso_en` | ISO 日期转英文月份+日期序数+年份 | `gy`, `gm`, `gd`, `year_as` | `year_as=year_en_prose` 时使用英文年份口语化 |
| `digits` | 捕获组中数字逐位读（仅 `0-9`） | `g` | 非数字字符被忽略 |
| `fraction_en` | 英文分数表达（含 whole and fraction） | `whole_g`, `num_g`, `den_g` | 对 `1/2`、`1/4` 有 half/quarter 特化 |
| `grp` | 通用分组处理 | `g`, `as` | `as`: `raw` / `spellout` / `ordinal` / `digits` / `year_2digit` / `year_en_prose` / `month_en_full` |
| `label_suffix` | 标签+数字+后缀组合输出 | `g1`, `g2`, `g3`, `label_case`, `number_mode`, `suffix_case` | 常用于英文章节/编号类 |
| `lit` | 输出固定文本 | `text` | 不读取匹配内容 |
| `lookup_currency` | 查货币符号映射（`currency_map`） | `g`, `num_g`, `singular`, `fallback` | 可按 `num_g` 做英文单复数 |
| `lookup_map` | 在 step 的 `map` 里按 key 查值 | `g`, `map`, `fallback`, `default_raw` | `map` 值需为字符串 |
| `lookup_table` | 查单位表（`unit_single`/`unit_composite`） | `g`, `num_g`, `table`, `singular` | 表来自规则 JSON 顶层字段 |
| `loose_num` | 数字清洗后拼读（可两侧补空格） | `g`, `strip`, `pad` | 兜底数字规则常用 |
| `normalize_mixed_digits` | 统一 mixed digits（全角/阿拉伯-印地等）到普通数字 | `g` | 调用 `toNormalDigitsMixed` |
| `ordinal` | 序数拼读并加前后垫字串 | `g`, `pad_before`, `pad_after` | 依赖 `ordinal_spellout` 回调 |
| `roman` | 罗马数字转阿拉伯再拼读 | `g` | 长度<=1 时原样返回 |
| `ru_num_morph` | 俄语数字变格拼读 | `g`；可选 `case`/`gender`/`number`/`force_morph` | 命中形态特征时走 `spellout_with_morph`；若指定 `case` 等则强制变格（如比分 `к` + 与格）；否则回退普通拼读 |
| `ru_year_spellout` | 俄语年份序数拼读（1800–2099） | `g`（四位年）；`mode`: `neut`/`gen`/`fem`/`nom`/`loc` | 需 `TtsCallbacks.ru_year_spellout`（`ru_tts` 内置）；替代大段 `lookup_map` 年份表 |
| `ru_ordinal_spellout` | 俄语序数拼读 | `g`；`mode`: `nom`/`gen` | 如 `39-го`→`тридцать девятого`；需 `TtsCallbacks.ru_ordinal_spellout` |
| `ru_year_fixup` | 已拼读年份短语纠错 | `g`（整段含「года」）；`mode`；`suffix`（默认 ` года`）；`parse_spoken` | 从基数口语反推年份再 `ru_year_spellout`；用于 late cleanup |
| `ru_decimal_whole_cardinal` | 俄语小数整数部分拼读 | `g` / `int_g` | 优先 MorphoDiTa 变格；末位为 1 时与 `целая` 一致改为阴性 |
| `ru_decimal_whole_word` | 俄语小数连接词 | `g` / `int_g` | 输出 `целая` 或 `целых` |
| `ru_decimal_frac_cardinal` | 俄语小数小数部分拼读 | `g` / `frac_g` | 普通拼读 + 阴性一致（不变格）；`strip` 默认 `,` |
| `ru_decimal_denominator` | 俄语小数分母词 | `g` / `frac_g` | 按小数位数输出 `сотая/сотых`、`тысячная/тысячных` 等 |
| `ru_ipv4_spellout` | 俄语 IPv4 按八位组拼读 | `g`；`sep`（默认 ` точка `）；`port_sep`（默认 ` двоеточие `） | 每段用 cardinal 拼读，如 `10.0.0.1` → `десять точка ноль…` |
| `spellout_affix` | 数字拼读后加前后缀 | `g`, `prefix`, `suffix` | 常用于百分比等 |
| `spellout_clean` | 去掉指定字符后再拼读 | `g`, `strip` | 默认 `strip=","` |
| `spellout_superscript` | 上标数字转普通数字后拼读 | `g`, `minus` | 可输出负号前缀 |
| `time_en_12h` | 英文 12 小时制时间表达 | `hour_g`, `minute_g`, `second_g`, `ampm_g` | 支持 midnight/noon、可配置文案 |
| `time_en_24h` | 英文 24 小时制时间表达 | `hour_g`, `minute_g`, `second_g` | 支持整点/秒级文案 |
| `time_zh_ampm` | 中文 AM/PM 转 24h 再读出 | `hour_g`, `minute_g`, `second_g`, `period_g` | `pm_alt`/`am_alt` 可配置中文时段词 |
| `time_zh_range` | 中文时间区间表达 | `hour_g1..second_g2`, `hour_suffix`, `minute_suffix`, `second_suffix`, `range_sep` | 整点区间会省略“零分” |
| `walk` | 逐字符走读（字母/数字/映射/原字符） | `g`, `map`, `letter`, `other_space`/`other_en_space` | `letter=spaced` 时字母前后加空格 |

---

## 非 `exec` 的专用 `action`

| `action` | 说明 |
|---|---|
| （空） + `replace` | 走 ICU 替换模板（`$0..$9`），对应 `expandReplacement` |
| `pinyin_han_paren_zh` | `汉字(拼音)`：按拼音音节查 `zh_pinyin.json`，未命中写 `?` |
| `pinyin_standalone_zh` | 独立拼音串查表，未命中保留原拼音 |
| `emoji_clear` | 命中片段清空 |

---

## 与旧文档差异（避免混淆）

以下名称在当前代码中**不是** `op`（旧版本文档残留）：
- `aircraft_series_en`, `currency`, `unit`, `pow10`, `minus`, `integral`
- `date_ymd`, `decade_zh`, `dms_zh`
- `time_clock`, `time_zh_24`, `walk_readout`

对应能力已由当前 `lookup_*`、`spellout_*`、`time_*` 等实现替代或下线。

---

## 相关文件

| 路径 | 说明 |
|---|---|
| `tts_normalizer_engine.cpp` | `applyExecStep`、`applyAction`、`runPipeline` |
| `tts_normalizer_engine.hpp` | `TtsCallbacks` 声明与引擎接口 |
| `rules_v2/*.full.json` | 运行时规则与映射表（单位、货币、digit_names 等） |
