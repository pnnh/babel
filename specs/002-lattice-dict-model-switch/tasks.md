---

description: "Task list for 拼音多候选格架构、真实词库与可切换本地模型"
---

# Tasks: 拼音多候选格架构、真实词库与可切换本地模型

**Input**: Design documents from `/specs/002-lattice-dict-model-switch/`

**Prerequisites**: [plan.md](./plan.md)（必需）、[spec.md](./spec.md)（必需）、[research.md](./research.md)、[data-model.md](./data-model.md)、[contracts/](./contracts/)、[quickstart.md](./quickstart.md)

**Tests**: 本特性延续 001 已选型的 **GoogleTest**（`plan.md`/`tests/CMakeLists.txt` 既有基础设施，无需新增框架），并符合宪章原则 VI 预授权质量基线"为新增逻辑补对应测试"；以下各用户故事阶段均包含对应测试任务。

**语言（宪章《语言与文档规范》）**：任务描述使用简体中文；文件路径、符号名、命令保留英文。

**Organization**：任务按用户故事（spec.md 的 P1/P2/P3）分组，确保每个故事可独立实现、独立测试、独立交付增量价值。本特性是对 `specs/001-floating-ime-converter` 已交付 MVP 的增量扩展，以下任务默认在 001 既有代码基线（`babel/src/{fastpath,slowpath,session}`、`babel/content/FloatingWindow.qml`、`tests/`）之上修改/新增，不重建既有骨架。

## Format: `[ID] [P?] [Story] Description`

- **[P]**：可与同阶段内其他 [P] 任务并行执行（不同文件、无先后依赖）
- **[Story]**：任务所属用户故事（US1/US2/US3），仅用户故事阶段任务标注，Setup/Foundational/Polish 阶段不标注
- 每条任务均含明确文件路径

## Path Conventions

沿用仓库既有单工程桌面应用布局（见 `plan.md` Project Structure）：

- `vcpkg.json`、根 `CMakeLists.txt`：本特性**不变更**（§2/§9/§7.4 最终选型均为零新增依赖）
- `babel/`：既有 Qt6 Quick 可执行工程（`babel/CMakeLists.txt`、`babel/main.cpp`、`babel/content/`）
- `babel/src/`：既有 `babel_core` 纯 C++ 业务逻辑静态库，按 `fastpath/`（快流：真实词库+Lattice）、`slowpath/`（慢流：标注文本+可切换模型）、`session/`（浮窗会话编排）三个子目录承载本特性改动
- `tests/`：既有 GoogleTest 测试目录，结构镜像 `babel/src/` 子目录划分；`babel/src/CMakeLists.txt`/`tests/CMakeLists.txt` 均使用 `file(GLOB_RECURSE ...)` 自动纳入新增 `.cpp`/`*Test.cpp` 源文件，新增源文件**无需**额外登记 CMake target

---

## Phase 1: Setup（词库资产准备）

**Purpose**：为 User Story 1（真实词库）准备随包分发的 `iDvel/rime-ice` 词库原始数据与 GPLv3 合规文件，并使其随构建产物一同打包；不包含任何业务逻辑代码

- [X] T001 [P] 获取 `iDvel/rime-ice` 仓库 `cn_dicts/` 下细胞词库原始文件（至少含 `base.dict.yaml`、`ext.dict.yaml`、`8105.dict.yaml` 等能覆盖本 spec 背景/Edge Cases 列举常见词的子集，`research.md` §1 已核实的 GPLv3 仓库），连同该仓库 `LICENSE` 全文另存为 `LICENSE-rime-ice.txt`、来源/版权/上游 commit 声明另存为 `NOTICE-rime-ice.txt`，一并存放至新增目录 `babel/content/dictionaries/`（`research.md` §1/§3，FR-001）
- [X] T002 在 `babel/CMakeLists.txt` 中扩展既有 `ASSETS_SOURCES`/`qt_add_qml_module RESOURCES` 资源收录范围（或新增并列的 `file(GLOB_RECURSE DICTIONARY_SOURCES ... content/dictionaries/*)` + `RESOURCES`），使 T001 存放的 `.dict.yaml`/`LICENSE-rime-ice.txt`/`NOTICE-rime-ice.txt` 文件随应用构建产物一并内嵌/打包，不新增第三方依赖（`research.md` §3，FR-001）

**Checkpoint**：随包分发词库资产已就绪并被构建产物包含，供 Foundational/US1 阶段读取

---

## Phase 2: Foundational（阻塞性前置基础设施）

**Purpose**：声明本特性三个方向各自新增的核心数据结构与路径约定工具（镜像 001 `ModelPaths` 的既有模式），为后续三个用户故事的独立实现提供共同的类型基础；**任何用户故事实现 MUST 等待本阶段完成后才能开始**

**⚠️ CRITICAL**：本阶段任务完成前，不得开始 Phase 3+ 的任何用户故事任务

- [X] T003 [P] 实现 `babel::fastpath::DictionaryPaths`（约定词库目录路径工具，镜像 `ModelPaths::conventionalModelDirectory()` 既有模式：`QStandardPaths::AppDataLocation` 下新增约定子目录，`research.md` §3/§7.1）于 `babel/src/fastpath/DictionaryPaths.{h,cpp}`
- [X] T004 [P] 定义 `RimeDictionarySourceFile` 诊断记录类型（`filePath`/`frontmatterName`/`entryCount`/`loadStatus` 枚举 `Loaded`\|`Skipped`\|`Aborted`，`data-model.md` §4）于 `babel/src/fastpath/RimeDictLoader.h`（仅类型声明与函数签名占位，解析实现留待 US1）
- [X] T005 [P] 定义 `CandidateEntry`（`surfaceForm`/`rawWeight`/`normalizedWeight`/`source` 枚举）、`PinyinLatticeCell`（`tokenIndex`/`originalToken`/`candidates`/派生 `isAmbiguous`）、`PinyinLattice`（`cells`/`builtAt`，含 `toDisplayText()`/`toAnnotatedPromptText()` 方法声明占位，`data-model.md` §1-3）于 `babel/src/fastpath/PinyinLattice.h`（方法实现留待 US2）
- [X] T006 [P] 在 `babel/src/session/DraftTypes.h` 中为 `NormalizedCandidateText` 新增 `babel::fastpath::PinyinLattice lattice` 字段，保留既有 `content`/`updatedAt` 字段不变（最小必要修改，`data-model.md` §1"与 NormalizedCandidateText 的关系"）（依赖 T005）
- [X] T007 [P] 定义 `ModelDescriptor`（`filePath`/`fileName`/`sizeBytes`/`tierHint` 枚举/`availability` 枚举）、`RecommendedModelProfile`（`tier`/`modelName`/`quantization`/`approxSizeDescription`/`targetHardwareDescription`）、`ActiveModelRuntimeState`（`currentModelPath`/`isSwitching`/`fallbackHistory`/`lastFallbackNotice`，`data-model.md` §6-8）于 `babel/src/slowpath/ModelCatalog.h`（仅类型声明，`ModelCatalog::scan()` 实现留待 US3）

**Checkpoint**：三个方向的基础数据结构与目录约定就绪——三个用户故事现在可以独立并行开始实现

---

## Phase 3: User Story 1 - 真实拼音词库消除因数据缺失导致的错误回退匹配 (Priority: P1) 🎯 MVP

**Goal**：将快流纠错的词典数据源从 001 内置 14 条演示数据升级为基于 `iDvel/rime-ice` 真实开源 Rime 词库，支持标准 `.dict.yaml` 解析、细胞词库自动合并、纯约定目录用户自助更新；查无候选时仍兜底回退到既有编辑距离匹配

**Independent Test**：收集 `kaorou`/`shaokao` 等此前因词典缺失被错误回退匹配的样本，应用词库升级后重新输入，验证快流纠错命中词库真实词条而非编辑距离回退匹配（spec Independent Test，`quickstart.md` 验证场景一）

### Tests for User Story 1 ⚠️

> 先写测试，确认其在对应实现完成前处于 FAIL 状态

- [X] T008 [P] [US1] `RimeDictLoader` 单元测试：覆盖 frontmatter `---`/`...` 边界定位与跳过（含 `research.md` §1 已核实的 `rime_ice.dict.yaml` 嵌套列表+行内注释+被注释掉的行真实样例）、制表符分隔"词条/拼音/权重"数据行解析、格式错误文件被跳过且不中断其余文件加载（`loadStatus=Skipped`，`research.md` §4.1）、同目录多细胞词库按文件名字典序通过既有 `insertDictionaryEntries()` `INSERT OR IGNORE` 语义合并、重复词条先加载者权重生效（`research.md` §4.2）、`loadAllDictionaryEntries()` 可查得 `kaorou`/`shaokao` 等常见词真实候选 于 `tests/fastpath/RimeDictLoaderTest.cpp`（依赖 T004）

### Implementation for User Story 1

- [X] T009 [US1] 实现 `RimeDictLoader`：扫描 `DictionaryPaths::conventionalDictionaryDirectory()` 下全部 `.dict.yaml` 文件，手写最小化解析器定位 frontmatter 边界并跳过（零新增依赖，`research.md` §2），按文件名字典序依次解析 TSV 数据行并调用 `insertDictionaryEntries()` 写入 sqlite3，解析失败文件标记 `RimeDictionarySourceFile.loadStatus=Skipped` 并输出 `spdlog::warn("[RimeDictLoader] skip malformed file path={} reason={}", ...)` 诊断日志、不中断其余文件加载 于 `babel/src/fastpath/RimeDictLoader.cpp`（依赖 T003、T004、T008）
- [X] T010 [US1] 在应用启动流程中实现"随包词库资产首次部署"逻辑：若 `DictionaryPaths::conventionalDictionaryDirectory()` 下不存在词库文件，则从 T002 内嵌资源复制一份初始副本（含 `.dict.yaml`/`LICENSE-rime-ice.txt`/`NOTICE-rime-ice.txt`）到该约定目录，确保 FR-001"随包分发"对用户可见、FR-004"纯约定目录"内容包含官方词库作为起点 于 `babel/main.cpp`（依赖 T001、T002、T003）
- [X] T011 [US1] 在 `SessionController::bootstrapPlatformDefaults()` 中将 `FastCorrector` 构造改为：打开/建立 sqlite3 连接 → `ensureDictionarySchema()` → `RimeDictLoader` 扫描约定目录写入 → `loadAllDictionaryEntries()` 读取全部条目构建 `FastCorrector`；若目录为空或全部文件解析失败（条目数为 0），回退使用 `builtinSeedDictionary()` 作为最后兜底（FR-005，`data-model.md` §5）于 `babel/src/session/SessionController.cpp`（依赖 T009、T010）
- [X] T012 [US1] 按 `quickstart.md` 验证场景一步骤 1–7 手工执行（词库加载日志、`kaorou`/`shaokao` 真实命中、生僻自造词编辑距离兜底不中断、新增细胞词库文件后无需重装生效、格式错误文件被跳过且无用户可见提示、同词条权重按文件名字典序合并），确认 Acceptance Scenarios 1-4 与 SC-004/SC-005 均满足（依赖 T009、T010、T011）

**Checkpoint**：至此 User Story 1 应完全可独立运行与测试（MVP 完成点——真实词库质量提升已交付，不依赖 Lattice/模型切换）

---

## Phase 4: User Story 2 - Lattice 多候选格架构结合上下文消解同音字误判 (Priority: P2)

**Goal**：将 `FastCorrector` 从"单一候选"改造为"Lattice 多候选格"，每个拼音 token 产出按权重排序、截断至 Top-K=8 的候选集合，以结构化标注文本（`⟦token:候选 权重|...⟧`）形式传入 `LlmEngine::generate()`（签名仍为 `QString`），供慢流结合上下文消歧；QML 展示层不呈现候选集合，仅输出开发诊断日志

**Independent Test**：构造 `chi` 等同音字歧义样本分别嵌入指向不同候选的上下文句子中输入，验证最终标准中文译写结果所用汉字与上下文语义相符而非被快流过早锁定的单一候选（spec Independent Test，`quickstart.md` 验证场景二/三）

**与 User Story 1 的关系**：文件层面与 US1 互不重叠（US1 仅改动 `fastpath/RimeDictLoader.*`/`DictionaryPaths.*`/`SessionController::bootstrapPlatformDefaults()`，US2 改动 `PinyinTrie`/`FastCorrector`/`PinyinLattice`/`LlmEngine`/`SessionController::submitKeystrokes()`+`onSlowPathTriggered()`），可在 Foundational 完成后与 US1 并行开始；但候选质量上限依赖 US1 已扩充的真实词库数据，建议按 P1→P2 顺序交付以体现叠加收益（spec "Why this priority"）。

### Tests for User Story 2 ⚠️

> 先写测试，确认其在对应实现完成前处于 FAIL 状态

- [X] T013 [P] [US2] `PinyinLattice` 单元测试：覆盖候选按 `rawWeight` 降序排序后截断至 Top-K=8（`research.md` §5.4）、`normalizedWeight` 基于截断后子集归一化（近似和为 1.00）、`isAmbiguous` 派生字段（`candidates.length >= 2`）、透传 token（非拼音）恒为单候选 `source=Passthrough` 权重 1.0、`toDisplayText()` 对外行为等价于取各格位最高权重候选拼接（001 `correct()` 兼容）、`toAnnotatedPromptText()` 严格按 `contracts/llm-prompt-annotation-contract.md` 的 `⟦原始token:候选1 权重1|候选2 权重2|...⟧` 语法仅对 `isAmbiguous` 格位输出标注 于 `tests/fastpath/PinyinLatticeTest.cpp`（依赖 T005）
- [X] T014 [P] [US2] 扩展 `PinyinTrieTest.cpp`：验证 `queryByFullPinyin()`/`queryByAbbrev()` 新返回类型携带 `frequencyWeight`（而非丢弃权重的 `std::vector<std::string>`）且仍按权重降序排列 于 `tests/fastpath/PinyinTrieTest.cpp`
- [X] T015 [P] [US2] 扩展 `FastCorrectorTest.cpp`：新增 `buildLattice()` 用例验证同一拼音片段（如 `chi`）产出多个候选而非唯一锁定、候选来源含 Trie 全拼∪简拼∪编辑距离回退合并结果、既有 `correct()` 对外输出字符串保持向后兼容（经 `toDisplayText()` 派生）于 `tests/fastpath/FastCorrectorTest.cpp`
- [X] T016 [P] [US2] 扩展 `LlmEngineGenerateTest.cpp`：验证 `generate()` 接收含 `⟦⟧` 结构化标注的输入文本时签名仍为单一 `QString` 参数不变（FR-007 硬性约束）、系统提示词包含标注语法说明 于 `tests/slowpath/LlmEngineGenerateTest.cpp`

### Implementation for User Story 2

- [X] T017 [US2] 修改 `PinyinTrie::queryByFullPinyin()`/`queryByAbbrev()` 返回类型为携带权重的候选列表（如 `std::vector<std::pair<std::string, double>>`），保留按 `frequencyWeight` 降序排序行为不变 于 `babel/src/fastpath/PinyinTrie.{h,cpp}`（依赖 T014）
- [X] T018 [US2] 实现 `PinyinLattice::toDisplayText()`（各格位最高权重候选拼接，沿用既有"相邻英文 token 间保留空格"规则）与 `toAnnotatedPromptText()`（按 `contracts/llm-prompt-annotation-contract.md` 语法，仅对 `isAmbiguous` 格位输出 `⟦⟧` 标注）于 `babel/src/fastpath/PinyinLattice.cpp`（依赖 T005、T013）
- [X] T019 [US2] 新增 `FastCorrector::buildLattice(const QString &rawInput) -> PinyinLattice`：按既有空白分词粒度逐 token 合并 Trie 全拼命中∪简拼命中∪编辑距离回退候选（取代 `correctToken()` "任一来源命中即返回单值"的提前截断逻辑），非拼音 token 保留为透传格位；候选按 `rawWeight` 降序排序后截断 Top-K=8 再计算 `normalizedWeight`（`research.md` §5.2/§5.4）；既有 `correct()` 改为 `buildLattice(rawInput).toDisplayText()` 的薄封装，保持对外签名与行为向后兼容 于 `babel/src/fastpath/FastCorrector.{h,cpp}`（依赖 T017、T018、T015）
- [X] T020 [US2] 在 `SessionController::submitKeystrokes()` 中改为调用 `m_fastCorrector->buildLattice()`，更新 `NormalizedCandidateText.content`（经 `lattice.toDisplayText()`）与新增 `lattice` 字段；在 `onSlowPathTriggered()` 中改为向 `LlmEngine::generate()` 传入 `lattice.toAnnotatedPromptText()` 而非原规整化字符串（FR-007）于 `babel/src/session/SessionController.cpp`（依赖 T019、T006）
- [X] T021 [US2] 在 `LlmEngine.cpp` 固定系统提示词（`kSystemPrompt`）中补充说明 `⟦⟧` 标注语法含义（候选+可信度数值、MUST 结合上下文选择语义最合适项、最终输出 MUST 为不含标注符号的纯中文句子），`generate()` 对外签名保持 `QString` 不变（`contracts/llm-prompt-annotation-contract.md`）于 `babel/src/slowpath/LlmEngine.cpp`（依赖 T016）
- [X] T022 [US2] 在 `SessionController` 构建 Lattice 后，对每个 `isAmbiguous` 格位输出开发诊断日志 `spdlog::debug("[Lattice] pos={} token={} candidates=[...]")`（`research.md` §6，FR-008，仅诊断用途，不面向终端用户）于 `babel/src/session/SessionController.cpp`（依赖 T020）
- [X] T023 [US2] 回归确认 `babel/content/FloatingWindow.qml` 原始输入槽/输出预览区域绑定未因 T020 的 `rawInputSlotText` 取值来源变更而向用户暴露候选集合或权重信息（FR-008，预期无需修改既有绑定，仅核实/如有泄露则修正）于 `babel/content/FloatingWindow.qml`（依赖 T020）
- [X] T024 [US2] 按 `quickstart.md` 验证场景二、场景三手工执行（同音字歧义上下文消歧结果正确、诊断日志展示候选集合与归一化权重、候选数超过 8 的音节截断生效、QML 界面本身不展示候选信息），并抽样执行场景一步骤 8–9 的 SC-001 语义正确率评估（目标 ≥80%）（依赖 T017-T023）

**Checkpoint**：至此 User Story 1 与 User Story 2 应均可独立运行与测试

---

## Phase 5: User Story 3 - 在设置页面按硬件配置水平切换本地慢流模型 (Priority: P3)

**Goal**：浮窗右上角新增设置入口，遍历约定本地模型目录列出可用模型文件，展示低配/中配/高配三档推荐信息，支持热切换（应用不重启、推理引擎卸载/重新加载）、不可用模型展示"不可用"标记、切换失败时级联自动回退

**Independent Test**：打开设置入口验证模型列表展示，选择非默认模型验证后续生成确实使用新模型，移除/损坏当前模型验证自动回退提示（spec Independent Test，`quickstart.md` 验证场景四）

**与 User Story 1/2 的关系**：文件层面与 US1/US2 互不重叠（US3 仅新增 `slowpath/ModelCatalog.*`、`content/SettingsPanel.qml`，并改动 `SessionController` 中与模型切换相关的独立方法/属性），可在 Foundational 完成后与 US1/US2 并行开始，核心转换闭环默认行为不受影响（spec "Why this priority"）。

### Tests for User Story 3 ⚠️

> 先写测试，确认其在对应实现完成前处于 FAIL 状态

- [X] T025 [P] [US3] `ModelCatalog` 单元测试：覆盖 `scan()` 枚举 `ModelPaths::conventionalModelDirectory()` 下全部 `*.gguf` 文件产出 `ModelDescriptor` 列表、文件名启发式分档（含 `3b`/`14b`/`32b` 关键字归类 Low/Medium/High，均不匹配归类 Unknown，`research.md` §7.2）、GGUF 文件头魔数 `"GGUF"` 校验产出 `availability=Unverified`/`KnownUnloadable`（`research.md` §7.3）于 `tests/slowpath/ModelCatalogTest.cpp`（依赖 T007）
- [X] T026 [P] [US3] 扩展 `SessionControllerTest.cpp`：验证 `switchModel()`/`openSettings()`/`closeSettings()` 行为与 `modelSwitching`/`currentModelName`/`availableModels`/`recommendedProfiles`/`fallbackNoticeText`/`settingsVisible` 属性、`modelSwitchCompleted`/`modelFallbackTriggered` 信号（`contracts/qml-cpp-session-interface.md`）；覆盖切换失败时按 FR-013 级联回退直至低配默认模型、切换过程中已有在飞 `generate()` 调用不因 `shared_ptr` 生命周期管理而崩溃（`research.md` §8）于 `tests/session/SessionControllerTest.cpp`（新增用例）

### Implementation for User Story 3

- [X] T027 [US3] 实现 `ModelCatalog::scan()`：枚举 `ModelPaths::conventionalModelDirectory()` 下全部 `*.gguf` 文件，读取文件头 4 字节校验 GGUF 魔数产出 `availability`，按文件名关键字产出 `tierHint`，构建 `ModelDescriptor` 列表 于 `babel/src/slowpath/ModelCatalog.cpp`（依赖 T007、T025）
- [X] T028 [P] [US3] 定义固定三档 `RecommendedModelProfile` 静态数据（低配 Qwen2.5-3B-Instruct GGUF Q4_K_M；中配 Qwen2.5-14B-Instruct GGUF Q4_K_M 约 9GB；高配 Qwen2.5-32B-Instruct GGUF Q4_K_M 约 19.85GB，目标硬件为 Apple M1 Pro+32GB 统一内存/Windows+RTX 4090 24GB 显存，FR-012 原文数值）于 `babel/src/slowpath/ModelCatalog.cpp`（依赖 T007）
- [X] T029 [US3] 将 `SessionController::m_llmEngine` 由 `std::unique_ptr<LlmEngine>` 改为 `std::shared_ptr<LlmEngine>`；`onSlowPathTriggered()` 后台线程按值捕获 `shared_ptr` 拷贝替代裸指针捕获，避免模型切换释放旧实例时发生 use-after-free（`research.md` §8）于 `babel/src/session/SessionController.{h,cpp}`（依赖 T026）
- [X] T030 [US3] 在 `SessionController` 中新增 `switchModel(filePath)`/`openSettings()`/`closeSettings()` 可调用方法与 `currentModelName`/`modelSwitching`/`availableModels`/`recommendedProfiles`/`fallbackNoticeText`/`settingsVisible` 属性、`modelSwitchCompleted(success, modelName)`/`modelFallbackTriggered(noticeText)` 信号（`contracts/qml-cpp-session-interface.md`）：`switchModel()` 置位 `modelSwitching=true` → 非 UI 线程构造新 `LlmEngine` → `QueuedConnection` 回主线程替换 `m_llmEngine` → 置回 `modelSwitching=false`；加载失败时按 FR-013 依次尝试 `fallbackHistory` 中此前已知可用的模型直至低配默认模型，并按原文模板渲染 `fallbackNoticeText`（"模型 {} 当前不可用，已自动切换回 {} 继续使用"）于 `babel/src/session/SessionController.{h,cpp}`（依赖 T027、T029）
- [X] T031 [US3] 新增 `babel/content/SettingsPanel.qml`：展示 `availableModels` 列表（`availability=KnownUnloadable` 条目附加置灰+不可用图标标记，仍可点选）、`recommendedProfiles` 三档推荐信息（型号/量化精度/约略体积/目标硬件描述），点选条目调用 `switchModel(filePath)`，`modelSwitching=true` 期间展示"模型加载中，请稍候"，展示 `fallbackNoticeText` 一次性提示，"返回"按钮调用 `closeSettings()` 于 `babel/content/SettingsPanel.qml`（依赖 T030）
- [X] T032 [US3] 在 `babel/content/FloatingWindow.qml` 中新增固定位于浮窗右上角的设置按钮（点击调用 `openSettings()`），并新增由 `settingsVisible` 驱动的同一 Window 内视图切换容器（`research.md` §11：默认展示既有三段式主视图，`settingsVisible=true` 时切换到 `SettingsPanel.qml`）于 `babel/content/FloatingWindow.qml`（依赖 T030、T031）
- [X] T033 [US3] 按 `quickstart.md` 验证场景四步骤 1–6 手工执行（设置入口可达、模型列表与三档推荐信息展示、非 GGUF 文件呈现"不可用"标记仍可选中、热切换生效且期间提示"加载中"、超出硬件档位选择不被拦截、模型不可用时按 FR-013 文案级联自动回退、完全退出重启后不持久化恢复低配默认模型），确认 Acceptance Scenarios 1-4 与 SC-002/SC-003 均满足（依赖 T027-T032）

**Checkpoint**：至此 User Story 1、2、3 应均可独立运行与测试

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**：GPLv3 合规收尾、性能实测记录、全量回归与验收抽样，不引入新的用户可见行为

- [X] T034 [P] 在应用关于页/文档入口（沿用 001 既有 About/文档模式，具体承载位置由实现者按既有模式确定）中补充"拼音词库数据来源于 iDvel/rime-ice（GPLv3）"的来源与许可证声明，呼应 copyleft 对"保留原始版权与来源声明"的要求（`research.md` §3 步骤 3）
- [X] T035 [P] 按 `research.md` §10 验证方法论，在可获得的低/中/高三档目标硬件（或替代实测环境）上分别实测 Qwen2.5-3B/14B/32B-Instruct 的首批内容与整句生成延迟，如实记录实测数值（不在本任务内断言是否达标，若高配档不达标按 `research.md` §10 第 3 步处理、不更换推荐模型）
  > **实测记录（本机 Apple M1 Pro，同一测试样例 "wo jintian xinqing hen bu hao wo xiang chi kaorou"，真实模型文件，非估算）**：
  > - **低配 Qwen2.5-3B-Instruct**：模型加载 2062ms（一次性成本），生成耗时 **628ms** —— 满足 SC-003（整句<800ms）；但本次实测输出语义有误（"我今天心情很不好我想吃**巧克力**"，应为"烤肉"），印证了本特性要解决的核心问题在小模型上仍可能复现，需配合 Lattice+真实词库的组合效果评估，不能仅看延迟达标。
  > - **中配 Qwen2.5-14B-Instruct**：模型加载 6057ms，生成耗时 **2579ms** —— 超出 001 SC-003 原定 800ms 整句生成阈值约 3.2 倍；但本次实测输出语义正确（"我今天心情不太好，我想吃烤肉。"）。**已提交用户决策并确认**：接受"中/高配以延迟换质量"的权衡，001 的 `spec.md` SC-003 已修订为"仅对低配默认模型强制适用"（见 001 `spec.md` Clarifications "Session 2026-10-19"），本任务无需进一步处理。
  > - **高配 Qwen2.5-32B-Instruct**：未在本沙箱环境验证（9.85GB 级别文件下载/加载成本超出本次会话可承受范围），需用户在真实目标硬件（M1 Pro 32GB 统一内存 / RTX 4090 24GB 显存）上按 `research.md` §10 步骤实测补齐（预期延迟高于 14B，但按已修订的 SC-003 不强制要求达标）。
- [X] T036 运行全量 GoogleTest 套件（`ctest --test-dir build`，由 `tests/CMakeLists.txt` 的 `gtest_discover_tests` 自动纳入全部 `*Test.cpp`）确认 001 既有测试与本特性 T008/T013-T016/T025-T026 新增测试全部通过，并按 `quickstart.md` 汇总 T012/T024 已执行的 SC-001 语义正确率抽样结果（目标 ≥80%）作为本特性验收记录

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**：无依赖，可立即开始
- **Foundational (Phase 2)**：依赖 Setup 完成（T003 不依赖 T001/T002，可与 Setup 并行启动；为清晰起见仍按顺序排列）——**阻塞**所有用户故事
- **User Stories (Phase 3-5)**：均依赖 Foundational (Phase 2) 完成后方可开始；三者文件层面互不重叠，理论上可完全并行；建议按优先级顺序 P1→P2→P3 交付以体现"真实词库→多候选消歧→模型可切换"的价值叠加（US2 候选质量依赖 US1 已扩充的词库数据，但不构成代码/编译期依赖）
- **Polish (Phase 6)**：依赖期望交付的全部用户故事完成（T034 仅依赖 US1 的 T001；T035 仅依赖 US3 的 T027-T033；T036 依赖全部期望交付的用户故事）

### User Story Dependencies

- **User Story 1 (P1)**：Foundational 完成后即可开始；不依赖 US2/US3
- **User Story 2 (P2)**：Foundational 完成后即可开始；文件层面不依赖 US1，但语义价值建立在 US1 已扩充的真实词库候选之上，建议在 US1 之后实施
- **User Story 3 (P3)**：Foundational 完成后即可开始；不依赖 US1/US2，核心转换闭环默认行为不受影响

### Within Each User Story

- 测试任务 MUST 先写、在对应实现完成前处于 FAIL 状态
- 数据结构实现先于消费该结构的业务逻辑（如 US2 内 T017/T018 先于 T019，US3 内 T027/T029 先于 T030）
- 核心类实现先于 `SessionController` 集成，`SessionController` 集成先于 QML 绑定
- 故事内全部任务完成后再执行该故事的 `quickstart.md` 验证任务

### Parallel Opportunities

- Setup 阶段 T001（资产获取）可独立执行；T002（CMake 资源收录）依赖 T001 产出文件已存在但可并行准备脚本
- Foundational 阶段 T003-T007 标记 [P]，对应不同文件，可完全并行（T006 依赖 T005，其余四项互不依赖）
- Foundational 完成后，US1/US2/US3 三个用户故事可由不同开发者完全并行推进
- 每个用户故事内标记 [P] 的测试任务可并行编写（T008 单独；T013/T014/T015/T016 四个测试文件互相独立；T025/T026 两个测试文件独立）
- US3 内 T028（静态数据）与 T027（`scan()` 实现）可并行（不同职责但同一文件 `ModelCatalog.cpp`，若需避免同文件编辑冲突可改为顺序执行）

---

## Parallel Example: User Story 2

```bash
# Foundational 完成后，US2 测试任务可并行编写：
Task: "PinyinLattice 单元测试（Top-K=8 截断/归一化/标注语法）于 tests/fastpath/PinyinLatticeTest.cpp"
Task: "扩展 PinyinTrieTest 验证带权重返回类型于 tests/fastpath/PinyinTrieTest.cpp"
Task: "扩展 FastCorrectorTest 验证 buildLattice() 多候选于 tests/fastpath/FastCorrectorTest.cpp"
Task: "扩展 LlmEngineGenerateTest 验证标注文本输入于 tests/slowpath/LlmEngineGenerateTest.cpp"
```

## Parallel Example: User Story 3

```bash
# Foundational 完成后，US3 测试任务可并行编写：
Task: "ModelCatalog 单元测试（扫描/分档/GGUF 魔数校验）于 tests/slowpath/ModelCatalogTest.cpp"
Task: "扩展 SessionControllerTest 验证 switchModel()/回退链路/并发安全于 tests/session/SessionControllerTest.cpp"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. 完成 Phase 1：Setup（词库资产准备）
2. 完成 Phase 2：Foundational（关键：阻塞所有用户故事）
3. 完成 Phase 3：User Story 1（真实词库）
4. **停下并验证**：按 `quickstart.md` 验证场景一独立测试 User Story 1
5. 若满足交付条件即可部署/演示——真实词库质量提升本身已是可独立验证的增量价值

### Incremental Delivery

1. 完成 Setup + Foundational → 基础就绪
2. 新增 User Story 1 → 独立测试 → 部署/演示（MVP！）
3. 新增 User Story 2 → 独立测试 → 部署/演示（叠加 Lattice 多候选消歧收益）
4. 新增 User Story 3 → 独立测试 → 部署/演示（叠加模型可切换能力）
5. 每个故事均在不破坏此前故事的前提下新增价值

### Parallel Team Strategy

多开发者协作时：

1. 团队共同完成 Setup + Foundational
2. Foundational 完成后：
   - 开发者 A：User Story 1（`fastpath/RimeDictLoader.*`、`DictionaryPaths.*`）
   - 开发者 B：User Story 2（`fastpath/PinyinTrie.*`/`FastCorrector.*`/`PinyinLattice.*`、`slowpath/LlmEngine.cpp`）
   - 开发者 C：User Story 3（`slowpath/ModelCatalog.*`、`content/SettingsPanel.qml`）
3. 三者均改动 `SessionController.{h,cpp}` 中各自独立的方法/属性（US1 的 `bootstrapPlatformDefaults()`、US2 的 `submitKeystrokes()`/`onSlowPathTriggered()`、US3 的 `switchModel()` 等新增方法），集成时需关注同文件合并冲突而非逻辑冲突

---

## Notes

- [P] 任务 = 不同文件、无依赖关系
- [Story] 标签将任务映射到具体用户故事，便于追溯
- 每个用户故事均应可独立完成与测试
- 实现前先确认测试处于 FAIL 状态
- 每完成一个任务或一组逻辑相关任务后提交一次
- 可在任一 Checkpoint 停下独立验证某用户故事
- 避免：模糊任务描述、同文件冲突、破坏故事独立性的跨故事依赖
- 本特性严格遵循宪章原则 V（最小必要修改）：`vcpkg.json`/根 `CMakeLists.txt` 均不变更（手写词库解析器与现有依赖已覆盖全部新增需求）
