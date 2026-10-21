---

description: "Task list for 005-learning-tip-generation"
---

# Tasks: 学习提示气泡内容生成

**Input**: Design documents from `/specs/005-learning-tip-generation/`

**Prerequisites**: plan.md（已完成，Constitution Check PASS 无条件通过，含用户对"分隔符格式方案 3"的显式决策）、spec.md（已完成全部 4 项澄清）、research.md、data-model.md、contracts/、quickstart.md（均已完成）

**Tests**: 本特性**包含**测试任务，但范围经与用户确认（`/speckit.tasks` 对话中选定方案 C）后按以下边界拆分，**不新增任何测试文件**（与本特性"不新增任何新源文件"的整体约束一致）：

- `tests/slowpath/LlmEngineGenerateTest.cpp`：`LlmEngine::generate()` 是公开方法，`includeLearningTip` 参数的全部契约行为（`contracts/llm-engine-learning-tip-contract.md`）均可直接调用真实模型验证，延续 001～004 既有"本地未放置真实模型文件时 `GTEST_SKIP()`"惯例。
- `tests/session/SessionControllerTest.cpp`：核实现状架构后发现一处**既有约束**——`onSlowPathTriggered()`（本特性新增逻辑的实际落点）为 `private` 方法，仅在 `bootstrapPlatformDefaults()`（生产环境专用、单元测试 MUST NOT 调用，见 `SessionController.h` 既有注释）内部与 `DebounceScheduler` 回调完成接线；`LlmEngine` 本身非多态类（无 `virtual` 方法），仓库亦未引入任何 mock 框架（`vcpkg.json` 仅声明 `gtest`，未声明 `gmock`）。因此**当前测试文件架构下，没有任何安全方式可以从测试进程触发一次真实经过 `onSlowPathTriggered()` 的生成回合**——这一限制同时覆盖了用户已确认的"开关关闭立即隐藏""跨会话清空残留"两项，以及"低配档位恒不展示""开关关闭档位恒不展示"这两项（原计划作为确定性用例，经核实后发现同样不可达）。按用户决策（方案 C）：
  - **可自动化的确定性子集**（不依赖 `onSlowPathTriggered()`、不依赖真实模型推理内容）：`learningTipEnabled` 属性读写/`NOTIFY`、`resetSessionData()`/`onHotkeyToggled()` 对 `learningTipText`/`learningTipVisible` 的清空与信号触发（使用 `QObject::connect` 计数验证，非 `QSignalSpy`——仓库未引入 `Qt::Test` 组件，避免新增依赖）、模型不可用（FR-011 既有 `ModelUnavailable` 状态路径）时学习提示恒不可见（复用 `missingPath` 既有模式，`tests/session/ModelUnavailableTest.cpp` 已有先例，本特性在 `SessionControllerTest.cpp` 内新增同构用例，不新建文件）。
  - **延期到人工验证的子集**：低配/开关关闭场景下"新一轮生成后学习提示恒不展示"、开关关闭"立即隐藏已展示内容"、跨会话"清空已展示的真实残留内容"、以及中/高配档位下"实际展示出有效英文建议"——均需真实走通 `onSlowPathTriggered()` 一次完整回合，仅能通过 `quickstart.md` 场景一～五人工验证覆盖（与 004 `tasks.md` T029"受限于沙箱无显示支持，记录为阻塞，需在具备真实环境的机器上另行执行"同类处理方式一致），Polish 阶段逐一列出。

**语言（宪章《语言与文档规范》）**：任务描述使用简体中文；文件路径、符号名、命令保留英文。

**范围重申（`plan.md` Scale/Scope）**：本特性严格限定于以下 3 组既有文件 + 2 个既有测试文件，**不新增任何新源文件/头文件/QML 文件/测试文件**：

```text
babel/src/slowpath/LlmEngine.{h,cpp}
babel/src/session/SessionController.{h,cpp}
babel/content/SettingsPanel.qml

tests/slowpath/LlmEngineGenerateTest.cpp
tests/session/SessionControllerTest.cpp
```

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 可并行执行（不同文件、无依赖关系）
- **[Story]**: 所属用户故事（US1/US2）；Setup/Foundational/Polish 阶段任务不标注

---

## Phase 1: Setup（基线确认）

**Purpose**: 在改动前建立"全绿基线"，便于后续断言变更前后对照，不引入任何新依赖/新构建目标。

- [X] T001 运行既有完整 CTest 套件建立变更前基线：`cmake --build --preset macos` 后执行 `ctest --preset macos --output-on-failure`，确认 001～004 既有全部测试文件中的用例（当前共 16 个测试文件、101 个 `TEST()` 用例；依赖真实模型文件的用例按既有 `GTEST_SKIP()` 约定可跳过）处于可对照的基线状态，记录通过/跳过清单，供后续核对本特性新增用例是否按预期从新增变为 PASS（而非破坏既有断言）。

**Checkpoint**：基线已记录，可开始 Foundational 阶段。

---

## Phase 2: Foundational（跨故事阻塞性前置工作）

**Purpose**：`LlmEngine::generate()` 新增的 `includeLearningTip` 参数、`kLearningTipSeparator` 分隔符常量、双段输出系统提示词后缀、`kMaxOutputTokensWithLearningTip` 采样上限，是 User Story 1（展示有效建议）与 User Story 2（格式缺失/未产出时静默回退）共同依赖的基础设施（两个故事在 `SessionController` 侧走的是同一条 `generate()` 调用 + 解析分支，解析成功与否分别对应两个故事）——`generate()` 的新签名与行为契约 **MUST** 先于两个用户故事完成。

**⚠️ CRITICAL**: 本阶段完成前，两个用户故事均无法开始实施。

- [X] T002 [P] 在 `babel/src/slowpath/LlmEngine.h` 新增（`data-model.md` 二.1/二.2，`contracts/llm-engine-learning-tip-contract.md` 一）：
  - 公开常量 `static constexpr const char *kLearningTipSeparator = "<<<EN_TIP>>>";`（用户已决策采纳的方案 3 ASCII marker）；
  - `generate()` 签名新增带默认值参数：`QString generate(const QString &normalizedInput, bool includeLearningTip = false) const;`（返回类型不变，默认值保证 001～004 既有单参数调用点源码零改动）。

- [X] T003 在 `babel/src/slowpath/LlmEngine.cpp` 实现 T002 新增参数的行为（depends on T002，`research.md` §3/§6/§7，`contracts/llm-engine-learning-tip-contract.md` 二）：
  - 在 `generate()` 内新增内部常量 `constexpr int kMaxOutputTokensWithLearningTip = 400;`（与既有 `constexpr int kMaxOutputTokens = 256;` 并列）与一段仅在 `includeLearningTip == true` 时追加拼接的系统提示词后缀常量（暂命名 `kLearningTipInstructionSuffix`），文案 MUST 满足：(a) 明确"标准中文译写结果"在前、分隔符 `kLearningTipSeparator` 其次、"英文表达建议"在后的固定顺序；(b) 明确英文表达建议"只能是地道英文表达本身，不附加语法点讲解或其他说明文字"（FR-003 原文）；(c) 明确除上述三段内容外不得输出任何其他文字；
  - 既有 `kSystemPrompt` 常量内容本身**不变**；`includeLearningTip == true` 时实际使用的系统提示词文本为 `kSystemPrompt + kLearningTipInstructionSuffix`，`== false` 时沿用 `kSystemPrompt` 本身（与 001～004 现状逐字节一致）；
  - 新增 `const int effectiveMaxOutputTokens = includeLearningTip ? kMaxOutputTokensWithLearningTip : kMaxOutputTokens;`，并将既有单模型采样循环 `for (int i = 0; i < kMaxOutputTokens; ++i)` 与投机解码分支的 `while (producedTokens < kMaxOutputTokens)`/`windowSize = std::min(kSpeculativeDraftWindow, kMaxOutputTokens - producedTokens)` 三处既有引用（当前均在 `generate()` 方法体内）改为引用 `effectiveMaxOutputTokens`，使两条既有分支（单模型/投机解码）共享同一套按 `includeLearningTip` 决定的 prompt 文本与采样上限，不需要为投机解码分支单独适配代码（契约二.5 正交性要求）；
  - **MUST NOT** 修改 `ForbiddenTokenMask::build()` 预计算逻辑、既有黑名单范围（⟦/⟧/|）、既有语法梗阻/EOG 终止判定逻辑本身（契约二.3/二.6，`research.md` §4）。

- [X] T004 [P] 在 `tests/slowpath/LlmEngineGenerateTest.cpp` 新增测试用例（depends on T003，`contracts/llm-engine-learning-tip-contract.md` 二）：
  - `GenerateDefaultIncludeLearningTipFalseNeverContainsSeparator`：沿用既有 `ModelPaths::conventionalModelFilePath()` 模型不可用时 `GTEST_SKIP()` 惯例；分别调用既有单参数 `generate(text)` 与显式 `generate(text, /*includeLearningTip=*/false)`，断言两者输出均不包含 `LlmEngine::kLearningTipSeparator`（契约二.1，`includeLearningTip=false` 时与 001～004 现状逐字节一致的外部可观察等价性）；
  - `GenerateIncludeLearningTipTrueDoesNotCrashAndReturnsNonEmptyOutput`：调用 `generate(text, /*includeLearningTip=*/true)`，断言不崩溃且输出非空（模型是否真的遵循双段格式指令不作为本用例的强制断言——契约二.3 明确"格式指令未被遵循是预期内情形，不是 bug"）；
  - `GenerateIncludeLearningTipTrueNeverLeaksLatticeAnnotationSymbols`：复用既有 ⟦/⟧/| 检查手法（参照 `GenerateOnHighAmbiguityDensityInputNeverLeaksAnnotationSymbols`），确认 `includeLearningTip=true` 不影响既有 `ForbiddenTokenMask` 屏蔽效果；
  - `GenerateIncludeLearningTipTrueWhenSeparatorPresentTextBeforeSeparatorIsNonEmptyAfterTrim`：**条件式**不变量断言——若某次真实输出恰好包含 `kLearningTipSeparator`（不强制要求，模型遵循概率不保证），则断言分隔符之前trimmed 后的文本非空（为 `SessionController::parseLearningTipOutput()` 的"畸形输出兜底"算法提供真实模型输出层面的交叉验证依据，`data-model.md` 三.5 算法第 3 步）；若本次输出不含分隔符则该断言自然跳过（不视为失败）；
  - `GenerateWithDraftModelAndIncludeLearningTipTrueRemainsConsistentWithSingleModelPath`[P]（复用既有 `findRealDraftModelFile()` 辅助函数与既有 `GTEST_SKIP()` 惯例）：对 `LlmEngine(modelPath)` 与 `LlmEngine(modelPath, draftModelPath)` 两个实例均调用 `generate(text, /*includeLearningTip=*/true)`，断言二者均非空、均不含 ⟦/⟧/|（契约二.5 正交性：`includeLearningTip` 与投机解码分支完全独立，4 种组合均无需额外适配）。

- [X] T005 运行 `ctest --preset macos --output-on-failure --tests-regex "LlmEngineGenerateTest"`（depends on T004），确认 T002/T003/T004 新增内容全部通过（或因本地无真实模型文件而 `GTEST_SKIP`），且既有 `LlmEngineGenerateTest` 用例（`GeneratesWithoutCrashingOnMixedChineseEnglishDraft` 等）无回归。

**Checkpoint**：Foundational 就绪——`LlmEngine::generate()` 双段输出能力已可用，两个用户故事均可开始独立实施。

---

## Phase 3: User Story 1 - 译写同时看到地道英文表达建议 (Priority: P1) 🎯 MVP

**Goal**：`SessionController` 在中配/高配档位且学习提示开关开启时，于 `onSlowPathTriggered()` 同一次慢流回合中请求并解析出英文表达建议，回填 `learningTipText`/`learningTipVisible`，使 001 起从未展示过内容的学习提示气泡首次产生真实内容；设置页面提供默认开启的开关控件。

**Independent Test**：按 `quickstart.md` 验证场景一——在中配或高配档位、开关默认开启状态下，输入典型中英拼音混杂草稿并等待标准中文输出预览生成完成，验证学习提示气泡区域是否同步展示非空、语义相关的英文表达建议文本。

- [X] T006 [P] [US1] 在 `babel/src/session/SessionController.h` 新增（`contracts/qml-cpp-session-interface-increment.md` 一，`data-model.md` 三.1）：
  - `Q_PROPERTY(bool learningTipEnabled READ learningTipEnabled WRITE setLearningTipEnabled NOTIFY learningTipEnabledChanged)`；
  - 公开方法 `bool learningTipEnabled() const { return m_learningTipEnabled; }` 与 `void setLearningTipEnabled(bool enabled);`（声明，实现见 T007）；
  - 信号 `void learningTipEnabledChanged();`；
  - 私有成员 `bool m_learningTipEnabled = true;`（FR-008 默认开启，不持久化，`research.md` §8 同类先例）。

- [X] T007 [US1] 在 `babel/src/session/SessionController.cpp` 实现 `setLearningTipEnabled()`（depends on T006，`data-model.md` 三.1）：取值未变化时直接返回（不 emit）；取值变化时更新 `m_learningTipEnabled` 并 `emit learningTipEnabledChanged();`；**当且仅当切换为 `false` 且当前 `m_learningTipVisible == true`** 时，立即将 `m_learningTipText` 置空、`m_learningTipVisible` 置 `false` 并分别 `emit learningTipTextChanged();`/`emit learningTipVisibleChanged();`（FR-008"MUST 立即...不等待下一轮生成完成"，不产生等待下一轮生成的副作用）；开启（`true`）时不产生任何立即副作用（气泡内容仍需等待下一轮慢流生成完成才会出现）。

- [X] T008 [US1] 在 `babel/src/session/SessionController.cpp` 匿名命名空间新增（depends on T003，`data-model.md` 一/三.5）：
  - 结构体 `struct ParsedLearningTipOutput { QString standardOutput; QString learningTip; bool learningTipPresent = false; };`；
  - 函数 `ParsedLearningTipOutput parseLearningTipOutput(const QString &raw)`，按 `data-model.md` 三.5 算法实现：查找 `raw` 中 `babel::slowpath::LlmEngine::kLearningTipSeparator` 首次出现位置 `idx`；`idx < 0` → `standardOutput = raw`、`learningTipPresent = false`；`idx >= 0` 时取 `candidateStandard = raw.left(idx).trimmed()`、`candidateTip = raw.mid(idx + 分隔符长度).trimmed()`，`candidateStandard` 为空则判定畸形输出、`standardOutput = raw`（整段原文兜底）、`learningTipPresent = false`；否则 `standardOutput = candidateStandard`，`candidateTip` 非空则 `learningTip = candidateTip`、`learningTipPresent = true`，否则 `learningTipPresent = false`（`standardOutput` 仍保留已解析出的有效中文部分）。纯字符串处理，不依赖 `LlmEngine`/`SessionController` 任何私有状态，线程安全。

- [X] T009 [US1] 改造 `babel/src/session/SessionController.cpp` 的 `onSlowPathTriggered()`（depends on T007, T008，`data-model.md` 三.4）：
  - 在启动后台线程**之前**（主线程）计算：`const auto currentTier = babel::slowpath::ModelCatalog::classifyTierByFileName(QString::fromStdString(m_modelRuntimeState.currentModelPath.filename().string()));` 与 `const bool wantLearningTip = m_learningTipEnabled && (currentTier == babel::slowpath::ModelDescriptor::TierHint::Medium || currentTier == babel::slowpath::ModelDescriptor::TierHint::High);`（`Low`/`Unknown` 恒为 `false`，FR-009；不新增独立查表函数，内联布尔表达式，`data-model.md` 四）；
  - 将 `wantLearningTip` 按值捕获进后台线程 lambda；原 `engine->generate(inputSnapshot)` 改为 `engine->generate(inputSnapshot, wantLearningTip)`；`wantLearningTip == true` 时在**同一后台线程**内调用 `parseLearningTipOutput(generated)`（T008，纯函数、线程安全）得到 `standardOutput`/`learningTip`/`learningTipPresent`；`wantLearningTip == false` 时 `standardOutput = generated`、`learningTip = QString()`、`learningTipPresent = false`；
  - 在既有 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 完成回调内（与 `m_outputPreview.content` 回填**同一次**回调，`research.md` §2 既定"仅完成回调整体替换"时机）新增：`self->m_outputPreview.content = standardOutput;`（替换原先直接赋值 `generated`）、`self->m_learningTipText = learningTipPresent ? learningTip : QString();`、`self->m_learningTipVisible = learningTipPresent;`，并分别 `emit self->learningTipTextChanged();`/`emit self->learningTipVisibleChanged();`（与既有 `outputPreviewTextChanged()`/`outputPreviewStateChanged()` 一并 emit，顺序不敏感）。

- [X] T010 [US1] 在 `babel/src/session/SessionController.cpp` 的 `resetSessionData()` 新增（depends on T007，`research.md` §2，`data-model.md` 三.3）：`m_learningTipText = QString();`、`m_learningTipVisible = false;`，并 `emit learningTipTextChanged();`/`emit learningTipVisibleChanged();`（置于既有字段重置逻辑之后、既有信号 emit 之前或之后均可，保持与既有字段重置风格一致），避免跨会话残留上一次会话的英文建议内容。

- [X] T011 [P] [US1] 在 `babel/content/SettingsPanel.qml` 新增开关控件（depends on T006，`data-model.md` 五）：复用既有 `speculativeDecodingEnabled` 开关完全相同的纯 `Rectangle`（圆角滑块轨道+圆形滑块）+ `MouseArea` 实现模式（不引入 `QtQuick.Controls` 依赖，不新增任何 QML 文件），绑定 `SessionController.learningTipEnabled`，点击时执行 `SessionController.learningTipEnabled = !SessionController.learningTipEnabled`，搭配一个 `Text` 标签（如"学习提示气泡（默认开启）"）。放置位置建议紧邻既有投机解码开关附近，具体视觉细节（颜色/文案措辞）按宪章原则 IV 自主确定。

### Tests for User Story 1

- [X] T012 [P] [US1] 在 `tests/session/SessionControllerTest.cpp` 新增测试用例（depends on T009, T010）：
  - `LearningTipEnabledDefaultsToTrue`：构造 `SessionController` 后断言 `learningTipEnabled() == true`（FR-008 默认开启）；
  - `LearningTipEnabledWriteUpdatesValueAndEmitsNotifyOnlyOnActualChange`：用 `QObject::connect(&controller, &SessionController::learningTipEnabledChanged, [&]{ ++count; })` 计数（不引入 `QSignalSpy`/`Qt::Test` 新依赖）；调用 `setLearningTipEnabled(false)` 后断言 `learningTipEnabled() == false` 且计数为 1；再次调用 `setLearningTipEnabled(false)`（取值未变化）断言计数仍为 1（不重复 emit）；调用 `setLearningTipEnabled(true)` 断言恢复 `true` 且计数变为 2；
  - `ResetSessionDataClearsLearningTipFieldsAndEmitsNotifySignalsOnEachSessionInvocation`：用 `QObject::connect` 分别对 `learningTipTextChanged`/`learningTipVisibleChanged` 计数；调用 `onHotkeyToggled()`（Hidden → Invoked，内部触发一次 `resetSessionData()`）后断言两个计数均 ≥ 1、`learningTipText().isEmpty() == true`、`learningTipVisible() == false`；**注释须注明**：本用例验证的是"每次新会话唤起都会执行清空分支并正确 emit 信号"这一结构性正确性，**不**验证"确实清除了上一会话残留的真实非空内容"（该效果需真实走通一次 `onSlowPathTriggered()` 生成回合才能构造前置状态，受限于 `onSlowPathTriggered()` 为 private 且仅在生产专用的 `bootstrapPlatformDefaults()` 内接线，测试进程无法安全触发，留待 `quickstart.md` 场景三人工验证，Polish 阶段 T019）。

- [X] T013 运行 `ctest --preset macos --output-on-failure --tests-regex "SessionControllerTest|LlmEngineGenerateTest"`（depends on T012），确认 User Story 1 新增断言全部通过，既有用例无回归。

**Checkpoint**：User Story 1 结构性实现与可自动化验证部分完成（MVP）；真实场景下"确实展示出语义相关的英文建议"效果需 `quickstart.md` 场景一人工验证（Polish 阶段 T017）。

---

## Phase 4: User Story 2 - 生成失败或不可用时气泡保持沉默而非展示错误/陈旧内容 (Priority: P2)

**Goal**：模型不可用、生成失败、或本轮未能产出有效双段格式时，学习提示气泡保持不可见，不展示报错文案、乱码、占位符或残留的陈旧内容，不影响浮窗其余区域既有行为。本故事的核心机制（`parseLearningTipOutput()` 的"分隔符缺失/畸形输出"兜底分支、`wantLearningTip` 的档位/开关门控）已在 User Story 1 阶段（T008/T009）随同一套代码路径一并实现——两个故事在 `SessionController` 内共享完全相同的判定与解析逻辑，US1 对应"解析成功"分支、US2 对应"解析失败/未请求"分支。本阶段聚焦针对该共享机制中"失败/不可用"一侧补充专门的回归测试。

**Independent Test**：按 `quickstart.md` 验证场景三——在本地模型缺失或推理异常的环境下唤起浮窗并尝试输入，验证学习提示气泡区域是否保持不可见、不展示任何报错或残留内容。

### Tests for User Story 2

- [X] T014 [P] [US2] 在 `tests/session/SessionControllerTest.cpp` 新增测试用例（depends on T009，复用 `tests/session/ModelUnavailableTest.cpp` 已有的 `missingPath` 构造模式，不依赖真实模型文件，完全确定性）：
  - `LearningTipStaysInvisibleAndEmptyWhenModelUnavailable`：以指向不存在文件路径的 `LlmEngine` 构造 `SessionController`（`setLlmEngine(std::make_shared<LlmEngine>(missingPath))`），`ASSERT_FALSE(controller.modelAvailable())` 后调用 `onHotkeyToggled()`，断言 `sessionState() == FloatingSessionState::ModelUnavailable`（既有 FR-011 行为无回归）且 `learningTipVisible() == false`、`learningTipText().isEmpty() == true`（`onSlowPathTriggered()` 既有的 `!m_modelAvailable` 早退路径本就不会触碰这两个字段，本用例断言其保持默认初始值，FR-004 语义）；
  - `LearningTipVisibleAndTextConsistencyInvariantHoldsAfterConstruction`：构造后（未调用任何生成相关方法）断言 `learningTipVisible() == !learningTipText().isEmpty()` 恒成立（即 `learningTipVisible()` 为 `true` 当且仅当 `learningTipText()` 非空，FR-004"不得出现标准中文译写失败、但学习提示仍展示内容的不一致状态"对应的最小可确定性断言子集）。

- [X] T015 运行 `ctest --preset macos --output-on-failure --tests-regex "SessionControllerTest"`（depends on T014），确认 User Story 2 新增断言全部通过，既有用例（含 T012 新增）无回归。

**Checkpoint**：User Story 1、User Story 2 结构性实现与可自动化验证部分均已完成；真实场景下"中/高配展示有效建议""低配/关闭开关恒不展示""关闭开关立即隐藏已展示内容""跨会话清空真实残留内容"均需 Polish 阶段按 `quickstart.md` 场景一/二/三/四/五人工验证。

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**：两个用户故事均完成后的全量回归、端到端手工验证（覆盖受限于测试架构无法自动化的场景，含用户已确认延期到本阶段的"开关关闭立即隐藏""跨会话清空残留"两项）与实测数据记录。

- [X] T016 全量回归：运行 `ctest --preset macos --output-on-failure`（depends on T005, T013, T015），执行全部既有 + 本特性新增测试用例，确认无回归失败，与 T001 建立的基线逐条对照差异均为本特性预期内的新增/变更。

- [X] T017 [P] 按 `quickstart.md` 验证场景一（中/高配档位、开关默认开启时看到地道英文表达建议，对应 User Story 1、SC-001/SC-002/SC-003）手工验证（depends on T009, T011）：记录学习提示气泡是否从不可见变为可见、展示内容是否语义相关、是否仅包含地道英文表达本身不附加语法点讲解；若本机无真实中配/高配目标模型文件，记录"因缺少模型文件无法实测"并说明需用户下载后补测（与 003/004 既有记录模式一致）。

- [X] T018 [P] 按 `quickstart.md` 验证场景二（持续编辑触发多轮重新生成时内容保持同步，对应 User Story 1 验收场景 4、SC-005）手工验证（depends on T009）：核对学习提示内容与标准中文输出预览的更新时机是否同步（同一次生成完成回调内一起替换），不出现"中文预览已更新但英文建议还是旧的"窗口期。

- [X] T019 按 `quickstart.md` 验证场景三（生成失败/本地模型不可用时气泡保持沉默，对应 User Story 2、SC-004；**含** T012 `ResetSessionDataClearsLearningTipFieldsAndEmitsNotifySignalsOnEachSessionInvocation` 明确标注延期的"跨会话清空真实残留内容"效果）手工验证（depends on T009, T010, T014）：临时移出/损坏模型文件后验证气泡保持不可见；恢复模型文件并**全新**热键唤起（非同会话内重新生成），验证学习提示气泡不展示上一次会话残留的任何内容。

- [X] T020 按 `quickstart.md` 验证场景四（设置页面开关控制展示，对应 FR-008；**含**本特性 `/speckit.tasks` 阶段与用户确认延期的"关闭开关立即隐藏已展示内容"效果）手工验证（depends on T007, T011）：验证开关切换为关闭后 `learningTipVisible` 是否立即变为不可见（不等待下一轮生成）；验证开关关闭期间标准中文输出预览与原始输入槽的展示行为与开关开启时完全一致；重新打开开关后验证恢复为场景一的正常展示行为。

- [X] T021 [P] 按 `quickstart.md` 验证场景五（低配默认档位恒不生成学习提示，对应 FR-009）手工验证（depends on T009）：确认低配档位下学习提示气泡全程保持不可见，且标准中文输出预览的生成延迟与 001～004 既有体验一致，不因本特性引入任何可感知的延迟增加。

- [X] T022 记录 `quickstart.md`"实测记录"小节要求的验证结果（depends on T017, T018, T019, T020, T021，`/speckit.implement` 完成后填写，**不得**回填 `spec.md` 本身）：如实记录每个场景的实测结论（含因缺少真实中/高配模型文件、或测试架构限制导致无法完整实测的部分，说明需用户补测的条件），**不得**预先编造任何具体延迟数值或"模型是否遵循格式指令"的成功率数据。

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**：无依赖，可立即开始。
- **Foundational (Phase 2)**：依赖 Setup 完成；**阻塞**两个用户故事——`LlmEngine::generate()` 新增的 `includeLearningTip` 参数/`kLearningTipSeparator`/采样上限是 US1（请求并展示有效建议）、US2（请求失败/未请求时的静默回退）共同的前置依赖。
- **User Story 1 (Phase 3)**：依赖 Foundational 完成；是 MVP 核心，**建议最先完成**。
- **User Story 2 (Phase 4)**：依赖 Foundational 完成；其核心实现（T008/T009 的兜底分支与门控逻辑）已随 US1 一并完成，本阶段任务（T014/T015）为纯测试补强，**依赖** US1 的 T009 已完成（断言对象是 T009 接线后的 `onSlowPathTriggered()`/`m_learningTipVisible`/`m_learningTipText` 字段）。
- **Polish (Phase 5)**：依赖两个用户故事完成。

### User Story Dependencies

- **User Story 1 (P1)**：Foundational 完成后即可开始，不依赖 US2。
- **User Story 2 (P2)**：Foundational 完成后逻辑上即具备前置条件，但其测试断言的对象（`onSlowPathTriggered()` 新分支）由 US1 的 T009 引入，故实际开工顺序 **MUST** 晚于 US1 对应任务完成（与 spec.md 将 US2 定位为"围绕 US1 核心价值的失败兜底场景"的既定关系一致）。

### Within Each User Story

- 测试 MUST 在实现完成后编写并验证通过。
- 头文件声明（T002、T006）先于对应 `.cpp` 实现（T003、T007～T010）。
- `LlmEngine` 内部实现（T003）先于 `SessionController` 对其的调用点改造（T008～T009，`parseLearningTipOutput()` 依赖 `kLearningTipSeparator` 常量已存在）。
- 每个用户故事的 Checkpoint 达成后方可视为该故事的结构性实现与可自动化验证部分独立可交付；真实端到端效果统一在 Polish 阶段人工验证。

### Parallel Opportunities

- Foundational 阶段：T002（`LlmEngine.h`）与其余阶段任务无交集，可随时先行准备；T003（依赖 T002）、T004（依赖 T003）须顺序执行。
- US1：T006（`SessionController.h`）与 T011（`SettingsPanel.qml`）操作不同文件，可并行；T007～T010（`SessionController.cpp` 同文件）须顺序执行（T008 不依赖 T007，但二者同文件，建议顺序合并以减少冲突）；T012（测试文件）可在 T009/T010 完成后独立编写。
- US2：T014（测试文件）与 US1 的 T012 同文件但不同用例块，建议顺序合并。
- US1、US2 各自的 ctest 验证任务（T013、T015）须在对应测试新增完成后执行。
- Polish 阶段：T017～T021 为 5 个相互独立的手工验证场景，可并行执行（不同验证路径、互不干扰）。

---

## Parallel Example: Foundational + User Story 1 启动

```bash
# Foundational 头文件声明（可先行）：
Task: "在 babel/src/slowpath/LlmEngine.h 新增 kLearningTipSeparator 常量与 generate() 的 includeLearningTip 参数声明"

# Foundational 完成后，US1 的头文件与 QML 任务可并行：
Task: "在 babel/src/session/SessionController.h 新增 learningTipEnabled 读写属性声明"
Task: "在 babel/content/SettingsPanel.qml 新增学习提示开关控件"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. 完成 Phase 1：Setup（基线确认）
2. 完成 Phase 2：Foundational（`LlmEngine::generate()` 双段输出能力，**阻塞**两个用户故事）
3. 完成 Phase 3：User Story 1（`SessionController` 门控+解析+回填 + 设置页面开关）
4. **停止并验证**：按 `quickstart.md` 场景一/二独立验证 User Story 1（中/高配档位展示语义相关建议、多轮重新生成同步更新）
5. 具备 MVP 交付条件：001 起留空的学习提示气泡首次展示真实内容

### Incremental Delivery

1. Setup + Foundational 完成 → 基础设施就绪
2. 新增 User Story 1 → 独立验证 → 部署/演示（**MVP**）
3. 新增 User Story 2 → 独立验证 → 部署/演示（失败/不可用场景不展示报错或残留内容）
4. Polish 阶段：全量回归 + 5 个 quickstart 场景人工验证 + 实测数据记录

### Parallel Team Strategy

多名开发者协作时：

1. 团队共同完成 Setup + Foundational（`LlmEngine` 核心变更，建议由一人完成以避免同文件冲突）
2. Foundational 完成后：
   - 开发者 A：User Story 1 核心接线（`SessionController.{h,cpp}` 的 T006～T010）
   - 开发者 B：User Story 1 的 `SettingsPanel.qml`（T011，与 A 并行，不同文件）；待 A 完成后再编写 US1/US2 测试（T012/T014，同一测试文件，建议由同一人顺序完成以减少合并冲突）
3. 两个故事独立验证后集成，互不破坏彼此已验证的行为

---

## Notes

- `[P]` 任务 = 不同文件、无依赖关系。
- `[Story]` 标签将任务映射到对应用户故事，便于追溯。
- **测试覆盖边界已与用户确认**（`/speckit.tasks` 对话中选定方案 C）：`onSlowPathTriggered()` 为 private 方法且仅在生产专用的 `bootstrapPlatformDefaults()` 内接线，`LlmEngine` 非多态类、仓库未引入 mock 框架，故"开关关闭立即隐藏已展示内容""跨会话清空真实残留内容""低配/关闭开关档位下新一轮生成后恒不展示""中/高配档位下实际展示出有效建议"四类真实端到端效果均无法从 `SessionControllerTest.cpp` 安全触发，统一延期到 Polish 阶段 `quickstart.md` 场景一/三/四人工验证（与 004 `tasks.md` T029 同类处理方式一致）；`SessionControllerTest.cpp` 自动化覆盖聚焦不依赖该私有接线的确定性子集（属性读写/NOTIFY、`resetSessionData()`/`onHotkeyToggled()` 清空分支的结构性正确性、`ModelUnavailable` 既有状态路径下的字段默认值）。
- 每个用户故事应可独立完成与验证；US2 因与 US1 共享 `onSlowPathTriggered()` 同一套判定/解析代码路径（而非像 004 那样是技术查表依赖关系），测试任务在 US1 对应实现完成后开工，但不影响各自验收场景的独立性。
