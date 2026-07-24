# ICU 体积裁剪 / ICU size reduction

把 TN 依赖的 ICU 从 **~36 MB 裁到 ~4.4 MB(约 8×)**,归一化输出与裁剪前**逐字节一致、零回退**。
不替换 ICU、不动俄语 22 MB 形态模型(该模型独立、仅俄语部署携带)。

## 两大杠杆

### ① 数据裁剪(`ICU_DATA_FILTER_FILE`,源码构建期）
TN 只用到 ICU 的:RBNF 读法(en/zh/ar/ru)、正则、UnicodeString、字符属性/大小写、归一化。
用 [`filter.json`](filter.json) 按功能裁剪 ICU 数据：

- **保留**：rbnf(限 4 语种)、normalization、字符属性/ucase、**currency**（RBNF 内部 `DecimalFormatSymbols` 依赖，**务必保留**；早期误删过,导致读法初始化 `U_MISSING_RESOURCE_ERROR`）。
- **删除**：collation 排序、break iterator 分词(含 CJK 词典)、transliterator、**时区**、**语言/地区/单位显示名**、**字符集转换表 `.cnv`(最大一块，约 4.9 MB)**、stringprep、confusables。

> 以实际配置 [`filter.json`](filter.json) 的 `featureFilters` 为准:被排除的是
> `coll_* / brkitr_* / translit / zone_tree / lang_tree / region_tree / unit_tree /
> conversion_mappings / stringprep / confusables`。**`curr_tree` 不在排除之列。**

数据成分实测(icudt78l.dat)：31.57 MB → **1.93 MB**。
其中转换表 `.cnv` 占裁剪前的 4.91 MB（190 个 ibm-*/gb18030/euc-* 等遗留字符集，TN 只用内置的 UTF-8/16，非数据驱动）。

### ② 代码裁剪(链接期）
ICU 代码(i18n+uuc)动态库合计 5.27 MB，内含大量 TN 不用的模块(排序/分词/日期/日历/度量/消息格式…)。
静态链接 + 死代码消除 + strip，仅保留正则/RBNF/字符串路径 → 约 **1.7 MB**。

## 为什么删掉这些不影响功能(日期/度量/货币怎么实现的)

**TN 从未调用过 ICU 的日期/日历/度量/时区/排序/分词 API**，删掉的是从未被执行的代码与数据。
TN 只用到 ICU 四样东西:`UnicodeString`、`RegexMatcher`、`RuleBasedNumberFormat`(数字读法)、`Locale`。
日期、度量、货币的读法**全部由我们自己实现**，数据在 `rules_v2/*.full.json`:

| 功能 | TN 的实现 | 与 ICU 的关系 |
|---|---|---|
| 日期 | 规则正则 + 自有月份名(`resources.month_names`;英文月名内置于 `english_backend`) + 序数读法 | 不用 ICU 日期/日历 |
| 度量单位 | 自有 `resources.unit_single` / `unit_composite` + `lookup_table` op | 不用 ICU `unit_tree` |
| 货币 | 自有 `resources.currency_map` + `lookup_currency` op | 不用 ICU 货币**显示名**;保留的 `curr` 仅供 RBNF 取数字符号 |
| 时区 | TN 无时区功能 | 不用 |

被删的是 ICU 面向国际化 **UI** 的“显示名/区域数据”(如 “US Dollar”/“美元”/“Asia/Shanghai”)与排序、分词——与“把文本读成话”无关。
实测(裁剪后与裁剪前逐字节一致):`March 15, 2024 → March fifteenth, two thousand twenty-four`、
`5 kg → five kilograms`、`$3.50 → three point five dollars`、`12% → twelve percent`。

## 一键编译(推荐)

```bash
bash test/scripts/build_tts_so.sh
```

产出 `build/libtts_normalizer.so`(macOS 为 `.dylib`)——**单文件、自包含**:裁剪后的 ICU(数据+代码)、TN、MorphoDiTa 全部静态嵌入,**只导出 4 个 C API 符号**,无任何外部 `libicu` 依赖。

脚本自动完成:①拉取并缓存钉死版本的 ICU 源码 → ②按 [`filter.json`](filter.json) 构建裁剪版静态 ICU → ③链接成 `.so`(隐藏符号 + 死代码消除 + strip) → ④输出体积与导出符号。
ICU 构建按 **filter.json 的内容哈希缓存**:改了过滤配置会自动重编,没改则秒过。

**加语种时怎么调整**:编辑 [`filter.json`](filter.json) 的 `localeFilter.includelist`(如加 `"es"`),再新增 `data/es/`,重跑脚本即可——**不用改代码**。

常用环境变量:`FILTER`(过滤配置)、`ICU_SRC`(已有 ICU 源码目录,离线/交叉编译用)、`OUT`(输出路径)、`BUILD_DIR`、`JOBS`。

**接口**(见 [`unified/tts_capi.h`](../../unified/tts_capi.h)):
```c
tts_normalizer_t* h = tts_init("data/ru");      // 按资源目录选语种
const char* out = tts_normalize(h, line_utf8);  // 归一化一行
tts_free(h);                                    // 句柄非线程安全:每线程一个
```

> 交叉编译(Android/嵌入式):ICU 需先 host 构建、再用 `--host=... --with-cross-build=<host构建目录>` 构建目标;`ICU_SRC` 指向你们的源码树,**过滤配置与链接步骤不变**。数据部分体积与架构无关。

## 构建配方(手工步骤 / 交叉编译参考)

### ICU 源码构建(数据裁剪 + 静态）
```bash
# 目标平台请用对应的 runConfigureICU 参数/交叉工具链；数据部分体积与架构无关。
ICU_DATA_FILTER_FILE=/abs/path/filter.json \
  <icu>/source/runConfigureICU <Platform> \
    --enable-static --disable-shared --with-data-packaging=static
make -j
# 产物：lib/libicui18n.a  lib/libicuuc.a  lib/libicudata.a（data 已含裁剪后的 1.93 MB）
```

### 链接 TN（死代码消除 + strip）
- macOS: `-Wl,-dead_strip`
- Linux/Android(目标平台): 编译加 `-ffunction-sections -fdata-sections`，链接加 `-Wl,--gc-sections`
- 建议 ICU 符号 `-fvisibility=hidden`（.so 交付时只导出 TN 自己的 API，让链接器能剥离未用的 ICU 代码）

```bash
g++ -std=c++17 -O2 -fvisibility=hidden -ffunction-sections -fdata-sections \
  unified/*.cpp tts_normalizer_engine.cpp ru_year_spellout.cpp \
  original/morphodita/src_lib_only/morphodita.cpp \
  -I. -Ithird_party -Iunified -Ioriginal/morphodita/src_lib_only \
  -I<icu>/source/common -I<icu>/source/i18n \
  <icu-build>/lib/libicui18n.a <icu-build>/lib/libicuuc.a <icu-build>/lib/libicudata.a \
  -lpthread -Wl,-dead_strip -o tts_cli        # Linux: 用 -Wl,--gc-sections
strip -x tts_cli
```

## 实测结果(mac arm64，ICU 78.3）

| 阶段 | 产物 | 大小 |
|---|---|---|
| 原始 | TN.so + 全量 ICU 三库 | ~36 MB |
| 甲方手动(只留 5 语种) | 动态库 | ~16 MB |
| 杠杆① 数据裁剪 | 三动态库(data 打进库) | 7.23 MB（data 1.93 MB） |
| **①+② 静态可执行(死代码消除+strip)** | **单一静态产物(ICU 码+数据+TN+morphodita)** | **≈ 4.43 MB** |
| **①+② 自包含 `.so`(交付形态)** | **单个动态库(内部静态嵌入以上全部,仅导出 TN C-API)** | **≈ 5.19 MB** |

> 注:两个数字都已含 morphodita 库代码(~0.5 MB，TN 自身依赖)。俄语 22 MB `.tagger` 模型不在此列、仅俄语部署携带。
> `.so` 比静态可执行略大,是动态库固有开销(导出表/重定位/头),属正常。
> 数据部分体积与目标架构无关(同样 1.93 MB)；代码部分量级一致。

## 自包含 `.so` 交付(C-API + 构建配方)

把裁剪后的 ICU(数据+代码)、TN、morphodita 全部**静态嵌入一个动态库**,对外只暴露一个 C-API。
好处:①单文件交付、无外部 ICU 依赖;②ICU 符号隐藏后死代码消除才能生效;③`extern "C"` 规避 C++ ABI 跨编译器问题。

### C-API 样例(`tts_capi.cpp`)
```cpp
#include "text_normalizer.hpp"
#include <string>
#define TTS_API __attribute__((visibility("default")))   // 唯一导出根;其余(含 ICU)保持 hidden
extern "C" {
TTS_API void* tts_init(const char* dir) {                 // dir = data/<locale>
    auto* n = new TextNormalizer();
    if (!n->Init(dir)) { delete n; return nullptr; }
    return n;
}
TTS_API const char* tts_normalize(void* h, const char* line) {
    static thread_local std::string b;                    // 每实例/每线程各持一个句柄即可
    b = static_cast<TextNormalizer*>(h)->normalizeLine(line);
    return b.c_str();
}
TTS_API void tts_free(void* h) { delete static_cast<TextNormalizer*>(h); }
}
```

### 链接成 `.so`
```bash
# 关键:-fvisibility=hidden 让除 TTS_API 外的符号(含 ICU)全隐藏,-dead_strip/--gc-sections 才能剥离未用 ICU 代码。
g++ -std=c++17 -O2 -dynamiclib -fvisibility=hidden -ffunction-sections -fdata-sections \
  tts_capi.cpp \
  unified/text_normalizer.cpp unified/backend.cpp \
  unified/icu_backend.cpp unified/english_backend.cpp unified/arabic_backend.cpp \
  unified/chinese_backend.cpp unified/bengali_backend.cpp unified/russian_backend.cpp \
  tts_normalizer_engine.cpp ru_year_spellout.cpp \
  original/morphodita/src_lib_only/morphodita.cpp \
  -I. -Ithird_party -Iunified -Ioriginal/morphodita/src_lib_only \
  -I<icu>/source/common -I<icu>/source/i18n \
  <icu-static>/lib/libicui18n.a <icu-static>/lib/libicuuc.a <icu-static>/lib/libicudata.a \
  -lpthread -Wl,-dead_strip \
  -o libtts_normalizer.dylib          # Linux/Android: -shared 且 -Wl,--gc-sections,产物 libtts_normalizer.so
strip -x libtts_normalizer.dylib      # Linux: strip --strip-unneeded
```
- ICU 静态库来自「构建配方」里的 `--enable-static --disable-shared --with-data-packaging=static` 构建。
- 验证只导出了 3 个符号:`nm -gU libtts_normalizer.dylib | grep tts_` → `_tts_init/_tts_normalize/_tts_free`。

> 已实测:该 `.so` = **5.19 MB**,通过 dlopen 调用 C-API 跑完整 13 项回归,**逐字节一致、零回退**。

## 回归验证(零回退）

每一轮裁剪都跑 [`test/scripts/verify_icu_trim.sh`](../../test/scripts/verify_icu_trim.sh)，与提交在
`test/baseline_pretrim/` 的**裁剪前基线**逐字节比对：

- 语料：transsion 1000 句 × 5(中英阿俄孟)+ transsion_cmos × 3(中英俄)+ 冒烟 × 5，共 13 项。
- 结果：数据裁剪版、最终静态裁剪版均 **13/13 逐字节一致**。

```bash
TTS_CLI=/path/to/tts_cli_trimmed \
DYLD_LIBRARY_PATH=/path/to/trimmed/icu/lib \
TRANSSION_TESTSET_DIR=<...>/transsion_testset \
bash test/scripts/verify_icu_trim.sh
```

**经验教训**:初版曾误删 currency，导致 RBNF 读法初始化 `U_MISSING_RESOURCE_ERROR`、输出为空——被回归当场抓出。裁剪必须每步过回归,不能只看体积。

## 可继续下探(如需逼近 3 MB）
- 删 `unames.icu`(字符名，~0.33 MB，TN 不用 `\N{...}`)。
- 精简保留的 normalization 变体(nfkc_cf/nfkc_scf 等)。
- 收益递减,做前务必再过一遍回归。
