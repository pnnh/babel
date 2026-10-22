# Contract: `LlmEngine::generate()` 结构化标注文本格式

**Spec**: [spec.md](../spec.md)（FR-007） ｜ **Data Model**: [data-model.md](../data-model.md) §1 ｜ **Research**: [research.md](../research.md) §5.3

## 适用范围

本契约定义 `babel::slowpath::LlmEngine::generate(const QString &normalizedInput)` **输入参数内容**的结构化标注格式。**签名本身不变**（仍为单一 `QString` 参数，符合 spec FR-007"MUST 延续字符串类型"的硬性约束），本契约仅约束该字符串**内部**如何编码各拼音位置的多候选集合与权重信息。

本契约不约束 `generate()` 的**输出**（仍为 `QString`，即标准中文译写结果纯文本，不要求、也不期望模型在输出中回显任何标注符号）。

## 输入格式定义

### 总体结构

输入字符串由"普通文本片段"与"候选标注片段"交替拼接而成，按 `PinyinLattice` 的格位顺序依次排列：

- 非歧义格位（`candidates.length == 1`，含已是中文/标点的透传格位）：直接输出 `candidates[0].surfaceForm` 原文，不附加任何标记符号。
- 歧义格位（`candidates.length >= 2`）：输出下述"候选标注片段"语法。

### 候选标注片段语法

```
⟦<originalToken>:<候选1> <权重1>|<候选2> <权重2>|...|<候选N> <权重N>⟧
```

- 定界符：`⟦`（U+27E6 MATHEMATICAL LEFT WHITE SQUARE BRACKET）开始，`⟧`（U+27E7）结束；候选之间以半角 `|` 分隔；每个候选内部，候选词形与权重之间以单个半角空格分隔。
- `<originalToken>`：该格位 `PinyinLatticeCell.originalToken`（通常为拼音片段，如 `chi`），帮助模型理解候选的共同读音来源，不要求唯一可逆解析。
- `<候选i>`：`CandidateEntry.surfaceForm`，按 `rawWeight` 降序排列（与格位内部排序一致，不重新排序）。
- `<权重i>`：`CandidateEntry.normalizedWeight`，保留两位小数，格位内全部权重之和应近似为 `1.00`（四舍五入可能导致末位候选的求和误差，属可接受精度损失，不要求引擎额外做误差补偿）。

### 示例

原始击键：`wo chi le wanfan`（假设 `chi` 在词库中查得候选"吃(0.42)/持(0.35)/迟(0.15)/池(0.05)/尺(0.02)/齿(0.01)"，其余 token 均无歧义）：

```
我⟦chi:吃 0.42|持 0.35|迟 0.15|池 0.05|尺 0.02|齿 0.01⟧了晚饭
```

## 配套系统提示词约束

`LlmEngine.cpp` 内部固定系统提示词（`kSystemPrompt`）MUST 补充说明本标注语法的含义，使模型将其理解为"候选消歧线索"而非待逐字复述的文本；输出中 MUST NOT 包含 `⟦`/`⟧`/`|` 等标注符号本身（延续既有"只输出改写后的标准中文句子"约束）。系统提示词具体措辞属内部实现细节（宪章原则 IV），不在本契约中逐字固定，但 MUST 至少说明：

1. `⟦⟧` 内列出的是"同一位置的候选汉字及其可信度数值（越大越可信）"；
2. 模型 MUST 结合上下文从候选中选择语义最合适的一项，不要求必须选择可信度最高者；
3. 最终输出 MUST 为不含任何标注符号的纯中文句子。

## 本契约已回填的事项（`research.md` §12 用户委派）

- 每个歧义格位候选数量的规模上限（`research.md` §5.4，已决策：固定 Top-K 截断，K = 9，实测前起点；**007-candidate-manual-correction FR-010 已将该值由 8 调整为 9**，以与浮窗候选列表数字键 1–9 的编号上限对齐，详见该特性 `research.md` §8）：候选列表的截断发生在 `PinyinLattice` 构建阶段（`CandidateEntry[]` 本身已被截断至最多 9 项，`normalizedWeight` 基于截断后子集重新计算），**不改变本契约定义的标注语法本身**，仅改变参与渲染的候选数量上界；示例中列出的候选数（如 6 个）均未超出该上限，无需调整示例文本本身。
- 本契约格式**仅是 `/speckit.plan` 阶段的设计提案**，最终实现时若通过单元测试发现 Qwen2.5-Instruct 对该语法的消歧效果不佳，`/speckit.implement` 阶段 MAY 在不改变"`generate()` 输入仍为字符串"这一 FR-007 硬性约束的前提下调整具体标注符号/排版（该调整空间已由 spec FR-007 原文"具体标记符号/排版细节…留待 `/speckit.plan` 设计阶段确定"授权至设计阶段，微调不构成新的外部契约变更，只需同步更新本文件）。
