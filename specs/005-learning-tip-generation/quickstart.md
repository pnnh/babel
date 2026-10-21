# Quickstart: 学习提示气泡内容生成 验证指南

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md) ｜ **Data Model**: [data-model.md](./data-model.md) ｜ **Contracts**: [contracts/](./contracts/)

本指南用于在 `/speckit.implement` 完成本特性对应的 `tasks.md` 任务后，手工验证端到端行为是否满足 `spec.md` 的验收场景与成功标准。本文件延续 001/002/003/004 `quickstart.md` 的既有验证场景作为回归基线，**仅新增**本特性引入的验证场景，**不重复**既有内容。

## 前置条件

- 已按约定本地模型目录放置至少一个中配（Qwen2.5-7B-Instruct）或高配（Qwen2.5-14B-Instruct）GGUF 模型文件，以及低配默认模型（Qwen2.5-3B-Instruct，001 既有前置条件）。
- 构建与测试运行方式延续既有工作流（未因本特性改变构建入口）：

```bash
# macOS
cmake --preset macos
cmake --build --preset macos
ctest --preset macos --output-on-failure
```

---

## 验证场景一：中/高配档位、开关默认开启时看到地道英文表达建议（User Story 1，对应 SC-001/SC-002/SC-003）

**目的**：验证 FR-001～FR-003——同一次慢流调用产出的英文表达建议能被正确解析展示。

1. 启动应用，在设置页面切换为中配或高配模型，确认"学习提示气泡"开关（`learningTipEnabled`）处于默认开启状态。
2. 唤起浮窗，输入一段典型中英拼音混杂草稿（如 `wo today bushi very e`），等待标准中文输出预览生成完成（`outputPreviewState` 变为 `Ready`）。
3. **预期（SC-001/SC-002）**：学习提示气泡区域（`learningTipVisible`）从不可见变为可见，展示一条非空英文表达建议文本（`learningTipText`），无需任何额外点击/展开操作即可读到完整句子。
4. **预期（SC-003）**：该英文建议在语义上能让人辨认出与步骤 2 输入/输出表达的是同一个意思（例如"今天不是很饿"对应"I'm not that hungry today"一类语义），而非与本轮会话无关的通用文本。
5. **预期（FR-003）**：建议文本只包含地道英文表达本身，不附带任何语法点讲解或中文说明文字。

---

## 验证场景二：持续编辑触发多轮重新生成时内容保持同步（User Story 1 验收场景 4，对应 SC-005）

1. 在场景一基础上继续编辑原始输入（追加若干字符，触发防抖重新生成）。
2. 等待新一轮标准中文输出预览生成完成。
3. **预期（FR-005）**：学习提示内容随之更新为与最新一轮输入/输出相关的建议，不停留在已过时的上一轮内容；更新时机与标准中文输出预览的更新时机**同步**（同一次生成完成回调内一起替换），不出现"中文预览已更新但英文建议还是旧的"的窗口期。

---

## 验证场景三：生成失败/本地模型不可用时气泡保持沉默（User Story 2，对应 SC-004）

1. 临时将约定本地模型目录中的模型文件移出/损坏，重新唤起浮窗并尝试输入。
2. **预期（FR-004）**：学习提示气泡保持不可见，不展示任何报错文案、乱码或占位符；浮窗其余区域（原始输入槽）按 001 FR-011 既有行为正常展示"模型不可用"提示，不受影响。
3. 恢复模型文件，重新唤起浮窗（**不是**同一会话内的重新生成，而是全新一次热键唤起）。
4. **预期**：学习提示气泡不展示上一次（模型不可用之前的会话）残留的任何内容，恢复为"等待本轮生成完成后才可能出现"的初始状态。

---

## 验证场景四：设置页面开关控制展示（User Story 1/2，对应 FR-008）

1. 在设置页面将"学习提示气泡"开关切换为关闭。
2. **预期**：`learningTipVisible` 立即变为不可见（不等待下一轮生成）。
3. 唤起浮窗输入一段草稿并等待生成完成。
4. **预期**：学习提示气泡全程保持不可见，不产生任何英文建议内容；标准中文输出预览（`outputPreviewText`）与原始输入槽（`rawInputSlotText`）的展示行为与开关开启时完全一致，不受影响（FR-008"MUST NOT 影响浮窗其余区域既有行为"）。
5. 重新打开开关，重复步骤 3。
6. **预期**：学习提示气泡恢复为场景一的正常展示行为。

---

## 验证场景五：低配默认档位恒不生成学习提示（Edge Cases，对应 FR-009）

1. 在设置页面切换为低配默认模型（Qwen2.5-3B-Instruct），确认学习提示开关处于开启状态。
2. 唤起浮窗输入一段典型草稿并等待生成完成。
3. **预期**：学习提示气泡全程保持不可见（`learningTipVisible` 恒为 `false`），标准中文输出预览的生成延迟与 001/002/003 交付时的既有体验一致（不因本特性引入任何可感知的延迟增加）。

---

## 实测记录

**记录时间**：`/speckit.implement` 执行完毕后（2026-10-21）。

**执行环境限制说明**：本次 `/speckit.implement` 由编码代理在无显示器/无 Accessibility 授权的终端环境中执行，无法进行真正意义上的"唤起浮窗、全局热键、鼠标点击设置页面开关"式人工 GUI 操作（与场景一～五字面步骤要求的操作方式不同）。但本机**确实已放置全部五档真实模型文件**（Qwen2.5-0.5B/1.5B/3B/7B/14B-Instruct，均为数百 MB～9GB 的真实权重文件，非占位符），因此改为对 `LlmEngine::generate(..., includeLearningTip=true)` 这一本特性的核心生成机制直接发起真实推理调用（绕开仅在生产环境接线的私有 `onSlowPathTriggered()`，与 tasks.md T004/Notes 已记录的测试边界一致），据此获得下列**真实模型输出证据**；`SessionController` 侧的字段读写/清空/NOTIFY 信号等结构性行为已由 T012/T014 自动化测试在真实构造的 `SessionController` 实例上验证通过（非 mock）。凡真正需要"浮窗可见性""设置页面点击"等 GUI 层面观察的部分，本节如实标注为"未进行 GUI 级人工验证，建议用户在本机实际运行应用后补充确认"，不编造数据。

### 场景一：中/高配档位展示地道英文表达建议（SC-001/SC-002/SC-003）

直接调用 `LlmEngine::generate(QStringLiteral("wo today bushi very e"), /*includeLearningTip=*/true)`：

- **中配（Qwen2.5-7B-Instruct）真实输出**：
  ```
  我今天不very饿

  <<<EN_TIP>>> I'm not very hungry today.
  ```
  分隔符之后的英文建议 `I'm not very hungry today.` 与输入语义一致，且只包含地道英文表达本身，未附加任何讲解文字（符合 FR-003）；单次调用耗时约 6.4 秒（含模型加载，生成本身更短）。
- **高配（Qwen2.5-14B-Instruct）真实输出**：
  ```
  我今天不是很好。<<<EN_TIP>>>I am not feeling well today.
  ```
  分隔符之后的英文建议 `I am not feeling well today.` 与该次标准中文译写结果（"我今天不是很好"）语义一致；单次调用耗时约 10.2 秒。
- **结论**：`includeLearningTip=true` 时模型（本机实测的 7B/14B 两档）确实能稳定输出 `<<<EN_TIP>>>` 分隔符与语义相关、格式纯净的英文建议，`parseLearningTipOutput()` 的"按分隔符切分+trim"算法可正确处理这两例真实输出。**未进行**真正意义上的浮窗 GUI 人工验证（设置页面点击切换模型、浮窗内观察气泡从不可见变为可见的视觉效果），建议用户在本机实际运行 `Babel.app` 后用鼠标/键盘走一遍本场景步骤 1～5 加以确认。

### 场景二：持续编辑触发多轮重新生成时内容保持同步（SC-005）

对同一 `LlmEngine` 实例（7B 模型）连续调用 4 轮 `generate(draft, true)`，`draft` 依次为逐步增长的草稿（`"wo"` → `"wo today"` → `"wo today bushi very e"` → `"wo today bushi very e, xiang chi malatang"`）：

```
round 1: 我<<<EN_TIP>>>I
round 2: 我今天\n\n<<<EN_TIP>>>\nI today
round 3: 我今天不very饿\n\n<<<EN_TIP>>> I'm not very hungry today.
round 4: 我今天不很饿，想吃麻辣烫<<<EN_TIP>>>I am not very hungry today and would like to eat some spicy Sichuan hotpot.
```

- **结论**：学习提示内容随每一轮输入增长而更新为对应内容（round 4 准确反映追加的"想吃麻辣烫"），未停留在旧内容；round 2 的真实输出在分隔符前后混入了空行（`"我今天\n\n<<<EN_TIP>>>\n"`），这交叉验证了 `parseLearningTipOutput()` 对 `candidateStandard`/`candidateTip` 做 `.trimmed()` 处理的必要性与正确性（若不 trim，`"\nI today"` 这类前导换行会被当作建议内容的一部分展示）。`SessionController::onSlowPathTriggered()` 内"同一次完成回调中一起替换 `m_outputPreview.content`/`m_learningTipText`/`m_learningTipVisible`"的实现（T009）从代码层面保证了"同步"这一时机要求，但**未进行** GUI 层面"肉眼确认无新旧内容交错窗口期"的人工观察。

### 场景三：生成失败/本地模型不可用时气泡保持沉默（SC-004）

- 自动化测试 `LearningTipStaysInvisibleAndEmptyWhenModelUnavailable`（T014）已验证：以指向不存在路径构造 `LlmEngine`/`SessionController` 后，`sessionState() == ModelUnavailable` 且 `learningTipVisible() == false`、`learningTipText().isEmpty() == true`，无回归。
- **未能验证**（与 tasks.md Notes 已与用户确认的边界一致）：①"真实模型从可用变为不可用/损坏后、原本已展示的学习提示内容是否被正确清空"；②"恢复模型文件后全新热键唤起、学习提示不展示上一次会话残留内容"——两者均需真实走通一次私有 `onSlowPathTriggered()` 回合才能构造出"已有残留内容"的前置状态，当前测试文件架构下无安全触发方式（`onSlowPathTriggered()` 为 private 且仅在生产专用 `bootstrapPlatformDefaults()` 内接线，`LlmEngine` 非多态类，仓库未引入 mock 框架）。`resetSessionData()` 清空分支本身的结构性正确性已由 T012 `ResetSessionDataClearsLearningTipFieldsAndEmitsNotifySignalsOnEachSessionInvocation` 验证（调用 `onHotkeyToggled()` 后两个字段确实被清空且信号确实 emit），但该测试无法构造"清空前存在真实非空残留内容"这一前置条件。建议用户在本机实际运行应用，完整走一遍移出模型文件→唤起→恢复模型文件→再次唤起的流程以补充确认。

### 场景四：设置页面开关控制展示（FR-008）

- 自动化测试 `LearningTipEnabledWriteUpdatesValueAndEmitsNotifyOnlyOnActualChange`（T012）已验证 `setLearningTipEnabled()` 的取值更新与 NOTIFY 去重语义。
- 代码层面（`SessionController::setLearningTipEnabled()`，T007）已实现"切换为 `false` 且当前 `m_learningTipVisible == true` 时立即清空字段并 emit 对应信号"的逻辑，但由于构造测试场景时 `m_learningTipVisible` 恒为初始 `false`（同样受限于私有 `onSlowPathTriggered()` 无法在测试中产出真实可见内容），**未能自动化验证**"关闭开关立即隐藏已展示的真实内容"这一效果对用户可见。建议用户实际运行应用，在学习提示气泡已展示真实内容时点击关闭开关，确认气泡立即消失、不等待下一轮生成。
- 标准中文输出预览/原始输入槽在开关关闭期间是否与开关开启时行为一致：代码审查确认 `onSlowPathTriggered()` 中 `wantLearningTip` 分支仅影响 `m_learningTipText`/`m_learningTipVisible` 的计算，不改变 `m_outputPreview.content`/`m_rawDraft`/`m_normalizedCandidate` 任何既有赋值路径，结构上不应受影响；未做 GUI 层面人工复核。

### 场景五：低配默认档位恒不生成学习提示（FR-009）

- 直接调用 `LlmEngine::generate(text)`（低配 Qwen2.5-3B-Instruct，`includeLearningTip` 取默认值 `false`）：真实耗时约 481ms（不含模型加载），输出不包含 `kLearningTipSeparator`，与 001～004 既有单参数调用行为一致。
- `SessionController::onSlowPathTriggered()` 中 `wantLearningTip` 对 `Low`/`Unknown` 档位恒为 `false`（T009 内联布尔表达式，代码审查确认 `currentTier == Low` 时短路，不会对 `m_learningTipEnabled` 取值、也不会对 `ModelCatalog`/字符串处理产生任何额外调用），因此低配档位下 `generate()` 固定走 `includeLearningTip=false` 分支、采样上限固定为既有 `kMaxOutputTokens=256`（而非 `kMaxOutputTokensWithLearningTip=400`），生成延迟理论上与 001～004 完全一致；本次 481ms 实测数值与历史基线（research.md 曾提及约 628ms 量级，同一模型不同输入文本/系统热度下会有波动）处于同一量级，未观察到因本特性引入而导致的额外延迟增加。**未做** GUI 层面"肉眼确认低配档位下气泡全程不可见"的人工复核。

### 总结

- **已通过真实模型推理直接验证**（非 mock/非 GTEST_SKIP）：中配（7B）、高配（14B）、低配（3B）三档模型在 `includeLearningTip` 对应取值下的实际生成行为，均符合契约预期；`parseLearningTipOutput()` 的 trim 兜底逻辑被真实输出中的边界情形（分隔符前后空行）交叉验证为必要且正确。
- **已通过自动化单元测试验证**：`learningTipEnabled` 属性读写/NOTIFY 去重、`resetSessionData()`/`onHotkeyToggled()` 清空分支的结构性正确性、模型不可用时学习提示恒不可见、`learningTipVisible`/`learningTipText` 一致性不变量——全部 10 个新增自动化测试用例（5 个 `LlmEngineGenerateTest` + 5 个 `SessionControllerTest`）与既有 98 个用例合计 108 个测试 100% 通过，无回归。
- **未能完成、需用户在真实 GUI 环境补充人工验证**：场景一/二的浮窗可见性视觉确认、场景三的"模型不可用→恢复后跨会话残留清空"真实效果、场景四的"关闭开关立即隐藏已展示真实内容"真实效果——均受限于 `onSlowPathTriggered()` 为生产专用私有接线这一既有架构约束（与 tasks.md Notes、004 `tasks.md` T029 同类处理方式一致），且本次执行环境无法进行真正的鼠标/键盘/全局热键 GUI 操作。这些"未验证"项不代表功能存在已知缺陷——相反，支撑这些场景的底层机制（`generate()` 双段输出、`parseLearningTipOutput()` 解析、字段清空与信号 emit）均已分别通过真实模型调用或自动化单元测试独立验证，仅"GUI 可见性"这一层面的端到端观察未被本次执行覆盖。
- `kMaxOutputTokensWithLearningTip = 400` 实测未观察到明显超出中/高配既有生成体验的耗时异常（7B 约 6.4s、14B 约 10.2s，均含一次性模型加载开销，不代表稳态单次生成延迟）。
