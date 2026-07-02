# Transsion 测试集运行指南

> 本仓库（多语言 TTS 文本归一化引擎）跑 transsion 测试集的完整流程。
> 假设你已经把测试数据 `<locale>_1000_sample_sent.txt` 准备好。

---

## 一、前置依赖

| 依赖 | macOS 安装 | Linux 安装 |
|---|---|---|
| C++17 编译器 | 系统自带 / `xcode-select --install` | `apt install build-essential` |
| ICU4C ≥ 70 | `brew install icu4c` | `apt install libicu-dev` |
| Python 3.9+（仅在重新生成规则 JSON 时需要） | 系统自带 | 系统自带 |

可选：Russian locale 需要 MorphoDiTa 词形分析器；只跑 en/zh/ar/bn 可忽略。

---

## 二、仓库目录结构

```text
repo-root/
├── tools/                                  # 规则生成器（Python）
├── rules/                                  # 各 locale 的 JSON 规则
├── *.cpp / *.hpp                           # C++ 引擎 + 5 个 locale driver
└── test/
    ├── scripts/build.sh                    # 编译 binary
    ├── scripts/run_transsion_testset.sh    # 跑测试集
    ├── bin/                                # 编译产物（首次运行自动生成）
    ├── transsion_testset/                  # ← 你自己的测试集放这里
    └── transsion_runs/<RUN_TAG>/           # 每次运行的输出
```

---

## 三、三步跑测

### 步骤 1：克隆仓库并进入

```bash
git clone <repo-url> tts-norm
cd tts-norm
```

### 步骤 2：编译 binary

```bash
bash test/scripts/build.sh
```

- 脚本会自动查找 `/opt/homebrew/opt/icu4c` 或 `/usr/local/opt/icu4c`。
- 如果你的 ICU 装在别处：
  ```bash
  ICU_ROOT=/your/icu/prefix bash test/scripts/build.sh
  ```
- 编译成功后 `test/bin/` 下会有 `en_tts`、`zh_tts`、`ar_tts`、`bn_tts`、`ru_tts` 五个可执行文件。

### 步骤 3：跑测试集

**方式 A（推荐）：测试集放进仓库的 `test/transsion_testset/`，文件名按 `<locale>_1000_sample_sent.txt` 命名：**

```bash
mkdir -p test/transsion_testset
cp /path/to/your/en_1000_sample_sent.txt test/transsion_testset/
cp /path/to/your/zh_1000_sample_sent.txt test/transsion_testset/
# ... 其他 locale 同理

bash test/scripts/run_transsion_testset.sh
```

**方式 B：测试集放在仓库外，用环境变量指过去：**

```bash
TRANS_TESTSET_DIR=/path/to/your/testset \
RUN_TAG=trial_v1 \
bash test/scripts/run_transsion_testset.sh
```

---

## 四、输出位置

每次运行会创建一个时间戳目录（或你给的 `RUN_TAG`）：

```text
test/transsion_runs/<RUN_TAG>/
├── out/
│   ├── en.out.txt    # 1000 行英文规范化结果（与输入行一一对应）
│   ├── zh.out.txt
│   ├── ar.out.txt
│   ├── bn.out.txt
│   └── ru.out.txt
└── report/
    ├── summary.csv   # 机读：每 locale 的 input/output 行数、状态
    └── summary.txt   # 人读汇总
```

另外 `test/transsion_runs/latest` 会被 symlink 到本次运行目录，方便最近一次结果的快速访问。

成功跑完时屏幕大致如下：

```text
OK ar: in=1000, out=1000, empty_out=0
OK bn: in=1000, out=1000, empty_out=0
OK en: in=1000, out=1000, empty_out=0
OK zh: in=1000, out=1000, empty_out=0
```

`status=OK` 表示输入输出行数完全一致；`WARN line_count_mismatch` 说明某些输入行被规则吞掉（应检查相应 locale 的规则）。

---

## 五、常用环境变量

| 变量 | 作用 | 默认 |
|---|---|---|
| `ICU_ROOT` | ICU4C 安装根目录（含 `include/unicode/`） | 自动探测 brew 路径 |
| `TRANS_TESTSET_DIR` | 测试集目录 | `test/transsion_testset` |
| `TRANS_OUT_DIR` | 输出根目录 | `test/transsion_runs` |
| `RUN_TAG` | 本次运行标签 | `YYYYMMDD_HHMMSS` |
| `RU_MORPH_MODEL` | Russian MorphoDiTa 模型路径 | `original/morphodita/models/russian-syntagrus-morphodita-only.tagger` |

---

## 六、单条调试（不跑 1000 行）

`*_tts` 直接读 stdin，写 stdout，一行一个句子：

```bash
echo 'Upgraded plans start at $0.99/month.' | test/bin/en_tts
# →  Upgraded plans start at  zero point nine nine dollars per month.

echo '今天气温是 25 摄氏度。' | test/bin/zh_tts
```

也可以批量喂任意文件：

```bash
test/bin/en_tts < my_inputs.txt > my_outputs.txt
```

---

## 七、改了规则之后

```bash
bash test/scripts/build.sh                    # 改了 *.cpp 才需要
bash test/scripts/run_transsion_testset.sh    # 跑测试
```

只手改了 `rules_v2/*.full.json` 或不改任何东西，可以跳过 build 直接跑测试。

---

## 八、常见问题

- **`Set ICU_ROOT to your icu4c prefix`**：未装 ICU 或装在非常规位置。先 `brew install icu4c`，或显式传 `ICU_ROOT=`。
- **`Missing testset dir`**：`test/transsion_testset/` 不存在且未传 `TRANS_TESTSET_DIR`。按步骤 3 任一方式提供测试集。
- **`Failed to load TTS rules`**：`test/rules` 符号链接缺失。在仓库根目录执行 `ln -snf ../rules test/rules` 修复。
- **ru locale 报 missing executable**：MorphoDiTa 未初始化；只需要 ru 时再处理，否则忽略即可。
