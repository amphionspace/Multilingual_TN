# MorphoDiTa `.tagger` 模型说明（俄语 TN 交付参考）

本文档说明俄语 `ru_tts` 运行时使用的 MorphoDiTa 模型文件格式，并与常见的 **PyTorch `.pt`**、**FST** 做对照，便于甲方/运维区分「形态标注模型」与「神经网络权重」。

**仓库内默认模型路径：**

`original/morphodita/models/russian-syntagrus-morphodita-only.tagger`

**运行时指定：**

```bash
./ru_tts --morph-model /path/to/russian-syntagrus-morphodita-only.tagger
# 或环境变量：RU_MORPH_MODEL=...
```

---

## 1. 一句话结论

| 问题 | 答案 |
|------|------|
| `.tagger` 是神经网络吗？ | **否** |
| `.tagger` 是 FST 吗？ | **否** |
| `.tagger` 是什么？ | **MorphoDiTa 专有二进制包**：形态词典 + 前缀猜测器 + 平均感知机标注器 + Viterbi 解码 |
| C++ 能否直接推理？ | **能**，`ru_tts` 已链接 `morphodita.cpp`，`tagger::load()` 加载 |

---

## 2. `.tagger` 内部结构（概念）

`.tagger` 文件由 UFAL MorphoDiTa 工具链序列化，推理时大致流程：

```
输入文本
  → Tokenizer 分词
  → 形态词典查找（form → 多个 lemma/tag 候选）
  → 前缀 Guesser（词典未命中时规则补全）
  → 感知机特征打分 + Viterbi 序列消歧
  → 每 token 输出 lemma + 形态标签（本项目解析 Case/Gender/Number）
  → 供 ru_num_morph 决定数字变格读法
```

**主要内部组件（来自 MorphoDiTa `perceptron_tagger` 实现）：**

| 组件 | 类型 | 作用 |
|------|------|------|
| `morpho_dictionary` | 二进制哈希词典 | 词形 → lemma + 形态分析候选 |
| `morpho_prefix_guesser` | 规则猜测器 | 未登录词形的形态猜测 |
| `feature_sequences` + 权重表 | 线性模型特征与权重 | 上下文特征模板 + 训练得到的权重 |
| `viterbi` | 动态规划解码 | 在候选标签序列中选全局较优标注 |

---

## 3. 对照表：`.tagger` vs `.pt` vs FST

| 维度 | **MorphoDiTa `.tagger`** | **PyTorch `.pt`** | **FST / WFST** |
|------|--------------------------|-------------------|----------------|
| **格式性质** | UFAL 专有二进制模型包 | PyTorch 序列化权重/计算图 | 有限状态机/转导器（如 OpenFST） |
| **典型内部内容** | 形态词典 + 感知机权重 + 特征模板 + Viterbi 解码器 | 张量参数、层结构（Linear、Attention 等） | 状态、转移、输入/输出符号表 |
| **模型范式** | 词典 lookup + 线性分类（averaged perceptron）+ 结构化解码 | 深度学习（前向神经网络） | 符号自动机组合（compose / shortest path） |
| **是否神经网络** | **否** | **是**（通常） | **否** |
| **是否 FST** | **否** | **否** | **是** |
| **训练数据** | 标注语料（如俄语 Syntagrus） | 大规模语料 + 反向传播 | 词典/规则或从语料诱导 |
| **推理计算** | CPU 查表 + 特征打分 + Viterbi | 矩阵运算（可 GPU） | 图遍历 / 最短路径 |
| **典型体积** | 约数十 MB（本仓库俄语模型 ~22MB） | 数百 MB～数 GB 常见 | 视词典/规则规模而定 |
| **运行时依赖** | MorphoDiTa C++ 库（本仓库 vendored） | libtorch / Python + CUDA 等 | OpenFST、Thrax 等 FST 工具链 |
| **在本项目中的角色** | **俄语数字变格**：提供格/性/数上下文 | **未用于 TN** | **未用于 TN** |
| **与本项目 TN 的关系** | `ru_tts` 唯一外部形态模型 | 无 | 无 |

---

## 4. 与本仓库其他组件的边界

```
┌─────────────────────────────────────────────────────────────┐
│  ru_tts（C++ 可执行文件）                                      │
├─────────────────────────────────────────────────────────────┤
│  rules_v2/ru.full.json  ──►  TtsNormalizerEngine（规则 TN） │
│  ICU RuleBasedNumberFormat  ──►  基数/序数拼读                 │
│  *.tagger (MorphoDiTa)      ──►  句法形态特征（变格用）         │
└─────────────────────────────────────────────────────────────┘
```

- **TN 规则 JSON**：文本归一化主逻辑（v2 由语言学家直接维护，见 `rules_v2_draft.md`）。
- **ICU**：数字读法引擎。
- **`.tagger`**：仅辅助俄语「数字与上下文名词一致」的形态分析，**不是** TTS 声学模型，**不是**端到端神经网络。

---

## 5. 常见问题

**Q: 能否把 `.tagger` 转成 `.pt` 或 ONNX？**  
A: 不建议也无必要。二者范式不同；本项目 C++ 已原生加载 `.tagger`。

**Q: 部署 ru 是否必须装 Python？**  
A: **不必**。只需 `ru_tts` 二进制、规则 JSON、ICU 运行时库、以及 `.tagger` 文件路径。

**Q: `morphodita-only` 是什么意思？**  
A: 词典与 tagger 打在同一 `.tagger` 文件中；相对单独的 `.dict` + tagger 组合包。

**Q: 换模型是否影响 TN 规则？**  
A: 规则 JSON 不变；变格读法可能随形态标注质量略有差异。换模型后应跑俄语回归（见 `docs/transsion_testset_guide.md`）。

---

## 6. 参考

| 资源 | 说明 |
|------|------|
| [MorphoDiTa（UFAL）](https://ufal.mff.cuni.cz/morphodita) | 工具与模型官方说明 |
| [MorphoDiTa GitHub / MANUAL](https://github.com/ufal/morphodita) | 模型格式与 `run_tagger` 用法 |
| `original/morphodita/src_lib_only/README.md` | 本仓库 vendored 库说明 |
| `ru.cpp` | `tagger::load` 与 `analyzeMorph()` 集成点 |
| `docs/tts_ops.md` | `ru_num_morph` 等 exec op 说明 |

---

*文档版本：与仓库 `ru_tts` + `russian-syntagrus-morphodita-only.tagger` 配套；若升级 MorphoDiTa 或更换俄语模型，请同步更新本节并回归测试。*
