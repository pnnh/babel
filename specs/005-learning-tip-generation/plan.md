# Implementation Plan: 学习提示气泡内容生成

**Branch**: `005-learning-tip-generation`（Git 当前实际所在分支为 `main`，延续 001～004 既有做法——本仓库不为单个特性单独建立 feature branch，特性产物以 `specs/005-learning-tip-generation/` 目录隔离）

**Date**: 2026-10-21

**Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/005-learning-tip-generation/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/plan-template.md` for the execution workflow.

## Summary

本特性是对 001 中一直留空的"学习提示气泡"功能的内容生成补齐：`SessionController` 已声明的 `learningTipText`/`learningTipVisible` 只读属性与 `FloatingWindow.qml` 已有的纯文本展示区域自 001 起从未被赋予过真实内容。本特性按 spec.md 2026-10-21 Clarifications 已确定的四项前提——(1) 复用同一次慢流 `LlmEngine::generate()` 调用、不触发第二次独立模型调用，在产出标准中文译写结果的同时一并产出英文表达建议；(2) 仅展示地道英文表达本身，不附加语法点讲解；(3) 设置页面提供开关，默认开启；(4) 仅中配/高配两档目标模型启用，低配默认档位不生成、保持现有延迟预算不变——实现以下核心改动：

1. `LlmEngine::generate()` 新增带默认值参数 `includeLearningTip`（返回类型不变，仍为 `QString`），`true` 时系统提示词追加指令要求模型按"标准中文译写结果 → 固定 ASCII 分隔符 `<<<EN_TIP>>>` → 英文表达建议"的顺序输出，采样上限由既有 `256` 提升为 `400`（`research.md` §6/§7）。
2. `SessionController` 新增读写属性 `learningTipEnabled`（默认 `true`，不持久化），在 `onSlowPathTriggered()` 中按"开关开启 AND 当前档位为 Medium/High"判定本轮是否请求学习提示，调用方新增一个纯字符串处理的解析辅助函数按分隔符切分组合输出，解析失败/畸形时整段原文兜底为标准中文结果、学习提示判定为不可见（FR-004 语义）。
3. `resetSessionData()` 新增对 `m_learningTipText`/`m_learningTipVisible` 的清空，避免跨会话残留。
4. `SettingsPanel.qml` 新增一个开关控件（复用 004 `speculativeDecodingEnabled` 开关完全相同的纯 `Rectangle`+`MouseArea` 实现模式，不新增依赖）。

本计划在设计过程中**新发现 1 项**命中宪章判定程序 (a)（"中文结果与英文建议的分隔/解析格式"，直接影响 SC-003/SC-005 这一外部可观察的正确性语义）的架构选择点；**用户已在本轮 `/speckit.plan` 对话中就该项作出显式决策**：Agent 给出候选方案 1（中文标签行）/2（新增专用 Unicode 符号对）/3（纯 ASCII marker，Recommendation）三项，**用户采纳方案 3**（固定分隔符 `<<<EN_TIP>>>`）。按宪章原则 II，本计划在研究阶段**未**代为决策，仅在 `research.md` §3 记录候选方案与权衡；按宪章原则 III，Recommendation 本身不构成已批准决策，**直至用户显式批复采纳方案 3 后方视为最终决策**。决策已回填至 `research.md`/`data-model.md`/`contracts/` 对应小节，原"候选方案"占位已替换为具体内容。除该项外，本阶段核实到的其余技术事实（现有 `generate()` 行为、`ForbiddenTokenMask` 不受影响、既有"仅完成回调整体替换"更新时机、设置开关不持久化先例）均已在 `research.md` 给出明确 Decision，不遗留歧义。

## Technical Context

**Language/Version**: C++23（沿用 001～004 既有 `CMakeLists.txt` 配置，本特性未变更语言标准）。

**Primary Dependencies**:
- Qt6（沿用既有依赖；新增的 `learningTipEnabled` 读写属性延续既有 `Q_PROPERTY` 模式接入既有 `SettingsPanel.qml`，不新增 UI 框架、不新增 QML 文件）。
- `llama.cpp`（沿用既有推理引擎选型，vcpkg overlay port `llama-cpp` 版本不变；本特性不使用任何新 API，仅复用既有 `llama_sampler_init_temp`/`llama_sampler_init_dist`/`llama_decode` 等既有调用，`research.md` §1 已核实现状行为）。
- 既有依赖 `spdlog`、`boost-filesystem`、`sqlite3` 沿用不变，均**不**新增/变更任何第三方依赖；**不**引入 QSettings 或任何磁盘持久化基础设施（`research.md` §8 已核实全仓库范围内不存在任何 QSettings 用法，且 002/004 已为"设置页面功能开关"确立"不持久化"的既有先例）。
- 单元测试框架延续 GoogleTest（vcpkg 官方 port `gtest`），不新增/更换测试框架。

**Storage**: 不引入任何新的持久化存储；`m_learningTipEnabled` 与既有 `m_speculativeDecodingEnabled`/`ActiveModelRuntimeState` 同属纯运行时内存状态，每次应用启动固定重新初始化为默认值 `true`（`research.md` §8）。

**Testing**: 延续 GoogleTest + `gtest_discover_tests`；本特性涉及以下既有测试文件的更新（非新增测试框架）：
- `tests/slowpath/LlmEngineGenerateTest.cpp`：新增 `generate(text, /*includeLearningTip=*/true)` 路径下"输出包含分隔符时内容可被正确拆分""未产出分隔符时整段原文视为标准中文结果"等真实模型依赖用例（模型文件缺失时按既有惯例 `GTEST_SKIP()`）。
- `tests/session/SessionControllerTest.cpp`：新增 `learningTipEnabled` 属性读写/`NOTIFY` 断言、低配档位恒不展示学习提示、设置开关关闭后立即隐藏、跨会话重置清空等场景用例（注入测试替身 `LlmEngine`/`DebounceScheduler`，不依赖真实模型文件）。

**Target Platform**: 延续 001～004 既有桌面三平台 Windows/macOS/Linux 支持范围，未扩大/缩小目标平台集合。

**Project Type**: desktop-app（延续既有单一常驻 Qt6 Quick 进程，本特性不引入跨进程 IPC）。

**Performance Goals**（引用 spec Success Criteria，非本阶段新定义指标）:
- SC-001/SC-002：中配/高配档位、开关默认开启时，用户能在无需额外操作的前提下看到与本轮输入/输出语义相关的非空英文表达建议。
- SC-003/SC-005：英文建议与标准中文输出预览在语义上保持对应，多轮重新生成时同步更新，不出现可感知的"建议与预览不符"的情况（`research.md` §2 已确定的"仅完成回调整体替换"时机设计是达成此标准的关键机制）。
- SC-004：模型不可用/生成失败/解析判定无效时，学习提示区域始终保持不可见。

**Constraints**:
- `LlmEngine::generate()` 单参数调用形式与返回类型（`QString`）MUST 保持与 001～004 完全兼容（`research.md` §7），本特性仅新增一个带默认值的参数，不删除/不改变任何既有调用形态。
- 低配默认档位（Qwen2.5-3B-Instruct）MUST NOT 因本特性产生任何额外的 prompt 构造、采样上限提升或后处理解析开销（FR-009，`research.md` §5：判定条件在构造 prompt **之前**完成，而非"生成后丢弃"）。
- 既有 GBNF/`ForbiddenTokenMask` 黑名单（仅排除 ⟦/⟧/|）MUST NOT 因本特性扩展或改变（`research.md` §4），不得重新引入 `llama_sampler_init_grammar` 通用语法引擎。

**Scale/Scope**: 本特性修改范围严格限定于 `LlmEngine.{h,cpp}`、`SessionController.{h,cpp}`、`SettingsPanel.qml`（`FloatingWindow.qml` 既有绑定代码预期无需改动）三组既有文件及其对应的既有测试文件，**不新增新的源文件/头文件/QML 文件**。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Language (宪章《语言与文档规范》)：本 plan 及同特性 research/data-model/quickstart/contracts 的说明性文字 MUST 使用简体中文；API/路径/库名等标识符可保留英文。*

**原则 I–VI 逐条核对**：

- **原则 I（显式需求是唯一授权范围）**：本计划全部设计内容均可追溯至 `spec.md` FR-001～FR-009、Key Entities、Clarifications 原文，或本阶段新发现、已交由用户显式决策且未由 Agent 擅自拍板的 1 项事项（分隔符格式，已取得用户显式决策）；未发现任何"顺便优化/重构"类内容混入设计产物（如未借机调整既有防抖窗口数值、未借机修改既有 `kSystemPrompt` 中与学习提示无关的部分、未借机为 `speculativeDecodingEnabled` 补充持久化）。
- **原则 II（未决策不得默认）**：本阶段新发现的 **1 项**命中判定程序 (a) 的事项（分隔符格式）未由 Agent 自行选定默认值、未以最佳实践消解，而是在 `research.md` §3 汇总为候选方案 + Recommendation，交由用户显式决策；**用户已就此作出显式决策**（采纳方案 3），决策依据已记录于 `research.md` §3"用户决策"小节，并回填至 `data-model.md`/`contracts/llm-engine-learning-tip-contract.md` 对应字段，不再存在占位。其余涉及"是否存在现成同类实现可沿用"的事项（设置开关不持久化策略、学习提示更新时机、档位判定查表方式）均已在 `research.md` 对应小节写明核实依据与来源文件位置，符合"已存在的同类实现取值 MAY 沿用，MUST 写明来源"的要求。
- **原则 III（推荐不是批准）**：`research.md` §3 的 Recommendation（方案 3）在给出时**明确标注**"仅供参考"；本阶段**未**在用户批复前将 Recommendation 当作既定决策写入设计产物的最终字段。用户随后在本轮对话中以显式指令采纳方案 3，构成宪章原则 III 所要求的"用户显式批准"，此后方将对应结果回填至 `data-model.md`/`contracts/` 的最终字段，决策链条（候选→Recommendation→用户显式批准→回填）完整、无跳步。
- **原则 IV（内部细节低摩擦，外部行为高控制）**：以下事项经判定程序 (a)–(e) 核对全部为"否"，按本原则由 Agent 自主决定，已在 `research.md` 对应小节记录：`generate()` 新增参数的具体签名形态（§7）、`kMaxOutputTokensWithLearningTip` 的具体数值选型（§6）、解析辅助函数 `parseLearningTipOutput()` 的放置位置与具体解析算法细节（§7、`data-model.md` 三.5）、学习提示更新时机复用既有"仅完成回调整体替换"模式（§2）、档位判定逻辑是否需要独立查表方法（`data-model.md` 四：不需要，内联布尔表达式即可）。
- **原则 V（最小必要修改与实施边界）**：Technical Context"Scale/Scope"已列出严格限定的修改范围，**不新增新的源文件**；未在本计划中确认任何新依赖变更；未发现"为让测试通过而放宽断言"的情形；`generate()` 签名变更选择"新增带默认值参数、保持返回类型不变"这一对既有调用点影响最小的方案，而非改动面更大的"返回结构体"方案（`research.md` §7 Alternatives considered 已记录并否决后者）。
- **原则 VI（预授权质量基线不得发明产品语义）**：`/speckit.implement` 阶段为新增的内部辅助函数（`parseLearningTipOutput()`）补充对应测试，视为已预授权（新增逻辑的基线要求），不改变既有错误语义/测试断言结构本身；`kLearningTipInstructionSuffix` 的具体措辞文案（除 FR-001/FR-003 原文已明确的三项要素外）MAY 由 `/speckit.tasks` 阶段按该基线自主补充，不构成"发明产品语义"（FR-001/FR-003 已明确该提示词必须满足的产品语义本身，文案措辞细节不属于该范畴）。

**判定程序门禁**：对本 plan 中每一项用户未显式指定的事项，已逐条执行宪章《判定程序》(a)–(e)（详见上文原则 II/IV 小节的核对结果）。**1 项**命中 (a) 的事项（分隔符格式）此前已标记 `[NEEDS CLARIFICATION]`、未由 Agent 代为决策，现已取得用户显式决策并回填；其余事项判定结果全部为"否"，已按原则 IV 自主决定并记录。本计划**无遗留** `[NEEDS CLARIFICATION]`。

**阶段报告**：

1. **本阶段新增 Assumption**：无新增 Assumption——本计划 Technical Context/Constitution Check 中的全部事实陈述（全仓库不存在 QSettings 用法、`ForbiddenTokenMask` 仅排除 ⟦/⟧/| 三码点、既有 `outputPreviewText` 更新时机、002/004 持久化策略先例）均来自对仓库现有代码的直接核实（`research.md` §1/§2/§4/§8 逐条注明核实依据与来源文件），非主观推测。
2. **本阶段设置的默认值**：无——本阶段发现的 1 项事项（分隔符格式）经用户显式决策（非 Agent 代为选定默认值），决策结果见 `research.md` §3"用户决策"小节。其余"默认值"性质的取值（`kMaxOutputTokensWithLearningTip = 400`、`learningTipEnabled` 默认 `true`）分别来自 spec.md FR-008 原文直接规定（默认开启）与宪章原则 IV 授权的内部实现细节（采样上限数值，与既有 `kMaxOutputTokens = 256` 同等粒度的工程选型，`research.md` §6 已说明理由与否决的替代方案）。
3. **超出显式需求的事项**：无——详见上文原则 I 核对结果；唯一的"超出 spec.md 字面范围但已被妥善处理"的事项是本阶段新发现的 1 项事项（分隔符格式），已交由用户决策并取得显式批复，未被擅自扩大或假定。
4. **实际触碰的文件及必要性**：见 Project Structure 小节"Source Code"的逐文件说明。
5. **用户委派及 Agent 实际选择**：本计划设计阶段**无**用户委派——1 项事项（分隔符格式）为本计划主动标记、交由用户后续决策，Agent 未对此自行拍板；用户随后在本轮对话中以显式指令采纳方案 3，该决策为用户本人作出，非委派 Agent 代为选择，亦非 Agent 的 Recommendation 自动生效。

**Gate 结论**：**PASS——此前 1 项待用户显式决策的 `[NEEDS CLARIFICATION]`（研究阶段新发现，非 spec.md 原有 4 项，后者均已于 Clarifications 中解决）已由用户显式决策（采纳方案 3）并回填至全部相关设计产物，无条件通过**。

### Post-Design Re-check（Phase 1 完成后，决策回填后再次复查）

`data-model.md` 与 `contracts/` 中定义的实体字段、内部接口方法均逐条对照 spec 的 FR-001～FR-009 与 Key Entities 核对来源：

- `contracts/qml-cpp-session-interface-increment.md` 明确重申本特性对 002（已含 004 增量）既有契约"仅一处预先授权的增量"（新增 `learningTipEnabled` 读写属性），并给出判定程序 (a)–(e) 核对结果，说明该增量本身不构成新的 `[NEEDS CLARIFICATION]`；`learningTipText`/`learningTipVisible` 两个 001 已声明属性的读取语义本身不变，只是首次被赋予真实内容来源，这正是本特性的核心授权范围。
- `contracts/llm-engine-learning-tip-contract.md` 第一节"对外签名变更"、第二节"行为契约"均已按用户采纳的方案 3 回填为确定结论（固定分隔符 `<<<EN_TIP>>>`、不扩展 `ForbiddenTokenMask`），无遗留"待决架构问题"表述。
- `data-model.md` 一"学习提示建议"、二"`LlmEngine` 接口增量"、三"`SessionController` 接口增量"均已回填为确定值，与 `research.md` §3/§5/§6/§7/§8 的 Decision 一一对应，无遗漏、无矛盾。
- `quickstart.md` 场景一～五分别完整覆盖 spec.md User Story 1/2 的全部验收场景与 SC-001～SC-005，"实测记录"小节提醒本阶段不预先编造任何延迟数值。

**本次复查结论**：**PASS（无条件通过，无新增违规，无遗留事项）**；此前标记的 1 项 `[NEEDS CLARIFICATION]`（分隔符格式）已由用户显式决策（方案 3）并回填至全部设计产物，各处表述一致、无矛盾，可直接进入 `/speckit.tasks` 的最终排期与 `/speckit.implement` 的实现阶段。

## Project Structure

### Documentation (this feature)

```text
specs/005-learning-tip-generation/
├── spec.md               # 已有：特性规格（含 4 项已澄清 Clarifications）
├── plan.md               # 本文件（/speckit.plan 输出）
├── research.md           # Phase 0 输出（含 §3 用户决策，§9 汇总决策结果，无遗留 NEEDS CLARIFICATION）
├── data-model.md         # Phase 1 输出（LlmEngine/SessionController 接口增量，字段均已回填最终值）
├── quickstart.md         # Phase 1 输出
├── contracts/             # Phase 1 输出
│   ├── llm-engine-learning-tip-contract.md            # 新增：LlmEngine 双段输出内部实现契约（分隔符已按方案 3 确定）
│   └── qml-cpp-session-interface-increment.md         # 新增：对 002（含 004 增量）QML↔C++ 契约的第二处预授权增量
├── checklists/
│   └── requirements.md   # 已有：规格质量检查单
└── tasks.md               # Phase 2 输出（由 /speckit.tasks 生成，本命令不创建）
```

**与 001/002/003/004 既有契约的关系**：本特性对 `qml-cpp-session-interface.md`（含 004 增量）做**第二处**增量（新增 `learningTipEnabled` 读写属性，`contracts/qml-cpp-session-interface-increment.md`），`switchModel()`/`availableModels`/`recommendedProfiles`/`currentModelName`/`modelSwitching`/`fallbackNoticeText`/`settingsVisible`/`speculativeDecodingNoticeText`/`recommendedDraftProfiles`/`speculativeDecodingEnabled` 等既有属性/方法/信号签名与语义均保持既定状态不变。本特性新增的另一份 `contracts/llm-engine-learning-tip-contract.md` 标注为"内部实现契约"，与既有 `llm-engine-grammar-internal-contract.md`（003）、`llm-engine-speculative-internal-contract.md`（004）并列，供 `/speckit.tasks`/`/speckit.implement` 参考，不面向 QML 层开发者。

### Source Code (repository root)

本特性延续 001～004 已确立的单一桌面应用工程布局（`babel/` 可执行目标 + `content/` QML 资源 + `src/` 纯 C++ 业务逻辑目录），**不**引入新的顶层项目结构、**不新增任何新文件**。以下为本计划针对该特性确定的修改范围：

```text
babel/
├── src/
│   ├── slowpath/
│   │   └── LlmEngine.{h,cpp}          # [修改] 新增 kLearningTipSeparator 公开常量、generate() 的
│   │                                   #        includeLearningTip 参数（默认 false，返回类型不变）、
│   │                                   #        includeLearningTip=true 分支下的系统提示词后缀拼接与
│   │                                   #        kMaxOutputTokensWithLearningTip=400 采样上限（research.md
│   │                                   #        §3/§6/§7，contracts/llm-engine-learning-tip-contract.md）；
│   │                                   #        既有 ForbiddenTokenMask/GBNF 黑名单、投机解码分支逻辑不变
│   └── session/
│       └── SessionController.{h,cpp}  # [修改] 新增 learningTipEnabled 读写属性（默认 true，不持久化）、
│                                       #        onSlowPathTriggered() 内新增档位+开关判定与 includeLearningTip
│                                       #        传参、新增内部解析辅助函数 parseLearningTipOutput()、
│                                       #        resetSessionData() 新增对 learningTipText/learningTipVisible
│                                       #        的清空（contracts/qml-cpp-session-interface-increment.md，
│                                       #        data-model.md 三）
└── content/
    └── SettingsPanel.qml              # [修改] 新增学习提示开关控件（复用 004 speculativeDecodingEnabled
                                        #        开关完全相同的纯 Rectangle+MouseArea 实现模式）
                                        #        （FloatingWindow.qml 的 learningTipVisible/learningTipText
                                        #        绑定代码已存在，预期无需改动，仅在发现确有必要时才触碰）

tests/
├── slowpath/
│   └── LlmEngineGenerateTest.cpp      # [修改] 新增 includeLearningTip=true 路径下分隔符解析相关用例
└── session/
    └── SessionControllerTest.cpp      # [修改] 新增 learningTipEnabled 属性/低配档位恒不展示/开关关闭立即
                                        #        隐藏/跨会话重置清空等用例
```

**Structure Decision**：沿用 001～004 既有单一桌面应用结构（非 Option 1/2/3 模板中的任何一种标准模板场景，而是延续本仓库既有的 `babel/src/<子系统>/` 分层约定），本特性修改面严格限定于 `slowpath`（`LlmEngine`）与 `session`（`SessionController`）两个既有子系统目录内的既有文件，以及 `content/SettingsPanel.qml` 一个既有 QML 文件，确定不新增任何新文件。

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

本特性 Constitution Check 无需要论证的宪章**违规**项——Gate 结论为 PASS，此前 1 项事项（分隔符格式）是"待用户显式决策的架构分歧点"而非"已违反宪章却需要论证豁免的情形"，现已由用户显式决策（方案 3）解决（性质与宪章原则 II 要求的正常流程一致，不构成"Complexity Tracking"意义上的违规豁免）。本表无需填写。
