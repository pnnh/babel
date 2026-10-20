# Implementation Plan: 投机解码（Speculative Decoding）性能优化

**Branch**: `004-speculative-decoding`（Git 当前实际所在分支为 `main`，延续 001/002/003 既有做法——本仓库不为单个特性单独建立 feature branch，特性产物以 `specs/004-speculative-decoding/` 目录隔离）

**Date**: 2026-10-20

**Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/004-speculative-decoding/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/plan-template.md` for the execution workflow.

## Summary

本特性是对 `specs/001-floating-ime-converter`、`specs/002-lattice-dict-model-switch`、`specs/003-model-tier-perf-tuning` 三项已交付特性的后续性能优化专项（003 spec"方向二：投机解码"小节已记录该方向为"已识别但当时不实施，留待后续专项"，本特性即是该专项）。核心交付：在中配（Qwen2.5-7B-Instruct）、高配（Qwen2.5-14B-Instruct）两档目标模型档位下，引入一个独立的、参数规模更小的"草稿模型"（Qwen2.5-0.5B-Instruct / Qwen2.5-1.5B-Instruct 两档候选规格之一），由草稿模型快速产出候选 token 序列、目标模型一次性批量验证，使整句生成延迟相较不启用投机解码时有可测量的降低，同时保证最终译文在语义上与目标模型独立逐 token 生成完全等价（FR-001）；低配默认档位（Qwen2.5-3B-Instruct）不启用、不受影响（FR-005/FR-009）；草稿模型缺失时静默降级为既有单模型生成路径，并在设置页面给出明确提示（FR-006）；草稿模型与目标模型的搭配关系由应用根据目标模型档位自动固定（FR-007），用户无需手动指定。`LlmEngine::generate()` 对外签名与 QML↔C++ 既有会话契约保持不变（FR-002），仅新增一处预先授权的只读属性契约增量（设置页面"未启用加速"提示）。

本计划在设计过程中**新发现 2 项**命中宪章判定程序 (a)/(e)（分别涉及"是否改变 `SessionController` 对外可观察行为的实现复杂度归属"与"草稿模型具体搭配取值这一用户可感知的功能行为"）、且用户本轮已明确指令"不要在 plan 阶段拍板"的事项；**用户已在本轮后续对话中就这 2 项分别作出显式决策**：

1. **SessionController ↔ LlmEngine 架构划分**（`research.md` §3）：候选方案 A（`SessionController` 同时持有目标/草稿两个 `LlmEngine` 实例）/ B（`LlmEngine` 内部封装双模型逻辑，对 `SessionController` 透明，Recommendation）/ C（新增独立协调类）。**用户决策：采纳方案 B**。
2. **草稿-目标模型档位搭配映射的具体取值**（`research.md` §5.3）：候选方案 ①（Medium↔0.5B、High↔1.5B，Recommendation）/ ②（均↔1.5B）/ ③（均↔0.5B）/ ④（不区分档位、按已下载文件统一搭配，与 FR-007"固定搭配"措辞存在张力）。**用户决策：采纳方案①**。

按宪章原则 II，本计划在研究阶段**未**代为决策，仅在 `research.md` §3/§5.3 记录候选方案与权衡；按宪章原则 III，Recommendation 本身不构成已批准决策，**直至用户显式批复采纳方案 B、方案①后方视为最终决策**。上述决策已回填至 `research.md`/`data-model.md`/`contracts/`/`quickstart.md` 对应小节（原"【待决】"/"故意留空"占位处均已替换为具体内容）。除上述两项外，本阶段核实到的其余技术事实（llama.cpp 原生 API 不存在但底层原语足以手写实现、GBNF 两侧均需应用、`classifyTierByFileName()` 无需修改、固定窗口大小无技术障碍）均已在 `research.md` 给出明确 Decision，不遗留歧义。

## Technical Context

**Language/Version**: C++23（沿用 001/002/003/仓库既有 `CMakeLists.txt` 配置，本特性未变更语言标准）。

**Primary Dependencies**:
- Qt6（沿用 001/002/003 既有依赖；本特性新增的两个只读属性 `speculativeDecodingNoticeText`/`recommendedDraftProfiles` 延续既有 `Q_PROPERTY` 模式接入既有 `SettingsPanel.qml`，不新增 UI 框架、不新增 QML 文件——具体 QML 绑定改动属 `/speckit.tasks` 范围）。
- `llama.cpp`（沿用 001/002/003 既有推理引擎选型，经既有 vcpkg overlay port `vcpkg/ports/llama-cpp`、版本 `b9246` 接入，**未升级依赖版本**；已实地核实本机 `build/macos/vcpkg_installed/arm64-osx/include/llama.h` **不提供**任何 `speculative`/`draft` 命名的高层封装 API，但 `llama_batch_init()`/`llama_decode()`/`llama_get_logits_ith()`/`llama_memory_seq_rm()`/`llama_memory_seq_pos_min/max()` 等既有底层原语组合后技术上足以手写"草稿-验证"循环，`research.md` §1/§2 逐项列出核实依据与函数签名）。
- 既有依赖 `spdlog`（延续既有诊断日志用途）、`boost-filesystem`（模型目录遍历延续既有用途）、`sqlite3`（词典存储，本特性不涉及）沿用不变，均**不**新增/变更任何第三方依赖（`research.md` §2 已否决"升级 llama.cpp 版本以获得假想中的高层 API"方案）。
- 单元测试框架延续 001/002/003 已确定的 **GoogleTest**（vcpkg 官方 port `gtest`），不新增/更换测试框架；测试二进制 macOS 沙箱 ad-hoc codesign 机制（`tests/CMakeLists.txt` POST_BUILD + `DISCOVERY_MODE PRE_TEST`）已就绪，直接复用，核实结论见 `research.md` §8（无需任何 CMake 配置改动）。

**Storage**: 复用既有 `sqlite3`/文件系统约定目录机制，本特性**不**引入新的持久化存储技术；新增的 `SpeculativeDecodingProfile`（`data-model.md` 三）与既有 `ActiveModelRuntimeState` 同属纯运行时内存状态，不持久化（延续 002 `research.md` §9"每次应用启动固定重新初始化"的既有实践）；草稿模型文件与目标模型文件共享同一约定模型目录（`ModelPaths::conventionalModelDirectory()`），不新增独立目录。

**Testing**: 延续 GoogleTest + `gtest_discover_tests`；本特性涉及以下既有/新增测试文件的更新（非新增测试框架，具体用例设计已按 §3/§5.3 两项用户决策——方案 B、方案①——最终定稿，`/speckit.tasks` 阶段据此排期）：
- `tests/slowpath/ModelCatalogTest.cpp`：新增 `classifyDraftCandidateByFileName()`/`recommendedDraftProfiles()`/`pairedDraftCandidate()` 相关用例（`contracts/model-catalog-draft-pairing-internal-contract.md` 二）。
- `tests/slowpath/LlmEngineGenerateTest.cpp`（方案 B 已确定：沿用既有测试文件，不新增同目录测试文件）：新增投机解码路径下"语义等价""语法约束两侧均生效""草稿模型缺失时静默降级"等用例（`contracts/llm-engine-speculative-internal-contract.md` 二）。
- `tests/session/SessionControllerTest.cpp`：新增 `speculativeDecodingNoticeText`/`recommendedDraftProfiles` 属性的断言，以及"切换档位后自动重新匹配草稿模型"场景（`quickstart.md` 场景三）。

**Target Platform**: 延续 001/002/003 既有桌面三平台 Windows/macOS/Linux 支持范围，未扩大/缩小目标平台集合；本机实测环境仍为 macOS（`research.md` §4 测量方法论注明口径）。

**Project Type**: desktop-app（延续 001/002/003，单一常驻 Qt6 Quick 进程，本特性不引入跨进程 IPC，未扩大该边界）。

**Performance Goals**（引用 spec Success Criteria，非本阶段新定义指标）:
- SC-001/SC-002：中配/高配档位在已正确配置草稿模型前提下，整句生成延迟相较不启用投机解码时的既有延迟有可测量的降低——本计划不预先断言具体数值/倍数，留待 `/speckit.implement` 完成后实测记录（`quickstart.md` 场景一、"实测记录"小节）。
- SC-003：启用与不启用投机解码两种路径对同一输入产出的最终文本在语义正确性上保持一致，验证通过率 100%——验证方法见 `quickstart.md` 场景一 1.1 步骤 5。
- SC-004：未配置草稿模型时，生成功能成功率与既有（003 交付时）单模型生成路径保持一致——验证方法见 `quickstart.md` 场景二。
- SC-005：低配默认档位用户既有延迟体验与交互行为不受本特性影响——验证方法见 `quickstart.md` 场景一 1.3。

**Constraints**:
- 延续 001/002/003 既有约束：本地 LLM 模型文件（含本特性新增的草稿模型）MUST 由用户手动下载后放置于约定本地目录，应用不随包分发模型权重、不新增任何网络自动下载能力（FR-004）。
- `LlmEngine::generate()` 的对外签名（输入输出均为 `QString`）与 001/002/003 已确立的 QML↔C++ 会话契约 MUST NOT 因投机解码的引入而改变（FR-002），唯一预先授权的例外是 `contracts/qml-cpp-session-interface-increment.md` 记录的两个新增只读属性。
- 003 已引入的 GBNF 语法约束采样 MUST NOT 被投机解码路径削弱或绕过（FR-003），草稿模型候选生成阶段与目标模型验证阶段均须应用该约束（`research.md` §6）。
- 投机解码 MUST 仅对中配、高配两档目标模型启用，低配默认档位 MUST NOT 启用、MUST NOT 承担任何因此产生的额外初始化开销（FR-005/FR-009）。

**Scale/Scope**: 本特性修改范围严格限定于 `ModelCatalog.{h,cpp}`、`LlmEngine.{h,cpp}`、`SessionController.{h,cpp}` 三组既有文件及其对应的既有测试文件，**不新增新的源文件/头文件/QML 文件**（`research.md` §3 用户已决策采纳方案 B——`LlmEngine` 内部封装双模型逻辑，候选方案 C"新增独立协调类"未被选中，故本特性确定不新增协调类文件）。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Language (宪章《语言与文档规范》)：本 plan 及同特性 research/data-model/quickstart/contracts 的说明性文字 MUST 使用简体中文；API/路径/库名等标识符可保留英文。*

**原则 I–VI 逐条核对**：

- **原则 I（显式需求是唯一授权范围）**：本计划全部设计内容均可追溯至 `spec.md` FR-001～FR-009、Key Entities、Clarifications 原文，或本阶段曾新发现、已交由用户显式决策且未由 Agent 擅自拍板的事项（§3 架构划分、§5.3 搭配映射取值，均已取得用户显式决策）；未发现任何"顺便优化/重构"类内容混入设计产物（如未借机调整 003 已确立的防抖窗口数值、未借机修改 GBNF 语法规则本身）。
- **原则 II（未决策不得默认）**：本阶段新发现的 **2 项**命中判定程序 (a)/(e) 的事项（§3/§5.3，详见 Summary），均未由 Agent 自行选定默认值、未以最佳实践消解，而是在 `research.md` §9 汇总为候选方案 + Recommendation，交由用户显式决策；**用户已就这 2 项分别作出显式决策**（§3 采纳方案 B、§5.3 采纳方案①），决策依据已记录于 `research.md` §3/§5.3"用户决策"小节，并回填至 `data-model.md`/`contracts/model-catalog-draft-pairing-internal-contract.md`/`contracts/llm-engine-speculative-internal-contract.md`/`quickstart.md` 对应"最终"字段，不再存在占位。其余涉及"是否存在现成同类实现可沿用"的事项（`classifyTierByFileName()` 无需修改、GBNF 两侧应用方式、`K` 固定为常量）均已在 `research.md` 对应小节写明核实依据与来源文件位置，符合"已存在的同类实现取值 MAY 沿用，MUST 写明来源"的要求。
- **原则 III（推荐不是批准）**：`research.md` §3/§5.3 的 Recommendation（方案 B、方案①）在给出时均**明确标注**"仅供参考，MUST NOT 视为已批准决策"；本阶段**未**在用户批复前将 Recommendation 当作既定决策写入设计产物的"最终"字段。用户随后在本轮对话中以显式指令采纳方案 B、方案①，构成宪章原则 III 所要求的"用户显式批准"，此后方将对应结果回填至 `data-model.md`/`contracts/` 的"最终"字段，决策链条（候选→Recommendation→用户显式批准→回填）完整、无跳步。
- **原则 IV（内部细节低摩擦，外部行为高控制）**：以下事项经判定程序 (a)–(e) 核对全部为"否"，按本原则由 Agent 自主决定，已在 `research.md` 对应小节单行/整节记录：手写草稿-验证循环的具体代码结构（§2 四步循环）、GBNF 语法约束在草稿/目标两侧均应用且状态同步的实现方式（§6）、`classifyDraftCandidateByFileName()`/`recommendedDraftProfiles()` 的方法命名与落位（§5.1/§5.2）、窗口大小 `K` 固定为内部常量而非自适应（§7）、测试文件/CMake 配置无需变更（§8）。
- **原则 V（最小必要修改与实施边界）**：Technical Context"Scale/Scope"已列出严格限定的修改范围，**不新增新的源文件**（§3 用户已决策采纳方案 B，候选方案 C"新增独立协调类"未被选中，确定不涉及新增文件）；未在本计划中确认任何新依赖变更（`research.md` §2 已明确否决"升级 llama.cpp 以获取假想 API"）；未发现"为让测试通过而放宽断言"的情形。
- **原则 VI（预授权质量基线不得发明产品语义）**：`/speckit.implement` 阶段为新增的公共方法（`classifyDraftCandidateByFileName()`/`recommendedDraftProfiles()`/`pairedDraftCandidate()` 等）补充输入校验与对应测试，视为已预授权（新增公共入口的基线要求），不改变既有错误语义/测试断言结构本身；`speculativeDecodingNoticeText` 的具体提示文案措辞（除 FR-006 原文示例外）MAY 由 `/speckit.tasks` 阶段按该基线自主补充，不构成"发明产品语义"（FR-006 已明确该提示"必须存在且告知原因"这一产品语义本身，文案措辞细节不属于该范畴）。

**判定程序门禁**：对本 plan 中每一项用户未显式指定的事项，已逐条执行宪章《判定程序》(a)–(e)（详见上文原则 II/IV 小节的核对结果）。**2 项**命中 (a)/(e) 的事项（§3/§5.3）此前已标记 `[NEEDS CLARIFICATION]`、未由 Agent 代为决策，现已取得用户显式决策并回填；其余事项判定结果全部为"否"，已按原则 IV 自主决定并记录。本计划**无遗留** `[NEEDS CLARIFICATION]`。

**阶段报告**：

1. **本阶段新增 Assumption**：无新增 Assumption——本计划 Technical Context/Constitution Check 中的全部事实陈述（llama.cpp 版本 b9246、`llama.h` 不提供 speculative/draft 命名 API、既有 `ModelCatalog`/`LlmEngine`/`SessionController` 现状、候选草稿文件名与既有关键字表无冲突）均来自对本机已安装头文件与仓库现有代码的直接核实（`research.md` §1/§2/§5.1/§8 逐条注明核实依据，含具体命令与行号），非主观推测。
2. **本阶段设置的默认值**：无——本阶段发现的 2 项事项（§3/§5.3）均经用户显式决策（非 Agent 代为选定默认值），决策结果见 `research.md` §3/§5.3"用户决策"小节。
3. **超出显式需求的事项**：无——详见上文原则 I 核对结果；唯一的"超出 spec.md 字面范围但已被妥善处理"的事项是本阶段新发现的 2 项事项，均已交由用户决策并取得显式批复，未被擅自扩大或假定。
4. **实际触碰的文件及必要性**：见 Project Structure 小节"Source Code"的逐文件说明。
5. **用户委派及 Agent 实际选择**：本计划设计阶段**无**用户委派——2 项事项（§3/§5.3）均为本计划主动标记、交由用户后续决策，Agent 未对这两项自行拍板；用户随后在本轮对话中以显式指令分别采纳方案 B、方案①，该决策为用户本人作出，非委派 Agent 代为选择，亦非 Agent 的 Recommendation 自动生效。

**Gate 结论**：**PASS——此前 2 项待用户显式决策的 `[NEEDS CLARIFICATION]`（研究阶段新发现，非 spec.md 原有 4 项，后者均已于 Clarifications 中解决）已由用户分别显式决策（§3 采纳方案 B、§5.3 采纳方案①）并回填至全部相关设计产物，无条件通过**。本计划的设计产物（`research.md`/`data-model.md`/`contracts/`/`quickstart.md`）全部内容已自洽、无矛盾、无遗留待决事项，可全量进入 `/speckit.tasks` 排期与 `/speckit.implement` 实现阶段。

### Post-Design Re-check（Phase 1 完成后，决策回填后再次复查）

`data-model.md` 与 `contracts/` 中定义的实体字段、内部接口方法均逐条对照 spec 的 FR-001～FR-009 与 Key Entities 核对来源：

- `contracts/qml-cpp-session-interface-increment.md` 明确重申本特性对 002 既有契约"仅一处预先授权的增量"，并给出判定程序 (a)–(e) 核对结果，说明该增量本身不构成新的 `[NEEDS CLARIFICATION]`。
- `contracts/llm-engine-speculative-internal-contract.md` 第二节"行为契约"与宿主架构无关，无论宿主是谁均须成立；第四节此前"依赖待决架构问题的部分"已按用户采纳的方案 B 回填为确定结论（构造函数签名扩展以接受可选草稿模型路径、生命周期由 `LlmEngine` 内部统一管理）。
- `contracts/model-catalog-draft-pairing-internal-contract.md` 第三节"档位搭配映射表"已按用户采纳的方案①回填为具体返回值（`pairedDraftCandidate(Medium) = Size0_5B`、`pairedDraftCandidate(High) = Size1_5B`），行为契约第五条已更新为可直接断言的具体取值。
- `data-model.md` 三"SpeculativeDecodingProfile"的 `pairedDraft` 字段、"具体宿主位置"、六"差异对照表"均已回填为确定值，与 `research.md` §3/§5.3"用户决策"小节一一对应，无遗漏、无矛盾。
- `quickstart.md` 场景一/二/三分别完整覆盖用户要求的"切换到中/高配模型且已配置对应草稿模型后，验证生成延迟相比不启用投机解码时有可测量降低、且翻译质量不下降""未配置草稿模型时设置页面正确提示"两个指定场景，以及"实测记录"小节提醒本阶段不预先编造任何数值；场景一/二/前置条件中原"以回填后的最终映射为准"/"示例措辞"等表述已替换为用户最终决策的具体映射（Medium↔0.5B、High↔1.5B）。

**本次复查结论**：**PASS（无条件通过，无新增违规，无遗留事项）**；此前标记的 2 项 `[NEEDS CLARIFICATION]`（§3/§5.3）已由用户显式决策（方案 B、方案①）并回填至全部设计产物，各处表述一致、无矛盾，可直接进入 `/speckit.tasks` 的最终排期与 `/speckit.implement` 的实现阶段。

## Project Structure

### Documentation (this feature)

```text
specs/004-speculative-decoding/
├── spec.md               # 已有：特性规格（含 4 项已澄清 Clarifications）
├── plan.md               # 本文件（/speckit.plan 输出）
├── research.md           # Phase 0 输出（含 §3/§5.3 用户决策，§9 汇总决策结果，无遗留 NEEDS CLARIFICATION）
├── data-model.md         # Phase 1 输出（SpeculativeDecodingProfile 等新增实体，字段均已回填最终值）
├── quickstart.md         # Phase 1 输出
├── contracts/             # Phase 1 输出
│   ├── qml-cpp-session-interface-increment.md        # 新增：对 002 QML↔C++ 契约的唯一预授权增量
│   ├── llm-engine-speculative-internal-contract.md    # 新增：LlmEngine 投机解码内部实现契约（宿主架构已按方案 B 确定）
│   └── model-catalog-draft-pairing-internal-contract.md  # 新增：ModelCatalog 草稿模型识别与搭配映射内部契约（映射表已按方案①回填）
├── checklists/
│   └── requirements.md   # 已有：规格质量检查单
└── tasks.md               # Phase 2 输出（由 /speckit.tasks 生成，本命令不创建）
```

**与 001/002/003 既有契约的关系**：本特性对 `qml-cpp-session-interface.md` 做**唯一一处**增量（两个新增只读属性，`contracts/qml-cpp-session-interface-increment.md`），`switchModel()`/`availableModels`/`recommendedProfiles`/`currentModelName`/`modelSwitching`/`fallbackNoticeText`/`settingsVisible` 等既有属性/方法/信号签名与语义均保持 002/003 既定状态不变。本特性新增的另外两个 `contracts/*.md` 文件均标注为"内部实现契约"，供 `/speckit.tasks`/`/speckit.implement` 参考，不面向 QML 层开发者。

### Source Code (repository root)

本特性延续 001/002/003 已确立的单一桌面应用工程布局（`babel/` 可执行目标 + `content/` QML 资源 + `src/` 纯 C++ 业务逻辑目录），**不**引入新的顶层项目结构、**不新增任何新文件**（`research.md` §3 用户已决策采纳方案 B，候选方案 C"新增独立协调类"未被选中，确定不会新增协调类文件）。以下为本计划针对该特性确定的修改范围：

```text
babel/
└── src/
    ├── slowpath/
    │   ├── ModelCatalog.{h,cpp}      # [修改] 新增 DraftCandidate 枚举、classifyDraftCandidateByFileName()、
    │   │                               #        recommendedDraftProfiles()、pairedDraftCandidate()（research.md
    │   │                               #        §5.1/§5.2，contracts/model-catalog-draft-pairing-internal-
    │   │                               #        contract.md）；pairedDraftCandidate() 内部映射表已按用户决策
    │   │                               #        （方案①）确定为 Medium→Size0_5B / High→Size1_5B，
    │   │                               #        既有 TierHint/classifyTierByFileName()/recommendedProfiles()/
    │   │                               #        tierDebounceWindow() 不变
    │   └── LlmEngine.{h,cpp}          # [修改，改动范围已按 §3 用户决策（方案 B）确定] 内部新增投机解码算法实现
    │                                   #        （research.md §2 四步循环 + §6 GBNF 两侧应用）；generate() 对外
    │                                   #        签名不变（FR-002）；构造函数签名按方案 B 扩展以接受可选的草稿
    │                                   #        模型路径（contracts/llm-engine-speculative-internal-contract.md 四）
    └── session/
        └── SessionController.{h,cpp}  # [修改] 新增 speculativeDecodingNoticeText/recommendedDraftProfiles
                                        #        只读属性（contracts/qml-cpp-session-interface-increment.md）；
                                        #        switchModel()/applyModelSwitchResult()/bootstrapPlatformDefaults()
                                        #        新增"查表得到草稿模型路径并一并传给 LlmEngine 构造函数"调用点
                                        #        （方案 B：SessionController 私有状态与公开契约零改动）

tests/
├── slowpath/
│   ├── ModelCatalogTest.cpp       # [修改] 新增草稿候选分类/推荐数据/搭配映射相关用例（映射取值用例已按 §5.3 决策补全）
│   └── LlmEngineGenerateTest.cpp  # [修改，沿用既有测试文件，不新增同目录测试文件] 新增投机解码路径语义等价性/
│                                   #        降级透明性/语法约束两侧生效相关用例
└── session/
    └── SessionControllerTest.cpp  # [修改] 新增新属性断言与"切换档位自动重新匹配草稿模型"场景用例
```

**Structure Decision**：沿用 001/002/003 既有单一桌面应用结构（非 Option 1/2/3 模板中的任何一种标准模板场景，而是延续本仓库既有的 `babel/src/<子系统>/` 分层约定），本特性修改面严格限定于 `slowpath`（`ModelCatalog`/`LlmEngine`）与 `session`（`SessionController`）两个既有子系统目录内的既有文件；用户已决策采纳方案 B，确定不新增第三个子系统目录/新文件（§3 候选方案 C 未被选中）。

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

本特性 Constitution Check 无需要论证的宪章**违规**项——Gate 结论为 PASS，此前 2 项事项（§3/§5.3）是"待用户显式决策的产品/架构分歧点"而非"已违反宪章却需要论证豁免的情形"，现已由用户显式决策（方案 B、方案①）解决（二者性质不同：前者是原则 II 要求的正常流程产物，后者才需要本节填写豁免理由）。本节无需填写。
