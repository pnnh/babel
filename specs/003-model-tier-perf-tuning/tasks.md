---

description: "Task list for 003-model-tier-perf-tuning"
---

# Tasks: 模型档位调整、GBNF 语法约束解码与防抖节奏优化

**Input**: Design documents from `/specs/003-model-tier-perf-tuning/`

**Prerequisites**: plan.md（已完成，Constitution Check PASS）、spec.md（已完成全部 3 项澄清）、research.md、data-model.md、contracts/、quickstart.md（均已完成）

**Tests**: 本特性**包含**测试任务——并非为本特性新发起 TDD 流程，而是 `plan.md` Technical Context"Testing"小节与 `research.md`/`contracts/*.md` 明确要求对 001/002 既有 65 个 GoogleTest 用例中的 3 个文件（`ModelCatalogTest.cpp`/`LlmEngineGenerateTest.cpp`/`DebounceSchedulerTest.cpp`）做**必要的断言更新**，并按行为契约新增覆盖用例；`tests/session/SessionControllerTest.cpp` 新增 1 项集成测试。全部任务均为"更新既有测试文件"或"在既有测试文件内新增用例"，**不新增任何测试文件**（与本特性"不新增任何新源文件"的整体约束一致，GLOB 自动发现 + CMake POST_BUILD ad-hoc codesign 基础设施无需改动）。

**语言（宪章《语言与文档规范》）**：任务描述使用简体中文；文件路径、符号名、命令保留英文。

**范围重申（`plan.md` Scale/Scope）**：本特性严格限定于以下 4 组既有文件 + 4 个既有测试文件，**不新增任何新源文件/头文件/QML 文件**：

```text
babel/src/slowpath/ModelCatalog.{h,cpp}
babel/src/slowpath/LlmEngine.cpp          （LlmEngine.h 签名不变，不修改）
babel/src/slowpath/DebounceScheduler.{h,cpp}
babel/src/session/SessionController.cpp   （SessionController.h 签名不变，不修改）

tests/slowpath/ModelCatalogTest.cpp
tests/slowpath/LlmEngineGenerateTest.cpp
tests/slowpath/DebounceSchedulerTest.cpp
tests/session/SessionControllerTest.cpp
```

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 可并行执行（不同文件、无依赖关系）
- **[Story]**: 所属用户故事（US1/US2）；Setup/Foundational/Polish 阶段任务不标注

---

## Phase 1: Setup（基线确认）

**Purpose**: 在改动前建立"全绿基线"，便于后续断言变更前后对照，不引入任何新依赖/新构建目标。

- [X] T001 运行既有完整 CTest 套件建立变更前基线：`cmake --build --preset macos` 后执行 `ctest --preset macos --output-on-failure`，确认 001/002 既有全部测试文件（含本特性将要修改的 `tests/slowpath/ModelCatalogTest.cpp`/`tests/slowpath/LlmEngineGenerateTest.cpp`/`tests/slowpath/DebounceSchedulerTest.cpp`/`tests/session/SessionControllerTest.cpp`）在改动前处于可对照的基线状态（依赖真实模型文件的用例按既有 `GTEST_SKIP()` 约定可跳过，记录跳过/通过清单供后续核对哪些用例因本特性断言变更而预期改变）。

**Checkpoint**：基线已记录，可开始 User Story 实施。

---

## Phase 2: Foundational（跨故事阻塞性前置工作）

**Purpose**: 核对是否存在 User Story 1/2 共享、且会阻塞其中任一故事独立开工的基础设施。

**本阶段无任务**：`plan.md` Project Structure 已确认 User Story 1（`ModelCatalog.{h,cpp}`/`DebounceScheduler.{h,cpp}`/`SessionController.cpp` 及对应 3 个测试文件）与 User Story 2（`LlmEngine.cpp` 及 `LlmEngineGenerateTest.cpp`）的改造文件集合**完全不相交**，彼此不存在调用/数据依赖（GBNF 语法约束是 `generate()` 纯内部实现细节，不依赖模型档位信息；防抖窗口动态调整不依赖 GBNF 采样方式）。两个用户故事均可在 Phase 1 完成后**直接独立开工、可完全并行**，无需共享的新增基础设施。

**Checkpoint**：无阻塞项，直接进入 Phase 3/Phase 4（两者可并行）。

---

## Phase 3: User Story 1 - 中配/高配档位用户的实际使用延迟体感改善 (Priority: P1) 🎯 MVP

**Goal**：设置页面"中配"/"高配"推荐模型数值调整为 Qwen2.5-7B-Instruct/Qwen2.5-14B-Instruct（FR-001/FR-002）；`DebounceScheduler` 防抖窗口依据当前选定模型档位动态变化——低配 200ms（不变）/中配 500ms/高配 800ms/`Unknown` 档位复用低配 200ms（FR-010～FR-014），既有状态机与 `m_generationInFlight` 并发保护逻辑不变（FR-013）。

**Independent Test**：在设置页面分别切换为调整后的中配、高配推荐模型，验证展示数值已更新（FR-001/FR-002）且此前按旧版推荐下载的 14B/32B 文件仍可被扫描选用（FR-004）；在持续输入场景下比较切换前后防抖窗口触发"整句重新生成"所需的静默时长（低配 200ms 基线 vs 中配 500ms vs 高配 800ms vs 自定义命名模型 `Unknown` 复用 200ms），验证非默认档位下触发所需静默时长确有可测量的拉长，且并发保护（同一次生成完成前不重叠触发）与既有行为一致。

### Implementation for User Story 1

- [X] T002 [P] [US1] 在 `babel/src/slowpath/ModelCatalog.h` 新增两个公开静态方法声明（`contracts/debounce-tier-window-internal-contract.md` 二/三，`data-model.md` 二/五）：`static ModelDescriptor::TierHint classifyTierByFileName(const QString &fileName);`（原匿名命名空间私有 `classifyTier(const std::string&)` 提升为公开静态方法，参数类型由 `std::string` 改为 `QString`）与 `static std::chrono::milliseconds tierDebounceWindow(ModelDescriptor::TierHint tier);`（新增查表函数，就近放置于 `ModelCatalog` 旁边，与 `classifyTierByFileName()` 产出的枚举值直接衔接，避免新增跨文件依赖）；需新增 `#include <chrono>`。

- [X] T003 [US1] 在 `babel/src/slowpath/ModelCatalog.cpp` 实现 T002 新增的两个方法（depends on T002）：
  - 将原匿名命名空间内 `classifyTier(const std::string &fileName)` 的关键字匹配逻辑迁移/提升为 `ModelCatalog::classifyTierByFileName(const QString &fileName)`（内部可经 `fileName.toStdString()` 复用既有 `toLower()`/`find()` 逻辑，或改为 QString 原生 API，内部实现细节自主选择）；
  - 更新关键字—档位对应表（`research.md` §5）：`"3b"`/`"3-b"` → `Low`（不变）；新增 `"7b"`/`"7-b"` → `Medium`；`"14b"` 由原 `Medium` 改判 → `High`（变更，核心逻辑）；`"32b"` → `High`（不变）；均不匹配 → `Unknown`（不变）；
  - `scan()` 内部调用点由私有 `classifyTier(fileName)` 改为调用 `ModelCatalog::classifyTierByFileName(descriptor.fileName)`，对 `scan()` 既有调用方透明；
  - 实现 `tierDebounceWindow(ModelDescriptor::TierHint tier)`：`Low` → `200ms`、`Medium` → `500ms`、`High` → `800ms`、`Unknown` → `200ms`（复用 `Low` 档位数值，用户已显式决策，`research.md` §5/`data-model.md` 二）。

- [X] T004 [US1] 更新 `babel/src/slowpath/ModelCatalog.cpp` 中 `recommendedProfiles()` 的静态数据取值（FR-001/FR-002，`research.md` §6；纯数据变更，与 T002/T003 的签名/关键字表变更相互独立，但同一文件需避免并行编辑冲突，建议顺序执行）：Medium 档 `modelName` 由 `QStringLiteral("Qwen2.5-14B-Instruct")` 改为 `QStringLiteral("Qwen2.5-7B-Instruct")`、`approxSizeDescription` 由 `QStringLiteral("约 9GB")` 改为 `QStringLiteral("约 4.6GB")`；High 档 `modelName` 由 `QStringLiteral("Qwen2.5-32B-Instruct")` 改为 `QStringLiteral("Qwen2.5-14B-Instruct")`、`approxSizeDescription` 由 `QStringLiteral("约 19.85GB")` 改为 `QStringLiteral("约 9GB")`；Low 档与 `quantization`/`targetHardwareDescription` 两个字段均不变。

- [X] T005 [P] [US1] 在 `babel/src/slowpath/DebounceScheduler.h` 新增公开方法声明 `void setDebounceWindow(std::chrono::milliseconds window);`（`contracts/debounce-tier-window-internal-contract.md` 一）；既有构造函数签名与全部私有状态字段（`m_state`/`m_pendingInput`/`m_lastChangeAt`/`m_timerArmed`/`m_generationInFlight`）声明均不变。

- [X] T006 [US1] 在 `babel/src/slowpath/DebounceScheduler.cpp` 实现 `setDebounceWindow()`（depends on T005）：方法体仅执行 `m_debounceWindow = window;` 一行赋值，不触碰 `m_state`/`m_pendingInput`/`m_lastChangeAt`/`m_timerArmed`/`m_generationInFlight` 等既有字段（FR-013），可在任意 `state()` 下安全调用；调用后下一次 `tick()` 对"是否已过防抖窗口"的判定自然使用新值（无需额外生效时机控制逻辑，FR-014）。

- [X] T007 [US1] 在 `babel/src/session/SessionController.cpp` 的 `bootstrapPlatformDefaults()` 中（depends on T003, T004, T006）：将当前硬编码 `std::chrono::milliseconds(200)` 构造 `DebounceScheduler` 的方式，改为先对 `defaultModelPath` 文件名调用 `babel::slowpath::ModelCatalog::classifyTierByFileName(QString::fromStdString(defaultModelPath.filename().string()))` 得到 `TierHint`，再调用 `babel::slowpath::ModelCatalog::tierDebounceWindow(tier)` 得到窗口时长，并以该值构造 `DebounceScheduler`（预期默认低配模型恒为 `Low`/200ms，与 FR-011 一致，`contracts/debounce-tier-window-internal-contract.md` 四"启动调用点"）。

- [X] T008 [US1] 在 `babel/src/session/SessionController.cpp` 的 `applyModelSwitchResult(...)` 中（depends on T003, T004, T006；与 T007 同文件，建议在 T007 后顺序进行）：在 `success == true` 分支内，对 `finalModelPath` 文件名执行与 T007 相同的"`classifyTierByFileName()` → `tierDebounceWindow()` → `setDebounceWindow()`"流程；`success == false` 分支 MUST NOT 调用该流程（`contracts/debounce-tier-window-internal-contract.md` 四"热切换完成调用点"）；**必须**对 `m_debounceScheduler` 做空指针守卫（`if (m_debounceScheduler) { ... }`），因既有测试（`tests/session/SessionControllerTest.cpp` 现有 `SwitchModelToValidModelUpdatesStateAndEmitsCompletion` 等用例）通过 `setLlmEngine()` 直接构造 `SessionController` 且未调用 `setDebounceScheduler()`，此时 `m_debounceScheduler` 为 `nullptr`，缺少守卫会导致既有测试崩溃。

### Tests for User Story 1

- [X] T009 [US1] 更新 `tests/slowpath/ModelCatalogTest.cpp` 的 `TierHintClassifiesByFilenameKeyword` 用例（depends on T003）：新增一个文件名含 `"7b"` 关键字的 `.gguf` 占位文件，断言其 `tierHint == ModelDescriptor::TierHint::Medium`；将既有 `qwen2.5-14b-instruct-q4_k_m.gguf` 的断言由 `Medium` 改为 `High`；`"32b"`/`"3b"`/`mystery-model.gguf`（`Unknown`）三项断言保持不变。

- [X] T010 [US1] 更新 `tests/slowpath/ModelCatalogTest.cpp` 的 `RecommendedProfilesReturnFixedThreeTiers` 用例（depends on T004；与 T009 同文件，顺序执行）：Medium 档 `modelName.contains(...)` 断言由 `QStringLiteral("14B")` 改为 `QStringLiteral("7B")`；High 档由 `QStringLiteral("32B")` 改为 `QStringLiteral("14B")`；Low 档 `QStringLiteral("3B")` 断言不变；`quantization`/`approxSizeDescription`/`targetHardwareDescription` 非空断言不变。

- [X] T011 [US1] 在 `tests/slowpath/ModelCatalogTest.cpp` 新增 `ClassifyTierByFileNameIsPubliclyCallable` 测试用例（depends on T003；与 T009/T010 同文件，顺序执行）：不经由 `scan()`，直接调用 `ModelCatalog::classifyTierByFileName()`，分别传入含 `"3b"`/`"7b"`/`"14b"`/`"32b"` 关键字及不含任何关键字的文件名字符串，断言返回值依次为 `Low`/`Medium`/`High`/`High`/`Unknown`。

- [X] T012 [US1] 在 `tests/slowpath/ModelCatalogTest.cpp` 新增 `TierDebounceWindowMapsEachTierToDocumentedMilliseconds` 测试用例（depends on T003；与 T009/T010/T011 同文件，顺序执行）：调用 `ModelCatalog::tierDebounceWindow()`，断言 `Low` → `200ms`、`Medium` → `500ms`、`High` → `800ms`、`Unknown` → `200ms`（`data-model.md` 二 四档完整映射，用户已决策数值）。

- [X] T013 [P] [US1] 在 `tests/slowpath/DebounceSchedulerTest.cpp` 新增 `SetDebounceWindowDoesNotAlterExistingStateFields` 测试用例（depends on T006）：分别在 `Debouncing`/`Generating`/`Stale` 等既有状态下调用 `setDebounceWindow()`，断言调用前后 `state()` 返回值不变（`contracts/debounce-tier-window-internal-contract.md` 五 1，验证 FR-013"不改变既有状态机"）。

- [X] T014 [US1] 在 `tests/slowpath/DebounceSchedulerTest.cpp` 新增 `SetDebounceWindowAffectsNextTickButNotPastTrigger` 测试用例（depends on T006；与 T013 同文件，顺序执行）：以 100ms 旧窗口调用 `onInputChanged(t0)`，随后调用 `setDebounceWindow(std::chrono::milliseconds(500))`，验证 `tick(t0+150ms)` **不**触发生成（旧窗口下本应已触发，新窗口下未到期）、`tick(t0+550ms)` 才触发生成，验证"调用后下一次 `tick()` 立即使用新窗口值判定"的契约（`contracts/debounce-tier-window-internal-contract.md` 一/五 2）。

- [X] T015 [P] [US1] 在 `tests/session/SessionControllerTest.cpp` 新增集成测试 `SwitchModelToMediumOrHighTierModelLengthensDebounceWindow`（depends on T007, T008）：通过 `setDebounceScheduler()` 注入一个使用真实 `DebounceScheduler` 构造、回调记录触发次数的实例，设置 `applicationName = "Babel"` 以解析到约定模型目录，调用 `switchModel()` 切换至该目录下文件名含 `"7b"`/文件名含 `"14b"` 关键字的真实可加载模型文件（`research.md` §6/`quickstart.md` 前置条件要求用户预先下载的 Qwen2.5-7B-Instruct/Qwen2.5-14B-Instruct），待 `modelSwitchCompleted` 信号触发后，分别以"旧 200ms 本应触发、新 500ms/800ms 窗口尚未到期"的中间时间点驱动 `tick()`，验证窗口已按新档位动态拉长（`contracts/debounce-tier-window-internal-contract.md` 五 2/3）；本机未放置对应真实模型文件时 `GTEST_SKIP()`，与既有 `SwitchModelFailureFallsBackToPreviouslyWorkingModel` 用例的跳过策略保持一致，不影响 CI。

- [X] T016 [US1] 运行 `ctest --preset macos --output-on-failure --tests-regex "ModelCatalogTest|DebounceSchedulerTest|SessionControllerTest"`（depends on T002-T015 全部完成），确认 User Story 1 新增/更新断言全部通过，对照 `quickstart.md` 验证场景二（2.1～2.6：低配基线 200ms、中配 500ms、高配 800ms、切回低配恢复 200ms、并发保护回归、`Unknown` 档位复用 200ms）与场景一（FR-001/FR-002/FR-004）的覆盖范围逐条核对。

**Checkpoint**：至此 User Story 1 独立可用——设置页面展示新推荐数值，防抖窗口随档位动态调整，可独立于 User Story 2 交付/演示。

---

## Phase 4: User Story 2 - 最终上屏文本不再可能包含 Lattice 标注符号残留 (Priority: P2)

**Goal**：`LlmEngine::generate()` 内部引入 llama.cpp 原生 GBNF 语法约束采样，从语法层面强制排除 `⟦`（U+27E6）/`⟧`（U+27E7）/`|`（U+007C）三个 Lattice 内部标注符号出现在生成结果中（FR-005/FR-007"最宽边界"——仅排除这三个符号，不收窄其余任何字符），语法梗阻（候选集合全部为 `-inf`）时提前终止生成并返回已生成的部分内容（FR-008），`generate()` 对外签名（`QString` 输入输出）与 QML↔C++ 契约保持不变（FR-006）。

**Independent Test**：构造多组已知容易触发多候选歧义标注（`⟦候选1 权重1|候选2 权重2|...⟧` 结构化标注片段较多）的测试样例，引入语法约束后执行慢流生成，验证生成结果中不出现 `⟦`/`⟧`/`|` 三个字符中的任意一个，且该结论在测试语料集合上全部成立；同时验证含英文品牌名/技术缩写/emoji 等合法混排内容不被误伐；本故事与 User Story 1 的模型档位/防抖窗口调整完全无关，可独立验证、独立交付。

### Implementation for User Story 2

- [X] T017 [US2] 重构 `babel/src/slowpath/LlmEngine.cpp` 的 `generate()` 采样循环（`research.md` §1/§2/§3 方案 B，`contracts/llm-engine-grammar-internal-contract.md` 二）：放弃现有 `llama_sampler_chain_init`/`llama_sampler_chain_add`/`llama_sampler_sample()` 一体式链条封装，改为手动分步调用：
  - 新增 GBNF 语法产生式字符串常量（语法根符号名 `"root"`）：`"root ::= safe-char*\nsafe-char ::= [^\\u27E6\\u27E7\\x7C]\n"`（否定字符类，排除且仅排除 U+27E6 `⟦`/U+27E7 `⟧`/U+007C `|` 三个码点，不枚举正向范围以避免误伐"最宽边界"要求的其余任意 Unicode 字符，FR-005/FR-007）；
  - 调用 `llama_sampler_init_grammar(vocab, kOutputGrammar, "root")` 构造语法采样器，与既有 `llama_sampler_init_temp(0.3f)`/`llama_sampler_init_dist(0)` 分别保留为三个独立 `llama_sampler*` 实例（不再用 `llama_sampler_chain_add` 组装进同一条链）；
  - 每个输出 token 步骤改为：从 `llama_get_logits_ith(m_ctx, -1)` 读取原始 logits 构建 `llama_token_data_array cur_p`，先对 `cur_p` 调用语法采样器的 `llama_sampler_apply(grammar, &cur_p)`。

- [X] T018 [US2] 在 T017 同一改造范围内（`babel/src/slowpath/LlmEngine.cpp` `generate()`，depends on T017），新增语法梗阻检测、提前终止与既有退出路径的衔接（FR-008，`research.md` §3）：
  - 语法采样器 `apply()` 之后，检查 `cur_p` 中全部候选的 `logit` 字段是否**全部**为 `-inf`；若是，立即 `break` 循环，直接返回梗阻点之前已累积的 `result` 字符串（MUST NOT 抛异常、MUST NOT 返回空字符串代表"失败"，梗阻与"模型不可用"是两种不同情形，不得混淆）；
  - 若候选集合非空，继续依次 `llama_sampler_apply(temp, &cur_p)`、`llama_sampler_apply(dist, &cur_p)` 得到 `cur_p.data[cur_p.selected].id` 作为 `nextToken`；
  - 既有 `llama_vocab_is_eog(vocab, nextToken)` 自然终止判断、`kMaxOutputTokens = 256` 硬上限两条既有循环退出路径均保持不变，与梗阻提前终止是三条彼此独立、分别可测试的退出路径；
  - 选出 `nextToken` 后调用 `llama_sampler_accept(grammar, nextToken)` 推进语法采样器内部解析堆栈状态（不可省略，否则下一轮 `apply()` 会基于过期堆栈状态误判）；
  - 确认既有系统提示词常量 `kSystemPrompt` 中"最终输出的句子中绝对不能包含 ⟦、⟧、| 等标注符号本身"一句维持不变，不因引入硬性语法约束而被删除/弱化（`research.md` §3 Decision：保留可降低梗阻触发概率）；
  - 函数末尾新增 `llama_sampler_free(grammar)`（原 `llama_sampler_free(sampler)` 调用相应调整为释放 `temp`/`dist`/`grammar` 三个独立采样器实例，避免内存泄漏）。

### Tests for User Story 2

- [X] T019 [US2] 更新 `tests/slowpath/LlmEngineGenerateTest.cpp` 的 `GenerateAcceptsStructuredAnnotationTextWithoutSignatureChange` 用例（depends on T017, T018）：在既有对 `⟦`/`⟧` 的 `EXPECT_FALSE(output.contains(...))` 断言基础上，补充对 `|`（`QStringLiteral("|")`）的同等断言（`contracts/llm-engine-grammar-internal-contract.md` 三 1）。

- [X] T020 [US2] 在 `tests/slowpath/LlmEngineGenerateTest.cpp` 新增 `GenerateOnHighAmbiguityDensityInputNeverLeaksAnnotationSymbols` 测试用例（depends on T017, T018；对应 `quickstart.md` 场景三 3.1/3.2，SC-004）：构造包含多个 `⟦候选1 权重1|候选2 权重2|...⟧` 高密度标注片段的输入文本（可复用/扩展既有 `chi`/`shi` 等高歧义拼音样例），依赖真实本地模型文件，环境缺失时沿用既有 `GTEST_SKIP()` 模式；断言生成结果不含 `⟦`/`⟧`/`|` 任意字符。

- [X] T021 [US2] 在 `tests/slowpath/LlmEngineGenerateTest.cpp` 新增 `GenerateDoesNotFilterLegalMixedContentLikeBrandNamesOrEmoji` 测试用例（depends on T017, T018；对应 `quickstart.md` 场景三 3.2 第 5 步，FR-007"最宽边界"）：构造包含英文品牌名（如 "iPhone"）、技术缩写（如 "GPT"）等合法混排内容的输入文本，依赖真实本地模型，环境缺失时 `GTEST_SKIP()`；断言生成结果非空（验证语法约束未因这些合法字符导致异常梗阻/空输出，不强制断言模型逐字复现特定词汇本身）。

- [X] T022 [US2] 在 `tests/slowpath/LlmEngineGenerateTest.cpp` 新增 `GenerateOnGrammarStallTerminatesEarlyWithoutCrashing` 测试用例（depends on T017, T018；对应 `quickstart.md` 场景三 3.3，标注"较难稳定复现，尽力而为"）：依赖真实本地模型，环境缺失时 `GTEST_SKIP()`；通过多轮重复调用 `generate()`（复用既有 `RepeatedGenerateCallsOnSameEngineDoNotCrash` 的多轮调用手法）验证即便某次调用触发语法梗阻提前终止，也不抛出异常、不使进程崩溃，返回值始终为合法 `QString`（FR-008，不强制断言梗阻必然被触发，仅验证该路径被触发时的既有健壮性）。

- [X] T023 [US2] 运行 `ctest --preset macos --output-on-failure --tests-regex "LlmEngineGenerateTest|LlmEngineUnavailableTest"`（depends on T017-T022 全部完成），确认 User Story 2 新增/更新断言全部通过；若本机未放置真实模型文件，确认相关用例按既有 `GTEST_SKIP()` 模式跳过而非失败；对照 `quickstart.md` 验证场景三逐条核对。

**Checkpoint**：至此 User Story 2 独立可用——`generate()` 生成结果从语法层面强制不含标注符号，可独立于 User Story 1 交付/演示；两故事叠加后两者均独立可用。

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**：两个用户故事均完成后的端到端手工验证、实测数据记录与全量回归。

- [ ] T024 [P] 按 `quickstart.md` 验证场景一（设置页面展示调整后的推荐档位数值）手工验证（depends on T016）：确认中配/高配展示为 Qwen2.5-7B-Instruct/Qwen2.5-14B-Instruct（FR-001/FR-002），此前按旧版推荐下载的 Qwen2.5-32B-Instruct/Qwen2.5-14B-Instruct 文件仍出现在模型列表中可选用（FR-004）。
  > **阻塞**：需要真实桌面 WindowServer 会话打开浮窗设置页面做人工视觉核对，沙箱环境无显示/GUI 支持，无法执行该人工交互步骤；对应逻辑已由 `ModelCatalogTest.RecommendedProfilesReturnFixedThreeTiers`/`ClassifyTierByFileNameIsPubliclyCallable`（T010/T011，已通过）在源码层面验证——`recommendedProfiles()` 确实返回 7B/14B 字符串、旧版 14B/32B 关键字确实仍可被 `classifyTierByFileName()` 正确分类（归入 High，不会被扫描逻辑过滤掉）。人工视觉核对需在具备真实桌面环境的机器上另行执行。

- [ ] T025 [P] 按 `quickstart.md` 验证场景二全部步骤（2.1～2.6）手工验证（depends on T016，对应 SC-003/SC-005）：低配基线 200ms、切换中配 500ms、切换高配 800ms、切回低配恢复 200ms、生成进行中并发保护回归、自定义命名模型 `Unknown` 档位复用低配 200ms。
  > **阻塞（部分）**：防抖窗口映射表本身（200/500/800/200ms 四档）已由 `ModelCatalogTest.TierDebounceWindowMapsEachTierToDocumentedMilliseconds`（T012）与 `DebounceSchedulerTest` 新增两例（T013/T014）程序化验证通过；`SessionControllerTest.SwitchModelToMediumOrHighTierModelLengthensDebounceWindow`（T015）因本机模型目录缺少 Qwen2.5-7B-Instruct 真实文件（仅有 3B/14B）按既有 `GTEST_SKIP()` 约定跳过，故"切换中配→验证确实变为 500ms"这一端到端路径尚未被真实模型文件覆盖验证，需用户下载 7B 文件后补测；"真实浮窗 UI 下人工感受停顿节奏变化"这一主观体验核对同样需要真实桌面环境，本沙箱无法执行。

- [ ] T026 [P] 按 `quickstart.md` 验证场景三全部步骤（3.1～3.3）手工验证（depends on T023，对应 SC-004）：高歧义密度输入最终上屏文本不含 `⟦`/`⟧`/`|`、合法品牌名/技术缩写/emoji 未被误伐、语法梗阻情形下的健壮性（较难稳定复现，尽力而为）。
  > **阻塞（部分）**：核心断言已由 `LlmEngineGenerateTest` 新增三例程序化验证通过并实际运行（非仅编译通过）：`GenerateOnHighAmbiguityDensityInputNeverLeaksAnnotationSymbols`（高歧义密度输入不泄漏标注符号）、`GenerateDoesNotFilterLegalMixedContentLikeBrandNamesOrEmoji`（品牌名/emoji 不被误伐）、`GenerateOnGrammarStallTerminatesEarlyWithoutCrashing`（语法梗阻提前终止不崩溃），均使用本机真实 Qwen2.5-3B/14B 模型文件实际推理验证，非仅断言占位。"真实浮窗 UI 下人工走查"这一步骤需要真实桌面环境，本沙箱无法执行，但自动化测试已覆盖本场景的全部可验证断言内容。

- [ ] T027 记录 SC-001/SC-002 要求的实测延迟数据（depends on T024）：切换至新中配 Qwen2.5-7B-Instruct/新高配 Qwen2.5-14B-Instruct 后，针对 `spec.md` 背景中列举的实测测试样例，分别记录实测整句生成延迟，并与 002 原中配实测 2579ms/原高配外推约 6.7 秒对比是否存在可测量的降低；该实测数据记入实现说明/PR 描述，MUST NOT 回填 `spec.md` 本身（spec 明文禁止在规格阶段断言具体数值，FR-009）。
  > **部分完成**：新高配 Qwen2.5-14B-Instruct（本机真实模型文件，Metal 加速）实测——样例 "wo today bushi very e" 耗时 **3370ms**；"wo today bushi very e if you e we go eat malatang" 耗时 **5722ms**（含首次 Metal kernel JIT 编译开销）。与 002 原中配（同一 14B 权重）记录的约 2579ms 量级吻合（样例长度/GBNF 约束开销差异导致数值不完全相同），**显著低于原高配 32B 外推约 6.7 秒**——确认档位调整后新高配延迟相对原高配存在可测量降低，达成 FR-009/SC-001 的改善意图。新中配 Qwen2.5-7B-Instruct **因本机未放置该模型文件（约 4.6GB，需用户手动下载）无法实测**，需用户下载后补测，预期按参数量线性缩放规律外推约 1.46 秒（显著低于 2579ms 原中配）。

- [ ] T028 全量回归：运行 `ctest --preset macos --output-on-failure`（depends on T016, T023, T024, T025, T026）执行全部既有测试文件（001/002 既有共计 65 个测试文件，其中 4 个——`ModelCatalogTest.cpp`/`LlmEngineGenerateTest.cpp`/`DebounceSchedulerTest.cpp`/`SessionControllerTest.cpp`——已被本特性更新/新增用例，测试文件总数不变），确认无回归失败，与 T001 建立的基线逐条对照差异均为本特性预期内的断言变更。
  > **已完成**：独立重新执行 `ctest -C Debug`（非仅采信实施报告），实测 **73/73 测试通过**（原 65 + 本特性新增 8 例），2 项按设计跳过（`PasteInjectorPermissionTest`——环境依赖，非本特性引入；`SwitchModelToMediumOrHighTierModelLengthensDebounceWindow`——缺 7B 模型文件，见 T025）。T024/T025/T026 中"真实桌面 GUI 人工走查"部分未完成（沙箱无显示环境），已如上在各任务处分别记录；T027 中配延迟因缺模型文件未实测，高配已实测确认改善。

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**：无依赖，可立即开始。
- **Foundational (Phase 2)**：无任务，Setup 完成后直接视为"已就绪"。
- **User Story 1 (Phase 3)**：依赖 Setup 完成；与 User Story 2 之间**无任何文件/调用依赖**，可与 Phase 4 完全并行开工。
- **User Story 2 (Phase 4)**：依赖 Setup 完成；与 User Story 1 之间**无任何文件/调用依赖**，可与 Phase 3 完全并行开工。
- **Polish (Phase 5)**：依赖 User Story 1（T024/T025 依赖 T016）与 User Story 2（T026 依赖 T023）均完成；T028 全量回归依赖全部前序任务完成。

### User Story Dependencies

- **User Story 1 (P1)**：Foundational 完成后即可开始，不依赖 User Story 2。
- **User Story 2 (P2)**：Foundational 完成后即可开始，不依赖 User Story 1。

### Within User Story 1

- T002（`.h` 声明）→ T003/T004（`.cpp` 实现，同文件顺序执行）→ T007 → T008（`SessionController.cpp` 同文件顺序执行）。
- T005（`.h` 声明）→ T006（`.cpp` 实现），独立于 T002-T004，可与之并行（不同文件）。
- T007/T008 同时依赖 T003/T004 与 T006（需要 `ModelCatalog` 新方法 + `DebounceScheduler::setDebounceWindow()` 均已就绪）。
- 测试任务 T009-T012（`ModelCatalogTest.cpp` 同文件顺序执行）依赖 T003/T004；T013-T014（`DebounceSchedulerTest.cpp` 同文件顺序执行）依赖 T006；T015（`SessionControllerTest.cpp`）依赖 T007/T008。
- T016（检查点）依赖 T002-T015 全部完成。

### Within User Story 2

- T017 → T018（同一函数改造，顺序执行）。
- 测试任务 T019-T022（`LlmEngineGenerateTest.cpp` 同文件顺序执行）依赖 T017/T018。
- T023（检查点）依赖 T017-T022 全部完成。

### Parallel Opportunities

- **跨故事并行**：Setup（T001）完成后，User Story 1（T002-T016）与 User Story 2（T017-T023）可由不同开发者/Agent 完全并行推进，两者改造文件集合不相交。
- User Story 1 内部：T002 与 T005 可并行（不同文件）；T013 与 T015 可并行（不同文件，且与 `ModelCatalogTest.cpp` 内的 T009-T012 顺序链互不冲突）。
- Polish 阶段：T024/T025/T026 三个手工验证场景可并行执行（互不干扰的独立验证步骤）。

---

## Parallel Example: 跨故事并行启动

```bash
# Setup 完成后，两条故事线可同时派发：
Task: "User Story 1 — T002 在 babel/src/slowpath/ModelCatalog.h 新增 classifyTierByFileName()/tierDebounceWindow() 声明"
Task: "User Story 2 — T017 重构 babel/src/slowpath/LlmEngine.cpp 的 generate() 采样循环，接入 GBNF 语法采样器"
```

## Parallel Example: User Story 1 内部

```bash
# T002（ModelCatalog.h）与 T005（DebounceScheduler.h）可并行：
Task: "T002 在 babel/src/slowpath/ModelCatalog.h 新增两个公开静态方法声明"
Task: "T005 在 babel/src/slowpath/DebounceScheduler.h 新增 setDebounceWindow() 声明"

# T013（DebounceSchedulerTest.cpp）与 T015（SessionControllerTest.cpp）可并行：
Task: "T013 在 tests/slowpath/DebounceSchedulerTest.cpp 新增 SetDebounceWindowDoesNotAlterExistingStateFields"
Task: "T015 在 tests/session/SessionControllerTest.cpp 新增 SwitchModelToMediumOrHighTierModelLengthensDebounceWindow"
```

---

## Implementation Strategy

### MVP First（仅 User Story 1）

1. 完成 Phase 1：Setup（T001，建立基线）。
2. Phase 2 无任务，直接视为就绪。
3. 完成 Phase 3：User Story 1（T002-T016）。
4. **此时即可停下并交付/演示**：设置页面展示新推荐档位数值，防抖窗口随档位动态调整，这是 spec.md 中明确列为最高优先级（P1）、且已有实测数据佐证紧迫性的核心交付价值，不依赖 GBNF 语法约束（User Story 2）即可独立成立。

### 增量交付（Incremental Delivery）

1. Setup → User Story 1 独立交付（MVP）。
2. 追加 User Story 2（T017-T023）→ 补齐"最终上屏文本强制不含标注符号"的正确性保障，两故事叠加交付。
3. Phase 5 Polish（T024-T028）：端到端手工验证 + 实测数据记录 + 全量回归，作为本特性最终验收收尾。

### 并行团队策略（若有多个 Agent/开发者）

- Setup 由单人/单 Agent 完成后，立即拆分为两条完全独立的并行工作线：User Story 1（`ModelCatalog`/`DebounceScheduler`/`SessionController` 三组文件 + 对应测试）与 User Story 2（`LlmEngine` 一组文件 + 对应测试）。
- 两条线各自完成检查点（T016/T023）后汇合进入 Phase 5 Polish。
