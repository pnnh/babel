# Contract: 候选选择与替换生命周期

**Spec**: [spec.md](../spec.md)（FR-004/006/007/011/013/015/019/020/021）｜ **Research**: [research.md](../research.md) §2/§6 ｜ **Data Model**: [data-model.md](../data-model.md) 二/七

## 适用范围

本契约定义"用户按下数字键选中候选"这一事件从发生到最终反映到原始输入槽文本的完整行为规则，以及候选列表展示/隐藏的生命周期边界。不重复定义候选生成/排序本身（见 `fastpath-candidate-generation-contract.md`）或 QML↔C++ 方法签名（见 `qml-cpp-session-interface-increment.md`）。

## 一、替换算法（FR-006/FR-020）

给定当前原始输入槽文本 `text`、光标锚定词 cell（`tokenStart`/`tokenEnd`）、用户选中候选 `chosenText`：

```text
needsSeparator = tokenEnd >= text.length() || !isWordBoundaryDelimiter(text.at(tokenEnd))
newText        = text.left(tokenStart) + chosenText + (needsSeparator ? " " : "") + text.mid(tokenEnd)
newCursorPos   = tokenStart + chosenText.length() + (needsSeparator ? 1 : 0)
```

- **MUST**：替换**MUST NOT** 引入"格位索引 + 用户覆盖状态"一类附加状态机制；`newText` 写回后与用户手动键入的文本完全等价对待（FR-006 字面约束），后续任何编辑（含对 `chosenText` 本身的再次编辑）均按既有规则重新分词/匹配，不保留"此处曾被手动选中过"的任何标记。
- **MUST**：`needsSeparator` 为真时，追加的分隔符 **MUST** 为半角空格（FR-020"如空格"）；为假时（替换位置紧邻的后一个字符已经是分隔符）**MUST NOT** 重复插入。
- **MUST**：替换完成后 **MUST** 基于 `newText`（而非原 `text`）重新执行完整的 `buildLattice()` 流程（FR-007），候选列表与标准中文输出预览随之刷新。

## 二、候选列表展示生命周期（FR-013/FR-015/FR-019/FR-021）

- **MUST**：只要存在光标锚定的当前词（`currentTokenIndex != -1`）且浮窗主视图可见，候选列表 **MUST** 展示，即便候选集合只有 1 项（编号固定为"1"），**MUST NOT** 因候选数量变化而整体隐藏/显示该区域本身（区域的"隐藏"仅发生于"无当前词"或"浮窗/主视图不可见"两种情形，见下）。
- **MUST NOT**：浮窗整体隐藏（`FloatingSessionState::Hidden`）、或设置页面处于前台（`settingsVisible == true`）、或当前无锚定词（空输入/光标落在空白或标点处）时，候选列表 **MUST NOT** 展示（FR-019，`research.md` §6 已论证该三种情形天然覆盖"非输入/编辑进行中状态"，无需新增独立空闲超时定时器）。
- **MUST**：用户在候选列表展示期间直接按回车/空格确认上屏而不按任何数字键时，系统 **MUST** 按既有默认整句自动消歧结果处理（FR-015/FR-021），**MUST NOT** 自动确认候选列表第 1 项并写回原始输入槽；原始输入槽文本 **MUST** 保持用户实际键入的原始内容不变。

## 三、强约束汇总

- **MUST NOT** 要求用户必须完成候选选择才能继续输入或确认上屏（FR-015，默认整句自动消歧工作方式必须与交付前完全一致）。
- **MUST NOT** 记录/学习用户历史候选选择倾向（spec Out of Scope"用户个性化候选学习/记忆"）；每次 `selectCandidate()` 调用均为无状态的一次性文本替换操作。
- **MUST**：`selectCandidate(number)` 的 `number` 校验、替换计算、`buildLattice()` 重算三者 **MUST** 在同一次用户按键引发的调用链（`Keys.onPressed → selectCandidate() → candidateReplacementReady → submitKeystrokes()`）内同步完成，不引入异步/延迟确认机制（FR-016"不依赖慢流/语义推理计算候选排序"的同一"跟手"精神延伸至替换本身）。

## 四、测试契约（供 `/speckit.tasks` 参考）

- 可对 `SessionController` 直接单元测试（`Q_INVOKABLE` 公开方法）：注入已知 `rawInput`/`cursorPosition` 组合，调用 `selectCandidate(number)`，断言 `candidateReplacementReady` 发出的 `(newText, newCursorPosition)` 符合本契约一、算法公式；覆盖"替换位置后紧邻分隔符（不重复插入）"与"替换位置后紧邻非分隔符（插入空格）"两类场景（对应 SC-007 的核心断言）。
- `number` 越界（如候选只有 3 项却传入 5）/当前无锚定词时调用 `selectCandidate()`：断言为 no-op（不崩溃、不发出信号、`rawDraft` 不变）。
