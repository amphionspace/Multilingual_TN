# libtts_normalizer 使用说明

分支:`chore/icu-size-reduction`

> ⚠️ 若之前 clone 过这条分支,请先重新拉取(分支已更新到最新代码):
> ```bash
> git fetch origin && git reset --hard origin/chore/icu-size-reduction
> ```

---

## 一、编译(一键)

```bash
bash test/scripts/build_tts_so.sh
```

产出两个东西:

| 产物 | 说明 |
|---|---|
| **`build/libtts_normalizer.so`**(约 4.15 MB) | 交付物。单文件自包含:裁剪后的 ICU(数据+代码)、归一化模块、俄语形态库全部静态嵌入,**不依赖外部 libicu**,只导出 4 个 C 接口 |
| `build/tts_cli_so` | 命令行驱动,链接上面的库、只用公开 C API。**开箱即可试跑,也用于验证**;同时是最小集成范例([`unified/tts_cli_so.cpp`](unified/tts_cli_so.cpp)) |

编完直接试:
```bash
echo 'March 15, 2024' | ./build/tts_cli_so --data ./data/en
# ->  March fifteenth, two thousand twenty-four
```

脚本自动完成:拉取并缓存钉死版 ICU 源码 → 按 `filter.json` 编裁剪版 ICU → 链接 `.so`(隐藏符号 + 死代码消除 + strip)→ 打印体积和导出符号。

常用环境变量:

| 变量 | 说明 |
|---|---|
| `ICU_SRC` | 已有的 ICU 源码目录(离线 / 交叉编译用) |
| `FILTER` | 数据过滤配置(默认 `docs/icu_size_reduction/filter.json`) |
| `OUT` | 输出路径 |
| `BUILD_DIR` | 构建缓存目录(默认 `build/`) |
| `JOBS` | 并发编译数 |

---

## 二、调用(接口见 [`unified/tts_capi.h`](unified/tts_capi.h))

```c
#include "tts_capi.h"

tts_normalizer_t* h = tts_init("data/ru");        // 按资源目录选语种,失败返回 NULL
const char* out = tts_normalize(h, line_utf8);    // 归一化一行(UTF-8)
tts_free(h);
```

- 语种由**资源目录**决定:`data/ru`、`data/en`、`data/zh`、`data/ar`、`data/bn`。
- **句柄非线程安全**:每线程建一个句柄。
- `tts_normalize` 返回的指针由库持有,在**同一线程下次调用前**有效;需要保留请自行拷贝。
- `tts_init` 返回 NULL 时,用 `tts_last_error()` 取原因。

---

## 三、加语种(不用改代码)

1. 编辑 [`docs/icu_size_reduction/filter.json`](docs/icu_size_reduction/filter.json) → `localeFilter.includelist` 加入语种(如 `"es"`);
2. 新增 `data/es/`(`config.json` + 规则文件);
3. 重跑 `build_tts_so.sh`。

ICU 构建按 filter.json 的**内容哈希缓存**:改了自动重编,没改秒过。

> 说明:ICU 数字读法能覆盖的常规语种,只需加数据、无需写代码;若某语种 ICU 无读法数据或需复杂形态(如俄语),才需要新增一个独立后端文件。

---

## 四、验证

验证脚本只**检查**已编好的二进制,自己不编译,所以要把上一步产出的驱动指给它:

```bash
TTS_CLI=build/tts_cli_so bash test/scripts/verify_icu_trim.sh
```

1000 句语料在仓库外,如需纳入再加:`TRANSSION_TESTSET_DIR=<语料目录>`(不加则跳过该组)。

当前状态:`.so` 输出与未裁剪版**逐字节一致**(1000 句 × 5 语言 + cmos × 3),**13/13 全绿**。

---

## 五、交叉编译(Android / 嵌入式)

`ICU_SRC` 指向你们的 ICU 源码树;ICU 需先 host 构建,再用 `--host=... --with-cross-build=<host构建目录>` 构建目标平台。
**过滤配置与链接步骤不变**,数据部分体积与架构无关。

---

## 相关文档

- [`docs/icu_size_reduction/README.md`](docs/icu_size_reduction/README.md) —— 裁剪原理、保留/删除清单、实测数据、为什么不影响日期/度量/货币
- [`docs/icu_size_reduction/filter.json`](docs/icu_size_reduction/filter.json) —— 数据过滤配置(加语种就改这里)
