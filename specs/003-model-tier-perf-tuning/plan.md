# Implementation Plan: 模型档位调整、GBNF 语法约束解码与防抖节奏优化

**Branch**: `003-model-tier-perf-tuning`（Git 当前实际所在分支为 `main`，延续 001/002 既有做法——本仓库不为单个特性单独建立 feature branch，特性产物以 `specs/003-model-tier-perf-tuning/` 目录隔离）

**Date**: 2026-10-20

**Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/003-model-tier-perf-tuning/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/plan-template.md` for the execution workflow.

## Summary

本特性是对 `specs/001-floating-ime-converter`（已交付 MVP）与 `specs/002-lattice-dict-model-switch`（已交付真实词库+Lattice 多候选+本地模型可切换）的后续性能优化扩展，涵盖 spec.md 中的三个落地方向（方向二投机解码已在 spec 中明确不实施，本计划不为其产生任何设计产物）：

1. **方向一**：将设置页面"中配"推荐模型由 Qwen2.5-14B-Instruct 调整为 Qwen2.5-7B-Instruct、"高配"由 Qwen2.5-32B-Instruct 调整为 Qwen2.5-14B-Instruct（FR-001/FR-002），三档结构、模型目录扫描/切换/回退机制本身不变（FR-003/FR-004）。
2. **方向三**：在 `LlmEngine::generate()` 内部引入 llama.cpp 原生 GBNF 语法约束采样，从语法层面强制排除 Lattice 内部标注符号 `⟦`/`⟧`/`|` 泄漏到最终输出，采用"最宽边界"字符集设计（仅排除这三个符号，不收窄其余任何字符）（FR-005～FR-009），`generate()` 对外签名与 QML↔C++ 契约不变（FR-006）。
3. **方向四**：`DebounceScheduler` 防抖窗口从固定 200ms 改为依据当前选定模型档位动态变化——低配 200ms（不变）、中配 500ms、高配 800ms（FR-010～FR-014），既有状态机迁移逻辑与并发保护守卫不变（FR-013）。

本计划在设计过程中**新发现 1 项**曾命中宪章判定程序 (a)（改变用户可感知的交互节奏这一外部可观察行为）且**未**被 spec 显式决定的事项——**当前生效模型的 `tierHint` 为 `Unknown`（自定义命名、不含任何官方档位关键字的模型文件）时对应的防抖窗口数值**（`research.md` §5 详述，附 3 个候选方案与 1 个 Recommendation）。按宪章原则 II，本计划当时**未**代为决策，仅记录候选方案供用户确认/委派；**用户本轮已显式决策：采纳方案①/Recommendation，复用低配 200ms**，详见下文 Constitution Check 与 `research.md` §5。该项结论已完整回填至 `research.md`/`data-model.md`/`contracts/debounce-tier-window-internal-contract.md`/`quickstart.md`，本计划不再遗留任何 `[NEEDS CLARIFICATION]`。

## Technical Context

**Language/Version**: C++23（沿用 001/002/仓库既有 `CMakeLists.txt` 配置，本特性未变更语言标准）。

**Primary Dependencies**:
- Qt6（沿用 001/002 既有依赖，本特性设置页面展示内容变化属数据层面调整，不新增 UI 框架、不新增 QML 文件——`research.md` §6 的静态数据更新直接体现在既有 `SettingsPanel.qml` 绑定的 `recommendedProfiles` 属性上，QML 侧无需修改）。
- `llama.cpp`（沿用 001/002 `research.md` 已确认的推理引擎选型，经既有 vcpkg overlay port `vcpkg/ports/llama-cpp`、版本 `b9246` 接入，已实地核实本机 `build/macos/vcpkg_installed/arm64-osx/include/llama.h` 确实提供 `llama_sampler_init_grammar()` GBNF 语法采样 API，见 `research.md` §1）；本特性扩展其**使用方式**——`LlmEngine::generate()` 内部采样管线新增语法约束与梗阻检测（`research.md` §3），引擎本身选型、加载方式（`tryLoadModel()`/`releaseModel()`）不变。
- 既有依赖 `spdlog`（延续既有诊断日志用途）、`boost-filesystem`（模型目录遍历延续既有用途）、`sqlite3`（词典存储，本特性不涉及）沿用不变。
- 单元测试框架延续 001/002 已确定的 **GoogleTest**（vcpkg 官方 port `gtest`），不新增/更换测试框架；测试二进制 macOS 沙箱 ad-hoc codesign 机制（`tests/CMakeLists.txt` POST_BUILD + `DISCOVERY_MODE PRE_TEST`）已就绪，直接复用。

**Storage**: 复用既有 `sqlite3`/文件系统约定目录机制，本特性**不**引入新的持久化存储技术，也**不**新增任何需要持久化的运行时状态——`ActiveModelRuntimeState` 结构不变，当前模型档位通过"实时对文件名重新分类"获得而非缓存/持久化（`research.md` §5，`data-model.md` 四）。

**Testing**: 延续 GoogleTest + `gtest_discover_tests`；本特性涉及以下既有测试文件的**必要更新**（非新增测试框架/新增测试目标，`/speckit.tasks` 阶段排期）：
- `tests/slowpath/ModelCatalogTest.cpp`：`TierHintClassifiesByFilenameKeyword` 用例的断言需按 `research.md` §5 新关键字表更新（`14b` 文件名现应判定为 `High` 而非 `Medium`，需新增 `7b` 关键字用例）；`RecommendedProfilesReturnFixedThreeTiers` 用例对 `modelName` 包含 `"14B"`/`"32B"` 的断言需改为 `"7B"`/`"14B"`（FR-001/FR-002）。
- `tests/slowpath/LlmEngineGenerateTest.cpp`：现有对 `⟦`/`⟧` 的断言需补充对 `|` 的同等断言（`contracts/llm-engine-grammar-internal-contract.md` 第三节）；新增梗阻场景与"合法字符不被误伐"场景的测试用例（依赖真实模型文件，环境缺失时沿用既有 `GTEST_SKIP()` 模式）。
- `tests/slowpath/DebounceSchedulerTest.cpp`：新增针对 `setDebounceWindow()` 的测试用例，验证"调用不改变既有 `state()`/`m_generationInFlight` 等字段取值，仅影响后续 `tick()` 的窗口判定"（`contracts/debounce-tier-window-internal-contract.md` 一）。

**Target Platform**: 延续 001/002 既有桌面三平台 Windows/macOS/Linux 支持范围，未扩大/缩小目标平台集合。

**Project Type**: desktop-app（延续 001/002，单一常驻 Qt6 Quick 进程，本特性不引入跨进程 IPC，未扩大该边界）。

**Performance Goals**（引用 spec Success Criteria，非本阶段新定义指标）:
- SC-001/SC-002：切换至调整后的中配/高配模型后，整句生成延迟相较 002 原对应档位实测/外推数值有可测量的降低——本计划不预先断言具体数值，留待 `/speckit.implement` 完成后实测记录（FR-009 同样明文要求"不得在规格阶段断言具体缩短幅度"）。
- SC-003：中/高配档位下因防抖触发导致的"整句重新生成"次数相较固定 200ms 基线有可测量的降低——验证方法见 `quickstart.md` 场景二。
- SC-004：覆盖高歧义语料的测试集合上，最终上屏文本不含 `⟦`/`⟧`/`|` 的验证通过率达到 100%——验证方法见 `quickstart.md` 场景三。
- SC-005：低配默认档位用户的既有延迟体验与防抖节奏（200ms）保持与 001/002 一致，不受本特性影响。

**Constraints**:
- 延续 001/002 既有约束：本地 LLM 模型文件 MUST 由用户手动下载后放置于约定本地目录，应用不随包分发模型权重；本特性新增约束——`LlmEngine::generate()` 的 GBNF 语法约束 MUST 仅作为内部实现细节引入，MUST NOT 改变其对外签名（输入输出均为 `QString`）与 001/002 已确立的 QML↔C++ 会话契约（FR-006）。
- `DebounceScheduler` 既有状态机（`Idle → Debouncing → Generating → Ready/Stale`）与 `m_generationInFlight` 并发保护守卫 MUST 不变，调整范围严格限定为"防抖窗口时长"这一参数的取值来源（FR-013）。

**Scale/Scope**: 本特性修改范围严格限定于 4 组既有文件（`ModelCatalog.{h,cpp}`、`LlmEngine.{h,cpp}`（仅 `.cpp` 需改，`.h` 签名不变）、`DebounceScheduler.{h,cpp}`、`SessionController.{h,cpp}`）及其对应的 3 个既有测试文件，**不新增任何新的源文件/头文件/QML 文件**（`data-model.md` 五"内部接口变更索引"的新增方法均以既有类的新增成员方法形式落地）。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Language (宪章《语言与文档规范》)：本 plan 及同特性 research/data-model/quickstart/contracts 的说明性文字 MUST 使用简体中文；API/路径/库名等标识符可保留英文。——本计划及全部附属产物均以简体中文撰写，符合要求。*

**原则 I–VI 逐条核对**：

- **原则 I（显式需求是唯一授权范围）**：本计划全部设计内容均可追溯至 `spec.md` FR-001～FR-014、Key Entities、Clarifications 原文，或本阶段曾新发现、现已由用户显式决策解决的事项（`Unknown` 档位防抖窗口数值复用低配 200ms）；未发现任何"顺便优化/重构"类内容混入设计产物。
- **原则 II（未决策不得默认）**：本阶段曾新发现 1 项命中判定程序 (a) 的事项（`Unknown` 档位防抖窗口数值，见 Summary 与 `research.md` §5），彼时已标记 `[NEEDS CLARIFICATION]` 并给出候选方案，**未**自行选定默认值、**未**以最佳实践消解；**用户本轮已就该项作出显式决策**（采纳方案①/Recommendation，复用低配 200ms），该决策已记录于 `research.md` §5 并回填至各设计产物，不再是待决事项。其余涉及"是否复用既有同类实现"的事项（`classifyTier()` 关键字表更新、`RecommendedModelProfile` 数值更新）均已在 `research.md` 对应小节写明来源文件位置（`ModelCatalog.cpp` 现有 `classifyTier()`/`recommendedProfiles()`），符合"已存在的同类实现取值 MAY 沿用，MUST 写明来源"的要求。
- **原则 III（推荐不是批准）**：`research.md` 各 Decision 小节中标注为"Recommendation"的候选方案（如梗阻检测方案 B、DebounceScheduler 改造方案 A）均为**内部实现细节**（判定程序 (a)–(e) 全部为否，见下），按原则 IV 由 Agent 自主决定后直接记为 Decision，不属于"产品层决策"误用推荐充当批准的情形；唯一涉及外部可观察行为的新发现事项（`Unknown` 档位窗口数值）此前明确保留 `[NEEDS CLARIFICATION]`、未将其 Recommendation 当作已批准决策——本轮经用户显式批准该 Recommendation（方案①）后，方才记为正式 Decision，批准主体为用户本人而非 Agent 自行代为批准，符合本原则对"推荐"与"批准"的区分要求。
- **原则 IV（内部细节低摩擦，外部行为高控制）**：以下事项经判定程序 (a)–(e) 核对全部为"否"（不改变外部可观察行为/公共契约/数据模型/权限/安全隐私/依赖部署/性能量级），按本原则由 Agent 自主决定，已在 `research.md`/`data-model.md` 对应小节单行记录：GBNF 语法字符串的具体转义写法（`\x7C` vs 字面量 `|`）、梗阻检测的具体代码结构（方案 B 手动分步 `apply()`）、`setDebounceWindow()` 的方法命名与落位、`classifyTierByFileName()` 的提升方式与命名、`tierDebounceWindow()` 查表函数的落位文件。
- **原则 V（最小必要修改与实施边界）**：Technical Context"Scale/Scope"已列出严格限定的 4 组既有文件 + 3 个既有测试文件，**不新增任何新源文件**；未在 `plan.md` 中确认的依赖变更为零（未新增第三方库，`llama-cpp` GBNF API 已随既有依赖版本原生提供）；未发现"为让测试通过而放宽断言"的情形——相反，本计划明确要求**收紧/更新**既有测试断言以匹配新数值（Testing 小节）。
- **原则 VI（预授权质量基线不得发明产品语义）**：`/speckit.implement` 阶段为新增的 `setDebounceWindow()`/`classifyTierByFileName()`/`tierDebounceWindow()` 等新增公开方法补充输入校验与对应测试，视为已预授权（新增公共入口的基线要求），不改变既有错误语义/测试断言结构本身。

**判定程序门禁**：对本 plan 中每一项用户未显式指定的事项，已逐条执行宪章《判定程序》(a)–(e)（详见上文原则 II/IV 小节的核对结果）。唯一命中 (a) 的事项（`Unknown` 档位防抖窗口数值）此前已标记 `[NEEDS CLARIFICATION]`，现已由用户显式决策（复用低配 200ms）解决，不再遗留；其余事项判定结果全部为"否"，已按原则 IV 自主决定并单行记录。

**阶段报告**：

1. **本阶段新增 Assumption**：无新增 Assumption——本计划 Technical Context/Constitution Check 中的全部事实陈述（llama-cpp 版本 b9246、`llama_sampler_init_grammar()` 签名、既有文件现状）均来自对本机已安装头文件与仓库现有代码的直接核实（`research.md` §1/§5 逐条注明核实依据），非主观推测。
2. **本阶段设置的默认值**：无——本阶段曾发现的唯一待决事项（`Unknown` 档位窗口数值）此前已明确标记 `[NEEDS CLARIFICATION]`、未设置默认值；该项现已由用户显式决策（非 Agent 代为设置默认值）确定为复用低配 200ms。
3. **超出显式需求的事项**：无——详见上文原则 I 核对结果；唯一的"超出范围但已被妥善处理"的事项是本阶段新发现的 `Unknown` 档位窗口数值问题，此前已落入 `[NEEDS CLARIFICATION]`，现已经用户显式决策解决，未擅自扩大为已决策内容。
4. **实际触碰的文件及必要性**：见 Project Structure 小节"Source Code"的逐文件说明。
5. **用户委派及 Agent 实际选择**：本轮用户就本阶段新发现的 1 项 `[NEEDS CLARIFICATION]`（`Unknown` 档位防抖窗口数值）作出显式决策——采纳 `research.md` §5 候选方案①/Recommendation，复用低配 200ms；该决策已回填至 `research.md`/`data-model.md`/`contracts/debounce-tier-window-internal-contract.md`/`quickstart.md`，Agent 未对该项自行拍板，决策主体为用户本人。

**Gate 结论**：**PASS，无遗留 `[NEEDS CLARIFICATION]`**。本阶段曾新发现 1 项命中判定程序 (a) 的事项（`Unknown` 档位防抖窗口数值，`research.md` §5），已由用户显式决策（采纳方案①/Recommendation，复用低配 200ms）解决，并已回填至 `research.md`/`data-model.md`/`contracts/*.md`/`quickstart.md` 全部相关位置，三/四处表述一致、无矛盾。`/speckit.tasks` 可直接为"`Unknown` 档位防抖窗口赋值"这一子任务给出最终验收标准（200ms），无需再援引"待用户确认"的占位表述。

### Post-Design Re-check（Phase 1 完成后）

`data-model.md` 与 `contracts/` 中定义的实体字段、内部接口方法均逐条对照 spec 的 FR-001～FR-014 与 Key Entities 核对来源：

- `contracts/llm-engine-grammar-internal-contract.md` 明确重申 `generate()` 对外签名不变（FR-006），行为契约第 1～4 条均可逐条追溯至 FR-005/FR-007/FR-008。
- `contracts/debounce-tier-window-internal-contract.md` 新增方法均标注"既有签名不变"，行为契约第 1～6 条可逐条追溯至 FR-010～FR-014 及用户对 `Unknown` 档位窗口数值的决策；第三节查表函数的 `Unknown` 分支已回填为 200ms（用户决策，不再保留 `[NEEDS CLARIFICATION]` 标记）。
- `data-model.md` 中"一、2"分类表变更已在 §5 给出"为何不是新增产品决策而是 FR-001/FR-002 的必然推论"的论证（14B 文件此前属 002 中配、现为本特性高配，同一模型文件的档位标签随新语义正确迁移），避免被误读为擅自扩大判断范围；"二"查表函数的 `Unknown` 行同样已回填 200ms 具体数值。
- `quickstart.md` 场景二/场景三分别完整覆盖用户要求的"切换到中/高配模型后防抖窗口变化的体感验证""构造易触发标注符号泄漏的歧义输入验证 GBNF 约束"两个指定场景，且场景二额外包含"低配基线""切回低配""并发保护回归""`Unknown` 档位复用低配 200ms"四个配套检查点，场景三额外包含"合法字符不被误伐""梗阻行为"两个配套检查点。

**本次复查结论**：**PASS（无条件，无遗留 `[NEEDS CLARIFICATION]`）**，无新增违规；此前遗留的 `[NEEDS CLARIFICATION]`（`Unknown` 档位窗口数值）已由用户显式决策（复用低配 200ms）解决，完整记录并一致回填于 `research.md` §5、`data-model.md` 二、`contracts/debounce-tier-window-internal-contract.md` 三/五/六、`quickstart.md` 场景二 2.6，各处表述一致、无矛盾，设计产物整体自洽，可供 `/speckit.tasks` 完整排期。

## Project Structure

### Documentation (this feature)

```text
specs/003-model-tier-perf-tuning/
├── spec.md               # 已有：特性规格（含 3 项已澄清 Clarifications）
├── plan.md               # 本文件（/speckit.plan 输出）
├── research.md           # Phase 0 输出
├── data-model.md         # Phase 1 输出
├── quickstart.md         # Phase 1 输出
├── contracts/             # Phase 1 输出
│   ├── llm-engine-grammar-internal-contract.md      # 新增：LlmEngine GBNF 语法采样内部实现契约
│   └── debounce-tier-window-internal-contract.md    # 新增：DebounceScheduler 动态窗口 + ModelCatalog 档位识别内部契约
├── checklists/
│   └── requirements.md   # 已有：规格质量检查单
└── tasks.md               # Phase 2 输出（由 /speckit.tasks 生成，本命令不创建）
```

**与 001/002 既有契约的关系**：本特性**不**新增/修改任何 `qml-cpp-session-interface.md` 条目——`switchModel()`/`availableModels`/`recommendedProfiles`/`currentModelName` 等现有 QML 可见属性/方法签名与语义均保持 002 既定状态不变（仅 `recommendedProfiles` 底层返回的**数据取值**按 FR-001/FR-002 更新，属性本身的类型/语义不变，故不构成契约变更）。本特性新增的两个 `contracts/*.md` 文件均标注为"内部实现契约"，供 `/speckit.tasks`/`/speckit.implement` 参考，不面向 QML 层开发者。

### Source Code (repository root)

本特性延续 001/002 已确立的单一桌面应用工程布局（`babel/` 可执行目标 + `content/` QML 资源 + `src/` 纯 C++ 业务逻辑目录），**不**引入新的顶层项目结构，**不新增任何新文件**（与 002 的差异：002 新增了多个 `[新增]` 文件，本特性仅对既有文件做 `[修改]`）。以下为本计划针对该特性确定的修改范围，供 `/speckit.tasks` 排期与 `/speckit.implement` 落地参考：

```text
babel/
└── src/
    ├── slowpath/
    │   ├── ModelCatalog.{h,cpp}      # [修改] recommendedProfiles() 静态数据按 FR-001/FR-002 更新（research.md §6）；
    │   │                               #        classifyTier() 提升为公开静态方法 classifyTierByFileName()，
    │   │                               #        关键字表新增 "7b"→Medium、"14b" 由 Medium 改判 High（research.md §5）
    │   ├── LlmEngine.cpp              # [修改] generate() 内部新增 GBNF 语法采样器（llama_sampler_init_grammar()）+
    │   │                               #        梗阻检测循环（research.md §2/§3，contracts/llm-engine-grammar-
    │   │                               #        internal-contract.md）；LlmEngine.h 对外签名不变，不修改
    │   └── DebounceScheduler.{h,cpp}  # [修改] 新增 setDebounceWindow(std::chrono::milliseconds) 方法；
    │                                   #        既有状态机字段/迁移逻辑/构造函数签名不变（research.md §4）
    └── session/
        └── SessionController.cpp      # [修改] bootstrapPlatformDefaults()/applyModelSwitchResult() 新增
                                        #        "按当前模型档位调用 setDebounceWindow()"调用点（research.md §5，
                                        #        contracts/debounce-tier-window-internal-contract.md 四）；
                                        #        SessionController.h 对外 Q_PROPERTY/Q_INVOKABLE 签名不变，不修改

tests/
└── slowpath/
    ├── ModelCatalogTest.cpp           # [修改] 更新关键字表/推荐数值相关断言（见 Technical Context Testing 小节）
    ├── LlmEngineGenerateTest.cpp      # [修改] 补充 `|` 字符断言；新增梗阻/合法字符不被误伐测试用例
    └── DebounceSchedulerTest.cpp      # [修改] 新增 setDebounceWindow() 相关测试用例
```

**Structure Decision**：沿用 001/002 既有单一桌面应用结构（非 Option 1/2/3 模板中的任何一种标准模板场景，而是延续本仓库既有的 `babel/src/<子系统>/` 分层约定），本特性修改面严格限定于 `slowpath`（`ModelCatalog`/`LlmEngine`/`DebounceScheduler`）与 `session`（`SessionController`）两个既有子系统目录内的既有文件，不新增子系统目录、不新增文件。

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

本特性 Constitution Check 无需要论证的宪章违规项（Gate 结论为 PASS，此前遗留事项——待决的产品层 `[NEEDS CLARIFICATION]`——已由用户显式决策解决，非宪章违规），本节无需填写。
