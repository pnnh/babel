---

description: "Task list for 004-speculative-decoding"
---

# Tasks: 投机解码（Speculative Decoding）性能优化

**Input**: Design documents from `/specs/004-speculative-decoding/`

**Prerequisites**: plan.md（已完成，Constitution Check PASS 无条件通过，含用户对"方案 B"与"方案①"两项架构决策的显式确认）、spec.md（已完成全部 4 项澄清）、research.md、data-model.md、contracts/、quickstart.md（均已完成）

**Tests**: 本特性**包含**测试任务——`plan.md` Technical Context"Testing"小节与 `contracts/*.md` 明确要求对既有 3 个测试文件（`ModelCatalogTest.cpp`/`LlmEngineGenerateTest.cpp`/`SessionControllerTest.cpp`）做断言新增，`LlmEngineUnavailableTest.cpp` 按需补充回归用例。全部任务均为"在既有测试文件内新增用例"，**不新增任何测试文件**（与本特性"不新增任何新源文件"的整体约束一致，GLOB 自动发现 + CMake POST_BUILD ad-hoc codesign 基础设施无需改动，`research.md` §8）。

**语言（宪章《语言与文档规范》）**：任务描述使用简体中文；文件路径、符号名、命令保留英文。

**范围重申（`plan.md` Scale/Scope，`research.md` §3 用户已决策采纳方案 B）**：本特性严格限定于以下 3 组既有文件 + 4 个既有测试文件 + 1 个既有 QML 文件，**不新增任何新源文件/头文件/QML 文件**：

```text
babel/src/slowpath/ModelCatalog.{h,cpp}
babel/src/slowpath/LlmEngine.{h,cpp}
babel/src/session/SessionController.{h,cpp}
babel/content/SettingsPanel.qml

tests/slowpath/ModelCatalogTest.cpp
tests/slowpath/LlmEngineGenerateTest.cpp
tests/slowpath/LlmEngineUnavailableTest.cpp
tests/session/SessionControllerTest.cpp
```

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 可并行执行（不同文件、无依赖关系）
- **[Story]**: 所属用户故事（US1/US2/US3）；Setup/Foundational/Polish 阶段任务不标注

---

## Phase 1: Setup（基线确认）

**Purpose**: 在改动前建立"全绿基线"，便于后续断言变更前后对照，不引入任何新依赖/新构建目标。

- [X] T001 运行既有完整 CTest 套件建立变更前基线：`cmake --build --preset macos` 后执行 `ctest --preset macos --output-on-failure`，确认 001/002/003 既有全部 73 个测试文件中的用例（依赖真实模型文件的用例按既有 `GTEST_SKIP()` 约定可跳过）处于可对照的基线状态，记录通过/跳过清单，供后续核对本特性新增用例是否按预期从 FAIL 变为 PASS（而非破坏既有断言）。

  > **执行记录**：基线确认通过本特性 Polish 阶段（T027）的全量回归间接完成——全部 86 个用例（73 既有 + 本特性新增 13 个）100% 通过，2 个跳过（`PasteInjectorPermissionTest.UngrantedAccessibilityYieldsPermissionDenied` 需沙箱外真实 Accessibility 权限授予；`SessionControllerTest.SwitchModelToMediumOrHighTierModelLengthensDebounceWindow` 因 Qwen2.5-7B-Instruct 仍在下载中而跳过，均与本特性改动无关），与既有基线预期一致，未发现回归。未在改动前单独执行一次"纯基线"运行（而是改动与验证交替进行），如实记录此流程偏差，不影响最终结论。

**Checkpoint**：基线已记录，可开始 Foundational 阶段。

---

## Phase 2: Foundational（跨故事阻塞性前置工作）

**Purpose**：`ModelCatalog` 新增的草稿模型识别/推荐/搭配映射能力是 User Story 1（`SessionController` 需要查表得到草稿模型路径传给 `LlmEngine` 构造函数）、User Story 2（设置页面展示 `recommendedDraftProfiles` 候选规格）、User Story 3（计算"是否已配置"用于 `speculativeDecodingNoticeText` 提示文案）三者共同依赖的基础设施，**MUST** 先于全部三个用户故事完成。

**⚠️ CRITICAL**: 本阶段完成前，三个用户故事均无法开始实施。

- [X] T002 [P] 在 `babel/src/slowpath/ModelCatalog.h` 新增（`data-model.md` 一/二，`contracts/model-catalog-draft-pairing-internal-contract.md` 一）：
  - `ModelDescriptor` 内新增并列枚举 `enum class DraftCandidate { None, Size0_5B, Size1_5B };`（与既有 `TierHint`/`Availability` 并列，不改动后两者任何既有分支）；
  - 新增结构体 `RecommendedDraftModelProfile { DraftCandidate size; QString modelName; QString quantization; QString approxSizeDescription; };`（与既有 `RecommendedModelProfile` 并列、结构同构，枚举类型独立不复用）；
  - `ModelCatalog` 类内新增 3 个公开静态方法声明：`static ModelDescriptor::DraftCandidate classifyDraftCandidateByFileName(const QString &fileName);`、`static std::vector<RecommendedDraftModelProfile> recommendedDraftProfiles();`、`static ModelDescriptor::DraftCandidate pairedDraftCandidate(ModelDescriptor::TierHint targetTier);`；
  - 既有 `scan()`/`recommendedProfiles()`/`classifyTierByFileName()`/`tierDebounceWindow()` 签名与既有枚举均不变。

- [X] T003 在 `babel/src/slowpath/ModelCatalog.cpp` 实现 T002 新增的 3 个方法（depends on T002，`research.md` §5.1/§5.2/§5.3）：
  - `classifyDraftCandidateByFileName(fileName)`：文件名（不区分大小写）含 `"0.5b"`/`"0-5b"` 子串 → `Size0_5B`；含 `"1.5b"`/`"1-5b"` 子串 → `Size1_5B`；均不匹配 → `None`；内部可复用既有 `toLower()` 匿名命名空间辅助函数，**不得**修改 `classifyTierByFileName()` 既有四个分支（`research.md` §5.1 已核实两者匹配子串互不冲突）；
  - `recommendedDraftProfiles()`：固定返回两条静态数据——`Size0_5B`："Qwen2.5-0.5B-Instruct"、"GGUF Q4_K_M"；`Size1_5B`："Qwen2.5-1.5B-Instruct"、"GGUF Q4_K_M"（`approxSizeDescription` 按实际 GGUF 文件大小填入合理估算值，不编造夸张数值）；
  - `pairedDraftCandidate(targetTier)`：按用户已决策的方案①查表（`research.md` §5.3"用户决策"，`contracts/model-catalog-draft-pairing-internal-contract.md` 三）——`Low`/`Unknown` → `DraftCandidate::None`；`Medium` → `DraftCandidate::Size0_5B`；`High` → `DraftCandidate::Size1_5B`；**MUST NOT** 考虑候选方案②～④中的其余取值。

- [X] T004 [P] 在 `tests/slowpath/ModelCatalogTest.cpp` 新增测试用例（depends on T003，`contracts/model-catalog-draft-pairing-internal-contract.md` 二）：
  - `ClassifyDraftCandidateByFileNameMatchesDocumentedKeywords`：断言 `"qwen2.5-0.5b-instruct-q4_k_m.gguf"` → `Size0_5B`，`"qwen2.5-1.5b-instruct-q4_k_m.gguf"` → `Size1_5B`，`"qwen2.5-7b-instruct-q4_k_m.gguf"`/`"mystery-model.gguf"` → `None`；
  - `ClassifyDraftCandidateByFileNameDoesNotInterfereWithClassifyTierByFileName`：对同一组文件名分别调用两个方法，断言既有 `classifyTierByFileName()` 对 `"3b"`/`"7b"`/`"14b"`/`"32b"` 四档分类结果不因新方法的引入而改变（回归断言，`research.md` §5.1）；
  - `RecommendedDraftProfilesReturnsFixedTwoCandidates`：断言返回恰好 2 条数据，`size`/`modelName` 分别对应 `Size0_5B`/"Qwen2.5-0.5B-Instruct" 与 `Size1_5B`/"Qwen2.5-1.5B-Instruct"；
  - `PairedDraftCandidateMapsLowToNoneMediumToSize0_5BHighToSize1_5B`：断言 `pairedDraftCandidate(Low) == None`、`pairedDraftCandidate(Medium) == Size0_5B`、`pairedDraftCandidate(High) == Size1_5B`（`contracts/model-catalog-draft-pairing-internal-contract.md` 二.4/二.5）。

- [X] T005 运行 `ctest --preset macos --output-on-failure --tests-regex "ModelCatalogTest"`（depends on T004），确认 T002/T003/T004 新增内容全部通过，且既有 `ModelCatalogTest` 用例（`ScanEnumeratesAllGgufFilesInConventionalDirectory`/`TierHintClassifiesByFilenameKeyword` 等）无回归。

**Checkpoint**：Foundational 就绪——`ModelCatalog` 草稿模型识别/推荐/搭配映射能力已可用，三个用户故事均可开始独立实施。

---

## Phase 3: User Story 1 - 中配/高配档位用户在不牺牲翻译质量前提下获得更快的生成速度 (Priority: P1) 🎯 MVP

**Goal**：`LlmEngine` 内部封装双模型投机解码逻辑（方案 B，对 `SessionController` 透明）：构造函数扩展接受可选草稿模型路径，内部手写"草稿生成→批量验证→比较截断"循环（`research.md` §2 四步循环），GBNF 语法约束同时应用于草稿/目标两侧且状态同步（`research.md` §6）；`SessionController` 在 `switchModel()`/`bootstrapPlatformDefaults()` 中查表得到草稿模型路径一并传给 `LlmEngine` 构造函数；低配档位恒不触发任何草稿模型加载尝试（FR-005/FR-009）。

**Independent Test**：在设置页面切换为中配或高配模型，并在已正确配置可用草稿模型的前提下，使用同一测试样例分别执行"启用投机解码"与"不启用投机解码"两种路径的生成，比较二者的整句生成延迟与生成文本内容，验证延迟有可测量的降低、生成文本内容在语义上无差异；切换回低配默认档位时系统行为与既有单模型路径完全一致。

### Implementation for User Story 1

- [X] T006 [P] [US1] 在 `babel/src/slowpath/LlmEngine.h` 扩展构造函数签名与私有状态（`contracts/llm-engine-speculative-internal-contract.md` 四，`data-model.md` 五）：`explicit LlmEngine(std::filesystem::path modelPath, std::filesystem::path draftModelPath = {});`（新增参数带默认空路径，使 `tests/slowpath/LlmEngineUnavailableTest.cpp` 与 `tests/slowpath/LlmEngineGenerateTest.cpp` 现有单参数调用点无需修改即可继续编译通过，符合宪章原则 V"最小必要修改"）；新增私有成员 `std::filesystem::path m_draftModelPath;`、`bool m_draftModelAvailable = false;`、`llama_model *m_draftModel = nullptr;`、`llama_context *m_draftCtx = nullptr;`；`generate()` 对外签名（`QString generate(const QString&) const`）**不变**（FR-002）。

- [X] T007 [US1] 在 `babel/src/slowpath/LlmEngine.cpp` 的 `tryLoadModel()` 中扩展草稿模型加载逻辑（depends on T006，`contracts/llm-engine-speculative-internal-contract.md` 二.1/二.4）：目标模型加载成功后，若 `m_draftModelPath` 非空，尝试加载草稿模型（文件存在性/大小校验 + `llama_model_load_from_file()`/`llama_init_from_model()`，复用目标模型加载的既有校验模式）；任一环节失败（路径为空、文件不存在/为空/损坏、加载失败）时 `m_draftModel`/`m_draftCtx` 保持 `nullptr`、`m_draftModelAvailable = false`，仅记录 `spdlog::warn` 诊断日志，**不得**影响目标模型本身已成功加载的 `m_modelAvailable = true` 结果（FR-006 静默降级）；目标模型本身加载失败时维持既有行为不变，**不尝试**加载草稿模型（避免低配/目标模型本身不可用场景下的额外开销）。

- [X] T008 [US1] 在 `babel/src/slowpath/LlmEngine.cpp` 的 `releaseModel()` 中对称扩展草稿模型资源释放（depends on T006, T007）：析构顺序与既有目标模型释放一致（先 `llama_free(m_draftCtx)` 后 `llama_model_free(m_draftModel)`，均判空后置 `nullptr`），不影响既有目标模型释放逻辑的既定顺序与条件判断。

- [X] T009 [US1] 重构 `babel/src/slowpath/LlmEngine.cpp` 的 `generate()` 为分支结构（depends on T007, T008，`contracts/llm-engine-speculative-internal-contract.md` 二.4）：在既有"提示词 tokenize + 首次 `llama_decode()`"之后，依据 `m_draftModelAvailable` 分支——为 `false` 时执行**既有单 token 自回归采样循环逻辑原样不变**（当前 `generate()` 主体，含 GBNF 语法梗阻检测、`kMaxOutputTokens` 上限、EOG 终止）；为 `true` 时进入 T010 新增的投机解码分支。两分支共享的提示词模板拼接、tokenize、GBNF 语法规则字符串（`kOutputGrammar`）常量**不重复定义**。

- [X] T010 [US1] 在 `babel/src/slowpath/LlmEngine.cpp` 实现投机解码分支（depends on T009，`research.md` §2 四步循环 + §6 GBNF 双侧约束 + §7 固定窗口大小）：
  - 对草稿模型独立执行提示词 tokenize（使用草稿模型自身 `llama_vocab`）与首次 `llama_decode()`，建立草稿模型自身 KV cache 上下文；
  - 固定内部常量 `K`（草稿 token 数量，如 `constexpr int kSpeculativeDraftWindow = 4;`，类比既有 `kMaxOutputTokens` 风格，`research.md` §7：不实现自适应调整）；
  - **草稿阶段**：草稿模型自回归生成 `K` 个候选 token（复用既有单 token 采样模式，使用草稿模型**独立的** `llama_sampler_init_grammar()` 实例，语法规则字符串与目标侧相同）；
  - **批量验证阶段**：使用 `llama_batch_init()` 构造含 `K` 个草稿候选 token 的批次，各 token `logits` 标志位置 1，对目标模型一次性调用 `llama_decode()`；随后对每个位置调用 `llama_get_logits_ith()`，经目标模型**独立的** `llama_sampler_init_grammar()` 实例 + `temp`/`dist` 采样器决定目标模型在该位置的"真实"选择（语法梗阻检测逻辑与既有单模型路径一致）；
  - **比较与截断阶段**：从位置 0 起逐一比较草稿 token 与目标模型真实选择，找到首个不同位置 `j` 后停止；位置 `[0, j)` 的草稿 token 直接采纳，位置 `j` 使用目标模型真实采样结果替换；通过 `llama_memory_seq_rm()` 回滚 KV cache 中位置 `j` 及其后的失效草稿 token，`llama_memory_seq_pos_min/max()` 重新计算下一轮起始 `pos`；
  - **双侧语法采样器状态同步**（`research.md` §6）：无论某位置 token 最终是"草稿采纳"还是"目标替换"，**两侧**语法采样器均须对该最终确定的 token 调用一次 `llama_sampler_accept()`，保证草稿模型下一轮猜测时内部语法堆栈状态与已生成序列一致；
  - 循环直至遇到 EOG 或达到既有 `kMaxOutputTokens` 上限（与既有单模型路径共享同一上限常量）；函数末尾正确 `llama_sampler_free()` 全部新增的草稿侧采样器实例，避免内存泄漏。

  > **实现记录（重要缺陷发现与修复）**：初版实现按"批次内位置 i 的输出 logits 直接验证草稿候选 d_i"编码，经真实 Qwen2.5-14B-Instruct + Qwen2.5-1.5B-Instruct 模型实测，发现投机解码路径产出的文本"词汇本身合法但顺序错乱/重复"（如"我不是不太今天我很能你们一起项目我们需要下午你们讨论项目的和的下一步计划确认所有的进度下一步"），与单模型路径的正确输出（"我今天不是非常忙，很高兴能和你们一起讨论这个项目的技术方案，我们需要在下午的会议中确认所有项目的进度和下一步的计划。"）明显不一致。经排查定位为批量验证阶段的 logits 索引偏移错误：`llama_decode()` 对批次内位置 i（已输入草稿 token d_i）产出的 logits 预测的是"紧跟在 d_0..d_i 之后"的下一个 token，而非"d_i 本身是否合理"——验证 d_0 须使用本轮验证之前（上一轮末尾或首次提示词解码）已产出的 logits，验证 d_i（i>=1）须使用批次内位置 i-1 的输出 logits。已修正该索引映射（`sampleTargetChoice` lambda + 位置 0 单独用"验证前"logits、位置 1..k-1 用批次内 i-1 偏移索引），修复后复测确认投机解码路径与单模型路径产出逐字完全一致的文本，FR-001"语义等价"得到真实模型验证（非仅结构性/占位测试）。详见 `quickstart.md`"实测记录"小节与代码注释。

- [X] T011 [P] [US1] 在 `babel/src/session/SessionController.h` 新增私有辅助方法声明（`data-model.md` 三"具体宿主位置"）：`std::filesystem::path pairedDraftModelPathForTier(babel::slowpath::ModelDescriptor::TierHint tier) const;`（非 QML 可见，供 `switchModel()`/`bootstrapPlatformDefaults()`/后续 US3 的提示文案计算共同调用，避免逻辑重复）；不改动任何既有公开 `Q_PROPERTY`/`Q_INVOKABLE`/信号签名（FR-002）。

- [X] T012 [US1] 在 `babel/src/session/SessionController.cpp` 实现 `pairedDraftModelPathForTier()`（depends on T011, T003）：`tier` 为 `Low`/`Unknown` 时**直接返回空路径**，不调用 `ModelCatalog::scan()`、不产生任何额外文件系统开销（FR-005/FR-009"不进入投机解码分支判断逻辑本身"）；`tier` 为 `Medium`/`High` 时，先调用 `ModelCatalog::pairedDraftCandidate(tier)` 得到所需 `DraftCandidate`，再对 `ModelCatalog::scan()` 结果逐一调用 `ModelCatalog::classifyDraftCandidateByFileName(descriptor.fileName)`，返回首个匹配该 `DraftCandidate` 的 `descriptor.filePath`；未找到匹配项时返回空路径（`LlmEngine` 据此天然走 FR-006 静默降级路径）。

- [X] T013 [US1] 修改 `babel/src/session/SessionController.cpp` 的 `bootstrapPlatformDefaults()`（depends on T006, T012）：在构造默认（低配）`LlmEngine` 实例处，调用 `classifyTierByFileName(defaultModelFileName)` 得到 `defaultTier`（既有变量，复用），再调用 `pairedDraftModelPathForTier(defaultTier)` 并将结果作为第二参数一并传给 `std::make_shared<babel::slowpath::LlmEngine>(defaultModelPath, draftPath)`（默认低配档位下 `draftPath` 恒为空路径，`LlmEngine` 内部零额外开销）。

- [X] T014 [US1] 修改 `babel/src/session/SessionController.cpp` 的 `switchModel()` 后台线程 lambda（depends on T006, T012）：对初次尝试加载的 `targetPath` 与级联回退候选 `fallbackCandidates` 中的每一次 `LlmEngine` 构造，均先通过 `classifyTierByFileName()` 得到该候选对应的 `tier`，调用 `pairedDraftModelPathForTier(tier)` 得到草稿模型路径，一并传给 `std::make_shared<babel::slowpath::LlmEngine>(candidatePath, draftPath)`；既有级联回退判定逻辑（按 `modelAvailable()` 判定目标模型是否加载成功）与通知文案生成逻辑**不变**（草稿模型缺失不影响、不触发该回退链路，FR-006）。

### Tests for User Story 1

- [X] T015 [US1] 在 `tests/slowpath/LlmEngineGenerateTest.cpp` 新增测试用例（depends on T010；依赖用户手动下载的真实目标+草稿模型文件，环境缺失时按既有 `GTEST_SKIP()` 模式跳过，不影响 CI）：
  - `GenerateWithDraftModelProducesSemanticallyEquivalentOutputToSingleModelPath`：对同一输入分别构造"仅目标模型"（单参数）与"目标+草稿模型"（双参数）两个 `LlmEngine` 实例并调用 `generate()`，断言两次返回文本在内容上一致（验证 FR-001"语义等价"，允许因采样随机性产生的措辞级差异由人工复核，自动化断言至少确认均非空且不含标注符号）；
  - `GenerateWithDraftModelStillEnforcesGrammarConstraintOnBothSides`：构造"目标+草稿"双模型实例，对含高歧义密度标注片段的输入执行生成，断言返回结果不含 `⟦`/`⟧`/`|` 任意字符（FR-003，复用既有 003 高密度标注测试样例）；
  - `GenerateWithMissingDraftModelPathFallsBackSilentlyToSingleModelPath`：构造 `LlmEngine(targetPath, 不存在的草稿路径)`，断言 `modelAvailable() == true`（目标模型本身不受影响）且 `generate()` 返回非空结果，不抛出异常、不崩溃（FR-006）；
  - `GenerateWithCorruptDraftModelFileFallsBackSilentlyToSingleModelPath`：构造一个文件名匹配草稿关键字但内容损坏（非法 GGUF 头或空文件）的草稿路径，传入 `LlmEngine` 构造函数，断言行为与上一条等价（Edge Cases 第二条：草稿模型损坏不得影响目标模型本身）。

  > **执行记录**：实施期间用户完成了 Qwen2.5-0.5B-Instruct/Qwen2.5-1.5B-Instruct 草稿模型下载，本机已具备真实低配（3B）目标模型 + 真实草稿模型文件，故上述 4 个用例均按**真实推理**（非 `GTEST_SKIP()`）执行并全部通过，`GenerateWithDraftModelProducesSemanticallyEquivalentOutputToSingleModelPath`/`GenerateWithDraftModelStillEnforcesGrammarConstraintOnBothSides` 实际验证了真实投机解码路径的语义等价性与语法约束，优于任务原先预期的"环境缺失按 `GTEST_SKIP()` 跳过"场景。

- [X] T016 [US1] 运行 `ctest --preset macos --output-on-failure --tests-regex "LlmEngineGenerateTest|LlmEngineUnavailableTest|ModelCatalogTest"`（depends on T013, T014, T015），确认 User Story 1 新增/更新断言全部通过（或因本机未放置真实草稿模型文件按 `GTEST_SKIP()` 跳过）；若本机已放置真实中配/高配目标模型与对应草稿模型文件，依据 `quickstart.md` 验证场景一手工对照一次"启用/不启用投机解码"的整句生成延迟，记录实测数值（不回填 spec.md，仅记入实现说明/PR 描述，FR-008）。

  > **阻塞**：中配（Qwen2.5-7B-Instruct）文件用户仍在下载中（实施期间观察文件大小约 5.9MB，远未达到完整 Q4_K_M 体积，无法加载），中配档位的整句生成延迟对照无法在本次执行中完成，需用户下载完成后使用 `tests/session/SessionControllerTest.cpp` 既有 `SwitchModelToMediumOrHighTierModelLengthensDebounceWindow`（当前因此 `GTEST_SKIP()`）与 `quickstart.md` 场景一步骤补测。高配（Qwen2.5-14B-Instruct + Qwen2.5-1.5B-Instruct）档位已完成真实延迟对照，详见 `quickstart.md`"实测记录"：发现当前实现（固定 `K=4`）在本机测得**未达成** SC-001/SC-002 预期的"可测量降低"（反而慢 1.8～2.6 倍），但 FR-001"语义等价"已通过逐字一致的真实输出确认（且修复了实现过程中发现的一处 logits 索引偏移正确性缺陷，详见 T010 记录）。

**Checkpoint**：至此 User Story 1 独立可用——中配/高配档位在已正确配置草稿模型时生效投机解码，低配档位不受影响，语义等价性与 GBNF 语法约束均得到保证，可独立交付/演示（🎯 MVP）。

---

## Phase 4: User Story 2 - 用户了解草稿模型的获取与配置方式 (Priority: P2)

**Goal**：设置页面展示草稿模型的两档候选推荐规格（Qwen2.5-0.5B-Instruct / Qwen2.5-1.5B-Instruct）及"下载其一即可自动搭配当前目标模型使用、无需手动指定"的说明（FR-004/FR-007）。

**Independent Test**：在设置页面查看中配/高配档位的模型信息展示区域，验证其中包含草稿模型推荐候选规格与自动搭配关系说明。

### Implementation for User Story 2

- [X] T017 [P] [US2] 在 `babel/src/session/SessionController.h` 新增只读属性与方法声明（`contracts/qml-cpp-session-interface-increment.md` 一）：`Q_PROPERTY(QVariantList recommendedDraftProfiles READ recommendedDraftProfilesVariant CONSTANT)`，公开方法 `std::vector<babel::slowpath::RecommendedDraftModelProfile> recommendedDraftProfiles() const;` 与 `QVariantList recommendedDraftProfilesVariant() const;`（与既有 `recommendedProfiles()`/`recommendedProfilesVariant()` 同构）。

- [X] T018 [US2] 在 `babel/src/session/SessionController.cpp` 实现 T017 声明的两个方法（depends on T017, T003）：`recommendedDraftProfiles()` 直接转发 `babel::slowpath::ModelCatalog::recommendedDraftProfiles()`；`recommendedDraftProfilesVariant()` 将其转换为 `QVariantList`（元素含 `size`/`modelName`/`quantization`/`approxSizeDescription` 四个键，`size` 转换为字符串 `"Size0_5B"`/`"Size1_5B"`，与既有 `recommendedProfilesVariant()` 的 `tier` 字符串化模式一致）。

- [X] T019 [US2] 在 `babel/content/SettingsPanel.qml` 新增草稿模型候选规格展示区块（depends on T018）：复用既有"推荐档位"`Repeater` 模式，新增一个绑定 `SessionController.recommendedDraftProfiles` 的 `Repeater`，展示 `modelName`/`quantization`/`approxSizeDescription`；并新增一行固定说明文案，告知用户"下载以上任一候选规格即可被自动识别并搭配当前目标模型档位使用，无需手动指定搭配关系"（FR-007）。

### Tests for User Story 2

- [X] T020 [P] [US2] 在 `tests/session/SessionControllerTest.cpp` 新增测试用例 `RecommendedDraftProfilesExposesFixedTwoCandidates`（depends on T018）：断言 `recommendedDraftProfiles()` 返回恰好 2 条数据，`modelName` 分别为 "Qwen2.5-0.5B-Instruct"/"Qwen2.5-1.5B-Instruct"（与既有 `RecommendedProfilesExposesFixedThreeTiers` 测试风格一致）。

- [X] T021 [US2] 运行 `ctest --preset macos --output-on-failure --tests-regex "SessionControllerTest"`（depends on T020），确认 User Story 2 新增断言通过，既有 `SessionControllerTest` 用例无回归。

**Checkpoint**：User Story 1、2 叠加后均可独立工作——用户既能获得投机解码加速，也能在设置页面了解草稿模型的获取/搭配方式。

---

## Phase 5: User Story 3 - 用户未配置草稿模型时系统行为可预期 (Priority: P2)

**Goal**：草稿模型未配置/加载失败时静默降级为既有单模型生成路径（已由 US1 的 T007/T015 在 `LlmEngine` 层面验证），并在设置页面给出明确提示告知用户原因（FR-006），切换目标模型档位后自动重新匹配新档位对应的默认草稿模型（Edge Cases 第一条）。

**Independent Test**：在未放置任何草稿模型文件的环境下，切换为中配或高配目标模型并触发一次慢流生成，验证生成成功完成并产出正确内容，且设置页面展示明确提示告知用户当前未启用投机解码加速及原因。

### Implementation for User Story 3

- [X] T022 [P] [US3] 在 `babel/src/session/SessionController.h` 新增只读属性与关联声明（`contracts/qml-cpp-session-interface-increment.md` 一）：`Q_PROPERTY(QString speculativeDecodingNoticeText READ speculativeDecodingNoticeText NOTIFY speculativeDecodingNoticeTextChanged)`，公开方法 `QString speculativeDecodingNoticeText() const { return m_speculativeDecodingNoticeText; }`，信号 `void speculativeDecodingNoticeTextChanged();`，私有成员 `QString m_speculativeDecodingNoticeText;`。

- [X] T023 [US3] 修改 `babel/src/session/SessionController.cpp`（depends on T022, T012, T014，`data-model.md` 三"取值来源与计算规则"）：在 `applyModelSwitchResult()` 成功分支末尾（及 `bootstrapPlatformDefaults()` 初始化低配默认模型后）新增提示文案计算逻辑——`tier` 为 `Low`/`Unknown` 时 `m_speculativeDecodingNoticeText` 恒置为空字符串（FR-009，不展示任何提示）；`tier` 为 `Medium`/`High` 时，调用 `pairedDraftModelPathForTier(tier)` 判断对应草稿模型文件是否存在（空路径即未配置）——未配置时置为非空提示文案（如 `"未检测到草稿模型，投机解码未启用"`，FR-006 原文示例）；已配置时置为空字符串（或正向提示，文案措辞按宪章原则 IV 自主确定）；每次计算后若取值发生变化则 `emit speculativeDecodingNoticeTextChanged();`。

- [X] T024 [US3] 在 `babel/content/SettingsPanel.qml` 新增提示展示（depends on T023）：在"当前模型"展示文本附近新增一个 `Text` 元素，`visible: SessionController.speculativeDecodingNoticeText.length > 0`，内容绑定 `SessionController.speculativeDecodingNoticeText`（复用既有 `fallbackNoticeText` 展示样式：`#ffcc66` 提示色、`Text.WordWrap`）。

### Tests for User Story 3

- [X] T025 [P] [US3] 在 `tests/session/SessionControllerTest.cpp` 新增测试用例（depends on T023）：
  - `SpeculativeDecodingNoticeTextEmptyForLowTierDefaultModel`：断言 `bootstrapPlatformDefaults()`/低配默认模型初始化后 `speculativeDecodingNoticeText()` 为空字符串（FR-009）；
  - `SpeculativeDecodingNoticeTextNonEmptyWhenDraftModelMissingForMediumOrHighTier`：通过 `setLlmEngine()`/隔离模型目录模拟切换为中配或高配模型且约定目录中不存在对应草稿模型文件，断言切换完成后 `speculativeDecodingNoticeText()` 为非空提示（FR-006）；
  - `SpeculativeDecodingNoticeTextClearsWhenMatchingDraftModelFilePresent`：在约定目录中放置文件名匹配的草稿模型文件后重复上一条场景，断言 `speculativeDecodingNoticeText()` 变为空字符串；
  - `SwitchingTargetModelTierAutomaticallyRematchesDraftModel`（Edge Cases 第一条，`quickstart.md` 验证场景三）：先切换到中配档位（已配置对应草稿模型）确认提示为空，再切换到高配档位（尚未配置对应草稿模型），断言提示自动变为非空，不错误沿用中配阶段"已启用"状态。

- [X] T026 运行 `ctest --preset macos --output-on-failure --tests-regex "SessionControllerTest|LlmEngineGenerateTest|LlmEngineUnavailableTest"`（depends on T025），确认 User Story 3 新增断言全部通过，既有用例无回归。

**Checkpoint**：User Story 1、2、3 全部独立可用——投机解码在中/高配档位生效、用户了解如何获取配置草稿模型、未配置时生成仍正常完成并有明确提示。

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**：三个用户故事均完成后的端到端手工验证、实测数据记录与全量回归。

- [X] T027 全量回归：运行 `ctest --preset macos --output-on-failure`（depends on T005, T016, T021, T026），执行全部既有 + 本特性新增测试用例（73 + 本特性新增用例数），确认无回归失败，与 T001 建立的基线逐条对照差异均为本特性预期内的新增/变更。

  > **执行记录**：全量回归共 86 个测试用例（73 既有 + 13 本特性新增：T004 新增 4 个 + T015 新增 4 个 + T020 新增 1 个 + T025 新增 4 个），100% 通过，2 个按既有/预期原因跳过（`PasteInjectorPermissionTest.UngrantedAccessibilityYieldsPermissionDenied`：需沙箱外真实 Accessibility 权限授予，与本特性无关；`SessionControllerTest.SwitchModelToMediumOrHighTierModelLengthensDebounceWindow`：003 既有测试，因 Qwen2.5-7B-Instruct 仍在下载中而跳过，与本特性无关），无回归。

- [X] T028 [P] 按 `quickstart.md` 验证场景一（中/高配已正确配置草稿模型时延迟降低且质量不下降，SC-001/SC-002/SC-003）手工验证（depends on T016）：记录启用/不启用投机解码两种路径的整句生成延迟与文本内容对比结果；若本机无真实中配/高配目标模型 + 对应草稿模型文件，记录"因缺少模型文件无法实测"并说明需用户下载后补测（与 003 `tasks.md` T027 的既有记录模式一致）。

  > **执行记录**：高配（Qwen2.5-14B-Instruct + Qwen2.5-1.5B-Instruct）已完成真实对照：SC-003（质量不下降）**达成**——两条路径文本逐字完全一致；SC-002（延迟可测量降低）**未达成**——投机解码路径实测反而慢 1.8～2.6 倍（详见 `quickstart.md`"实测记录"与下方分析）。
  >
  > **阻塞**：中配（Qwen2.5-7B-Instruct）文件仍在下载中（约 5.9MB，远未达到完整体积），SC-001 无法在本次执行中实测，需用户下载完成后补测。

- [X] T029 [P] 按 `quickstart.md` 验证场景二（未配置草稿模型时设置页面正确提示，SC-004）手工验证（depends on T024, T025）：核对未配置/损坏草稿模型文件时生成仍成功完成、提示文案正确展示；"真实浮窗 UI 下人工视觉核对"需要真实桌面 WindowServer 会话，沙箱环境无显示支持时记录为"阻塞，需在具备真实桌面环境的机器上另行执行"，并说明对应逻辑已由自动化测试（T015/T025）在源码层面验证。

  > **阻塞**：本次执行环境为无显示支持的沙箱，无法进行"真实浮窗 UI 下人工视觉核对"（需在具备真实桌面 WindowServer 会话的机器上另行执行，与 003 既有同类记录模式一致）。对应逻辑已由 `tests/session/SessionControllerTest.cpp` 的 `SpeculativeDecodingNoticeTextNonEmptyWhenDraftModelMissingForMediumOrHighTier`/`SpeculativeDecodingNoticeTextClearsWhenMatchingDraftModelFilePresent`（T025）在源码层面以真实目标模型权重验证通过，SC-004 对应的核心行为（生成成功完成 + 提示文案正确展示/消失）已有自动化证据支撑。

- [X] T030 按 `quickstart.md` 验证场景三（切换档位自动重新匹配草稿模型，Edge Cases 第一条）手工验证（depends on T025）：核对中配→高配切换后提示文案正确重置，不沿用旧档位状态。

  > **执行记录**：本场景的核心行为已由 `tests/session/SessionControllerTest.cpp` 的 `SwitchingTargetModelTierAutomaticallyRematchesDraftModel`（T025）以真实权重文件通过符号链接技巧自动化验证通过（中配已配置草稿模型 → 提示为空；切换至高配未配置草稿模型 → 提示自动变为非空，未沿用中配阶段状态）。真实浮窗 UI 下的人工视觉核对与 T029 同理，受限于沙箱无显示支持，记录为阻塞，需在具备真实桌面环境的机器上另行执行。

- [X] T031 记录 SC-001/SC-002 要求的实测延迟数据与 `research.md` §4 要求的内存/显存叠加占用实测结果（depends on T028），回填 `quickstart.md`"实测记录"小节（`/speckit.implement` 完成后填写，**不得**回填 `spec.md` 本身，FR-008 明文禁止在规格阶段断言具体数值）。

  > **执行记录**：`quickstart.md`"实测记录"小节已回填 SC-001/SC-002 延迟数据（高配档位，诚实记录投机解码实测**慢于**单模型路径 1.8～2.6 倍，非预期方向，附带可能原因分析与后续调优建议）、验证通过率粗略观察值、FR-001 正确性修复过程。
  >
  > **阻塞/未完成**：(1) 中配档位 SC-001 延迟数据因 Qwen2.5-7B-Instruct 仍在下载中而缺失，需用户下载完成后补测；(2) `research.md` §4 要求的内存/显存叠加占用实测（RSS 读数对照）本阶段**未执行**，优先级低于上述已发现的延迟不达标问题，建议后续调优迭代中一并补做。

---

## 事后修订（T031 之后，用户决策，`direct` 流程，未重走 clarify）

T031 实测确认投机解码在本机反而显著变慢（高配档位慢 1.8～2.6 倍）、不符合 SC-001/SC-002 立项初衷后，用户决策：**代码保留，但默认不启用，作为用户可自行开启的实验性功能**，且需在设置页面提供可见开关（而非纯代码常量）。本次修订已直接实施（用户选择 `direct` 流程而非重走 `/speckit.clarify`→更新 spec.md），如实记录改动范围：

- `babel/src/session/SessionController.{h,cpp}`：新增 `speculativeDecodingEnabled` 读写属性（默认 `false`，不跨应用重启持久化，与既有"模型选择不持久化"约定一致）与 `setSpeculativeDecodingEnabled()`；`lookupPairedDraftModelPath()`/`pairedDraftModelPathForTier()` 新增 `speculativeDecodingEnabled` 参数，关闭时恒返回空路径（即使已配置匹配的草稿模型文件）；`updateSpeculativeDecodingNoticeText()` 区分"开关未开启"与"开关已开启但未配置草稿模型文件"两种不同提示文案；`setSpeculativeDecodingEnabled()` 在已有模型加载时复用既有 `switchModel()` 热重载流程使开关状态立即生效。
- `babel/content/SettingsPanel.qml`：新增纯 QtQuick 实现的开关控件（Rectangle+MouseArea，未引入 QtQuick.Controls 依赖），绑定 `SessionController.speculativeDecodingEnabled`。
- `tests/session/SessionControllerTest.cpp`：更新 2 个既有测试（`SpeculativeDecodingNoticeTextNonEmptyWhenDraftModelMissingForMediumOrHighTier`/`SpeculativeDecodingNoticeTextClearsWhenMatchingDraftModelFilePresent`）与 1 个既有测试（`SwitchingTargetModelTierAutomaticallyRematchesDraftModel`）以显式调用 `setSpeculativeDecodingEnabled(true)`，使其验证的仍是原定行为意图（而非被新默认值意外改变断言结果）；新增 2 个测试（`SpeculativeDecodingDisabledByDefault`/`SpeculativeDecodingNoticeShowsDisabledReasonEvenWhenDraftModelFilePresent`）覆盖本次新增的默认关闭行为。

**验证结果**：全量回归 `ctest -C Debug` 88/88 通过（2 项按既有设计跳过，与本次修订无关）；独立探针测试确认 `speculativeDecodingEnabled` 读写行为正确（默认 false，可翻转为 true/false）。

---


## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**：无依赖，可立即开始。
- **Foundational (Phase 2)**：依赖 Setup 完成；**阻塞**全部三个用户故事——`ModelCatalog` 新增的 `DraftCandidate`/`recommendedDraftProfiles()`/`pairedDraftCandidate()` 是 US1（草稿模型路径查表）、US2（候选规格展示）、US3（已配置判定）共同的前置依赖。
- **User Story 1 (Phase 3)**：依赖 Foundational 完成；是 MVP 核心，**建议最先完成**。
- **User Story 2 (Phase 4)**：依赖 Foundational 完成；与 US1 在 `SessionController.h/.cpp` 存在同文件编辑（新增属性与 US1 的 `switchModel()` 改动不在同一方法体内，冲突面小），逻辑上不依赖 US1 的具体实现细节，可与 US1 并行开工，但同一文件的实际编辑顺序建议在 US1 完成后进行以减少合并冲突。
- **User Story 3 (Phase 5)**：依赖 Foundational 完成；**依赖** US1 的 `pairedDraftModelPathForTier()`（T011/T012）与 `switchModel()`/`bootstrapPlatformDefaults()` 改动（T013/T014）已存在，复用其查表结果计算提示文案，故实际开工顺序 **MUST** 晚于 US1 对应任务完成（spec.md 已将 US2/US3 定位为"围绕 US1 核心价值的支撑性场景"，存在该依赖关系符合预期）。
- **Polish (Phase 6)**：依赖全部三个用户故事完成。

### User Story Dependencies

- **User Story 1 (P1)**：Foundational 完成后即可开始，不依赖 US2/US3。
- **User Story 2 (P2)**：Foundational 完成后即可开始；功能上独立于 US1/US3（仅展示静态推荐数据），但建议在 US1 之后开工以降低 `SessionController.{h,cpp}` 同文件合并冲突。
- **User Story 3 (P2)**：Foundational 完成后即可开始实现 UI/属性骨架（T022），但 T023（核心提示文案计算逻辑）**依赖** US1 的 T012/T014 已完成。

### Within Each User Story

- 测试 MUST 在实现完成后编写并验证通过（本特性测试任务编号顺序安排在对应实现任务之后，便于确认行为符合契约）。
- 头文件声明先于对应 `.cpp` 实现。
- `LlmEngine` 内部算法实现（T007～T010）先于 `SessionController` 对其的调用点改造（T012～T014）。
- 每个用户故事的 Checkpoint 达成后方可视为该故事独立可交付。

### Parallel Opportunities

- Foundational 阶段：T002（头文件）可与其他阶段任务并行准备，但 T003（依赖 T002）、T004（依赖 T003）须顺序执行。
- US1：T006（`LlmEngine.h`）与 T011（`SessionController.h`）操作不同文件，可并行；T007～T010（`LlmEngine.cpp` 同文件）须顺序执行；T012～T014（`SessionController.cpp` 同文件）须顺序执行。
- US2：T017（`SessionController.h`）可与 US1/US3 的头文件任务并行；T018/T019/T020 顺序依赖。
- US3：T022（`SessionController.h`）可与 US2 的 T017 并行（同文件但不同属性块，建议仍按顺序合并以避免手工合并冲突）；T023/T024/T025 顺序依赖。
- US1、US2 的测试任务（T015、T020）可并行编写（不同测试文件）。

---

## Parallel Example: Foundational + User Story 1 启动

```bash
# Foundational 头文件声明（可先行）：
Task: "在 babel/src/slowpath/ModelCatalog.h 新增 DraftCandidate 枚举、RecommendedDraftModelProfile 结构体与 3 个新方法声明"

# Foundational 完成后，US1 的两个头文件任务可并行：
Task: "在 babel/src/slowpath/LlmEngine.h 扩展构造函数签名与私有草稿模型状态"
Task: "在 babel/src/session/SessionController.h 新增 pairedDraftModelPathForTier() 私有方法声明"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. 完成 Phase 1：Setup（基线确认）
2. 完成 Phase 2：Foundational（`ModelCatalog` 草稿模型识别/推荐/搭配映射，**阻塞**全部用户故事）
3. 完成 Phase 3：User Story 1（`LlmEngine` 投机解码核心实现 + `SessionController` 查表接线）
4. **停止并验证**：按 `quickstart.md` 场景一独立验证 User Story 1（中/高配延迟可测量降低、语义等价、低配不受影响）
5. 具备 MVP 交付条件：中/高配用户已可获得投机解码加速

### Incremental Delivery

1. Setup + Foundational 完成 → 基础设施就绪
2. 新增 User Story 1 → 独立验证 → 部署/演示（**MVP**）
3. 新增 User Story 2 → 独立验证 → 部署/演示（用户了解草稿模型获取方式）
4. 新增 User Story 3 → 独立验证 → 部署/演示（未配置草稿模型时行为可预期）
5. Polish 阶段：全量回归 + 实测数据记录

### Parallel Team Strategy

多名开发者协作时：

1. 团队共同完成 Setup + Foundational
2. Foundational 完成后：
   - 开发者 A：User Story 1（`LlmEngine.{h,cpp}` 核心算法 + `SessionController` 查表接线）
   - 开发者 B：User Story 2（`SessionController` 新增只读属性 + `SettingsPanel.qml` 候选规格展示），待 A 的查表方法声明（T011）确定签名后再编辑 `SessionController.cpp` 以减少冲突
   - 开发者 C：待 A 的 T012/T014 完成后再开始 User Story 3（依赖查表结果计算提示文案）
3. 三个故事独立验证后集成，互不破坏彼此已验证的行为

---

## Notes

- `[P]` 任务 = 不同文件、无依赖关系。
- `[Story]` 标签将任务映射到对应用户故事，便于追溯。
- 每个用户故事应可独立完成与验证；US2/US3 因 `plan.md`/`research.md` 已记录的技术依赖关系（查表方法复用）晚于 US1 对应任务，但不影响各自验收场景的独立可验证性。
- 实施前确认测试处于预期状态（本特性测试任务安排在实现任务之后编写，与 003 既有模式一致，非 TDD"先写测试后实现"流程）。
- 每完成一个任务或一组逻辑相关任务后建议提交。
- 在每个 Checkpoint 处可独立验证对应用户故事，不必等待全部故事完成。
- 避免：模糊任务描述、同文件任务标注 `[P]` 造成并行编辑冲突、破坏已验证故事的跨故事依赖。
