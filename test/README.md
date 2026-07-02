# TTS Normalizer Locale Harness / 多语言测试工具

## English

This folder holds **sample inputs**, **baseline outputs**, **compiled drivers** (`bin/`), and scripts to exercise `en` / `zh` / `ar` / `bn` / `ru` without touching product code.

### Layout

| Path | Purpose |
|------|---------|
| `bin/*_tts` | Per-locale normalizer, rebuilt with `scripts/build.sh` |
| `rules` | Symlink to `../rules` so binaries resolve `rules/<locale>.json` when the current directory is `test/` |
| `in/<locale>.txt` | One sentence per line, read from stdin |
| `out/<locale>.out` | Latest generated output, safe to delete |
| `expected/<locale>.golden` | Human-reviewed baseline; `scripts/run_all.sh` diffs against it |
| `scripts/build.sh` | Compiles locale drivers into `bin/`; requires ICU, and Russian also builds/links MorphoDiTa |
| `scripts/run_all.sh` | Runs all locales, writes `out/`, and compares with `expected/` |
| `scripts/run_transsion_testset.sh` | Runs `transsion_testset/*_1000_sample_sent.txt` and writes timestamped outputs plus summary reports |

### Quick Start

```bash
cd ICU/test
./scripts/run_all.sh
```

Rebuild after C++ or JSON changes:

```bash
cd ICU/test
./scripts/build.sh
./scripts/run_all.sh
```

Run the client testset:

```bash
cd ICU/test
./scripts/run_transsion_testset.sh
```

Useful overrides:

```bash
# custom model path for Russian
RU_MORPH_MODEL=/path/to/model.tagger ./scripts/run_transsion_testset.sh

# custom output root / run tag
TRANS_OUT_DIR=./transsion_runs RUN_TAG=trial_v2 ./scripts/run_transsion_testset.sh
```

Refresh baselines after reviewing intentional output changes:

```bash
UPDATE_LOCALES=ru CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF ./scripts/run_all.sh
```

Update multiple locales:

```bash
UPDATE_LOCALES="zh,ru" CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF ./scripts/run_all.sh
```

Non-interactive update for CI-like use:

```bash
UPDATE_LOCALES=ru CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF AUTO_APPROVE_UPDATE=1 ./scripts/run_all.sh
```

### Requirements

- **ICU**, for example `brew install icu4c`. `scripts/build.sh` checks `/opt/homebrew/opt/icu4c` and `/usr/local/opt/icu4c`.
- **Rules JSON** under `../rules_v2/` (hand-maintained; no generator step).
- **Russian morph model** for `ru_tts`: defaults to `../original/morphodita/models/russian-syntagrus-morphodita-only.tagger`, or override with `RU_MORPH_MODEL=/path/to/model.tagger`.
- **Russian MorphoDiTa source** (`morphodita.h/.cpp`) is auto-bootstrapped by `scripts/build.sh`. You can also run `./scripts/prepare_morphodita.sh` manually in restricted environments.

### Notes

- Drivers resolve `rules/` using `__FILE__` at compile time. With the usual relative build path, the working directory should be `ICU/test/`; `run_all.sh` handles this automatically.
- Golden updates are guarded. The old `UPDATE_GOLDEN=1` flow is rejected to avoid accidental full overwrites.
- `UPDATE_LOCALES` only updates explicitly named locales; all others remain strict diff checks.
- `CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF` is required before overwriting any golden.
- In interactive terminals, each locale update asks for approval. For automation, set `AUTO_APPROVE_UPDATE=1` explicitly.
- `run_transsion_testset.sh` skips unsupported locales such as `es` and records the reason in `transsion_runs/<run_tag>/report/summary.csv`.

## 中文

这个目录用于存放 **样例输入**、**人工基准输出**、**编译后的 driver**（`bin/`）以及测试脚本。它可以在不改产品代码的情况下，快速验证 `en` / `zh` / `ar` / `bn` / `ru` 这几种语言的文本归一化效果。

### 目录结构

| 路径 | 作用 |
|------|------|
| `bin/*_tts` | 各语言 normalizer 可执行文件，由 `scripts/build.sh` 重新构建 |
| `rules` | 指向 `../rules` 的 symlink；当当前工作目录是 `test/` 时，二进制可以找到 `rules/<locale>.json` |
| `in/<locale>.txt` | 每行一句输入文本，程序从 stdin 逐行读取 |
| `out/<locale>.out` | 最近一次运行生成的输出，可删除，会重新生成 |
| `expected/<locale>.golden` | 人工审核过的基准输出，`scripts/run_all.sh` 会拿实际输出和它做 diff |
| `scripts/build.sh` | 编译各语言 driver 到 `bin/`；需要 ICU，俄语还需要编译/链接 MorphoDiTa |
| `scripts/run_all.sh` | 跑所有语言，写入 `out/`，并与 `expected/` 对比 |
| `scripts/run_transsion_testset.sh` | 一键跑 `transsion_testset/*_1000_sample_sent.txt`，生成带时间戳的输出和汇总报告 |

### 快速开始

```bash
cd ICU/test
./scripts/run_all.sh
```

C++ 或 JSON 规则修改后，先重新编译再跑测试：

```bash
cd ICU/test
./scripts/build.sh
./scripts/run_all.sh
```

一键跑客户端测试集：

```bash
cd ICU/test
./scripts/run_transsion_testset.sh
```

常用覆盖参数：

```bash
# 指定俄语 MorphoDiTa 模型路径
RU_MORPH_MODEL=/path/to/model.tagger ./scripts/run_transsion_testset.sh

# 指定输出根目录和 run tag
TRANS_OUT_DIR=./transsion_runs RUN_TAG=trial_v2 ./scripts/run_transsion_testset.sh
```

当输出变化是预期行为，并且已经人工审核 diff 后，可以刷新 golden：

```bash
UPDATE_LOCALES=ru CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF ./scripts/run_all.sh
```

刷新多个语言：

```bash
UPDATE_LOCALES="zh,ru" CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF ./scripts/run_all.sh
```

非交互式刷新，例如 CI 或脚本场景：

```bash
UPDATE_LOCALES=ru CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF AUTO_APPROVE_UPDATE=1 ./scripts/run_all.sh
```

### 依赖要求

- **ICU**：例如 macOS 上执行 `brew install icu4c`。`scripts/build.sh` 会优先查找 `/opt/homebrew/opt/icu4c` 和 `/usr/local/opt/icu4c`。
- **Rules JSON**：必须存在于 `../rules_v2/`（手工维护，无生成器环节）。
- **俄语形态模型**：`ru_tts` 默认使用 `../original/morphodita/models/russian-syntagrus-morphodita-only.tagger`，也可以通过 `RU_MORPH_MODEL=/path/to/model.tagger` 覆盖。
- **俄语 MorphoDiTa 代码**（`morphodita.h/.cpp`）会由 `scripts/build.sh` 自动准备；在受限环境下也可手动执行 `./scripts/prepare_morphodita.sh`。

### 注意事项

- Driver 在编译时通过 `__FILE__` 解析 `rules/` 路径；使用当前相对路径编译时，运行目录需要是 `ICU/test/`。`run_all.sh` 会自动处理。
- Golden 更新流程有保护机制：旧的 `UPDATE_GOLDEN=1` 会被拒绝，避免误把所有基准覆盖掉。
- `UPDATE_LOCALES` 只会更新明确列出的语言，其它语言仍然执行严格 diff。
- 覆盖 golden 前必须设置 `CONFIRM_GOLDEN_UPDATE=I_HAVE_REVIEWED_DIFF`。
- 交互式终端会逐语言询问是否确认更新；自动化场景必须显式设置 `AUTO_APPROVE_UPDATE=1`。
- `run_transsion_testset.sh` 会跳过当前二进制不支持的语言，例如 `es`，并把原因写入 `transsion_runs/<run_tag>/report/summary.csv`。
