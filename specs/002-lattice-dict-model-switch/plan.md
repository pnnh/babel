# Implementation Plan: 拼音多候选格架构、真实词库与可切换本地模型

**Branch**: `002-lattice-dict-model-switch`（Git 当前实际所在分支为 `main`，延续 001 既有做法——本仓库不为单个特性单独建立 feature branch，特性产物以 `specs/002-lattice-dict-model-switch/` 目录隔离）

**Date**: 2026-10-19

**Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/002-lattice-dict-model-switch/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/plan-template.md` for the execution workflow.

## Summary

本特性是对 `specs/001-floating-ime-converter`（已交付 MVP）的实质性扩展，涵盖三个方向：① 将快流拼音纠错的词典来源从 001 内置的 14 条演示数据升级为基于 `iDvel/rime-ice`（GPLv3）的真实开源 Rime 词库，支持标准 `.dict.yaml` 格式解析、细胞词库自动合并、纯约定目录的用户自助更新；② 将 `FastCorrector` 从"单一候选字符串"架构改造为"Lattice 多候选格"架构，每个拼音 token 产出按权重排序的候选集合，以结构化标注文本形式（`LlmEngine::generate()` 签名不变，仍为字符串）传递给慢流 LLM 供上下文消歧，QML 展示层不呈现候选集合、仅输出开发诊断日志；③ 本地 LLM 模型从固定单一模型升级为"默认 + 可选"，浮窗新增设置入口遍历约定模型目录、支持低配/中配/高配三档 Qwen 家族推荐模型、热切换（应用不重启、推理引擎卸载/重新加载）、不可用时级联自动回退。验收标准 SC-001 要求语义正确样本比例不低于 80%（高于 001 SC-007 的 60%）。

spec.md 已完成的全部 8 项 `[NEEDS CLARIFICATION]`（见 `checklists/requirements.md`）均已澄清写回 `spec.md` `## Clarifications`，本计划 Technical Context 中的事实陈述均可追溯至这些已澄清条款。本计划在设计过程中**新发现** 7 项命中宪章判定程序 (a)/(d)/(e) 且**未**被 spec 或用户本轮指令显式委托的事项（YAML 解析库选型、词库格式错误文件处理策略、细胞词库权重合并策略、Lattice 候选规模上限、设置页面不可用模型呈现方式、超出硬件能力档位选择限制、模型选择跨重启持久化），按宪章原则 II/判定程序，本计划当时**未**代为决策，仅在 `research.md` 中列出候选方案与权衡并标记 `[NEEDS CLARIFICATION]`，汇总于 `research.md` §12。

**本轮更新——用户委派及 Agent 实际选择**：用户已在本轮对话中就上述 7 项明确表态"全部委托 Agent 按 research.md 中各自的 Recommendation 执行"。该表态构成宪章原则 III 所要求的用户显式委派，7 项均已由 Agent 依据 `research.md` 各小节记录的 Recommendation 完成最终选型（`research.md` §2/§4.1/§4.2/§5.4/§7.3/§7.4/§9 已更新为 Decision，汇总见 §12），`data-model.md`/`contracts/*.md`/`quickstart.md` 中原先因此留白/占位之处已同步回填。7 项最终选型：① YAML 解析——手写最小化解析器；② 词库格式错误文件——跳过+仅诊断日志；③ 细胞词库权重合并——文件名字典序 + `INSERT OR IGNORE`；④ Lattice 候选规模上限——固定 Top-K=8；⑤ 不可加载模型呈现——展示+"不可用"标记（GGUF 文件头魔数校验）；⑥ 超出硬件档位限制——不限制，复用 FR-013 回退链路；⑦ 模型选择持久化——不持久化。本计划 Constitution Check 已据此复查，详见下文。

## Technical Context

**Language/Version**: C++23（沿用 001/仓库既有 `CMakeLists.txt` 配置，本特性未变更语言标准）。

**Primary Dependencies**:
- Qt6（沿用 001 既有依赖，浮窗 UI/设置页面新视图均复用既有 QML 技术栈，不新增 UI 框架）。
- `llama.cpp`（沿用 001 `research.md` §1 已确认的推理引擎选型，经既有 vcpkg overlay port `vcpkg/ports/llama-cpp` 接入）；本特性扩展其**使用方式**——从"构造时固定加载单一模型路径"改为"支持运行时卸载/重新加载不同模型路径"（`research.md` §8 热切换设计），引擎本身选型不变。
- **Rime `.dict.yaml` 解析所需的 YAML 库——已决策（用户本轮委派，Agent 采纳 Recommendation）**：本特性新增"解析标准 Rime 词库 frontmatter"的需求（FR-002）；候选曾为"手写最小化解析器"/`yaml-cpp`（vcpkg 官方 port，MIT）/`ryml`（vcpkg 官方 port，MIT）。**最终选型：手写最小化解析器**（零新增依赖），详见 `research.md` §2。
- 既有依赖 `spdlog`（本特性新增用途：§诊断日志，`research.md` §6）、`boost-filesystem`（词库/模型目录遍历延续既有用途）、`sqlite3`（词典存储，延续 001 `research.md` §5，本特性扩展数据来源但不改变存储技术本身）沿用不变。
- 单元测试框架延续 001 已确定的 **GoogleTest**（vcpkg 官方 port `gtest`），不新增/更换测试框架。

**Storage**: 复用既有 `sqlite3`（001 `research.md` §5 已确认），`dictionary_entry` 表结构字段不变（见 `data-model.md` §5），仅扩展数据来源；本特性**不**引入新的持久化存储技术。模型选择的运行时状态（`data-model.md` §8 `ActiveModelRuntimeState`）设计为**纯进程内存状态，不持久化到磁盘**——"是否需要持久化以支持跨应用重启记忆用户选择"已由用户本轮委派、Agent 采纳 `research.md` §9 Recommendation 最终决策为**方案 1：不持久化**，每次应用启动固定使用低配默认模型，仅 spec 字面要求的"同一次应用运行期间热切换"范围生效，不隐含扩大为跨重启记忆。

**Testing**: 延续 001 已确定的 GoogleTest + `gtest_discover_tests`；本特性新增测试目标覆盖范围（`PinyinLattice`/`RimeDictLoader`/`ModelCatalog`/热切换并发安全等）留待 `/speckit.tasks` 排期，本阶段不改动构建文件（宪章原则 V）。

**Target Platform**: 延续 001 既有桌面三平台 Windows/macOS/Linux 支持范围，未扩大/缩小目标平台集合。高配档模型验证涉及的两类基准硬件（Apple M1 Pro 32GB 统一内存、Windows+RTX 4090 24GB 显存）为 spec Clarifications 已明确的**验证目标**，非新增的"受支持平台"概念——`research.md` §10 已声明其可行性验证方法，不断言实测结论。

**Project Type**: desktop-app（延续 001，单一常驻 Qt6 Quick 进程，本特性不引入跨进程 IPC 或系统级输入法框架接入，未扩大该边界）。

**Performance Goals**（引用 spec Success Criteria，非本阶段新定义指标）:
- 快流端到端延迟 <15ms 的既有目标（001 SC-002）：本特性要求快流产出 Lattice（多候选）而非单一候选，`research.md` §5.2 的候选合并逻辑复杂度高于 001 单候选逻辑，具体是否仍能满足 <15ms 留待 `/speckit.implement` 阶段实测验证，本计划不预先断言（延续 001 既定阈值，未新增/修改该数值本身，故不单独标记为新 `[NEEDS CLARIFICATION]`，但列为实现阶段的实测验证项）。
- 慢流首批内容 <300ms、整句 <800ms 的既有目标（001 SC-003）：本特性新增的结构化标注文本会增加 prompt 长度（相较 001 单候选输入），且新增三档可选模型中的中/高配档参数规模显著增大，`research.md` §10 已设计验证方法，不预先断言结论。
- **语义正确率 MUST 不低于 80%**（SC-001，本特性新引入的量化阈值，高于 001 SC-007 的 60%）。

**Constraints**:
- 延续 001 既有约束：快流/慢流全程本地离线运行（Assumptions）；本地 LLM 模型文件 MUST 由用户手动下载后放置于约定本地目录，应用不随包分发模型权重、不提供自动下载入口（001 `research.md` §7，FR-015 重申）。
- **本特性新增约束**：词库数据文件（`.dict.yaml`）MUST 随应用安装包分发（FR-001，与模型文件"不随包分发"形成对比，二者分发策略不同，均为各自 spec 条款已明确的既定事实，非本计划自行选择）；随包分发 MUST 履行 GPLv3 copyleft 义务（`research.md` §3 设计具体技术落地步骤，不构成法律意见）。
- `LlmEngine::generate()` 输入参数 MUST 继续为 `QString` 类型，不得改为结构化对象（FR-007 硬性约束，详见 `contracts/llm-prompt-annotation-contract.md`）。
- QML 展示层 MUST NOT 向用户呈现候选集合及权重（FR-008），诊断信息仅通过开发日志输出。
- 模型切换 MUST 为热切换（应用本体不重启），期间慢流可暂不可用（FR-014）；不可用时 MUST 级联自动回退至低配默认模型（FR-013）。

**Scale/Scope**: 延续 001 既有"单机单用户本地离线场景、浮窗同一时刻仅处理一个转换会话"范围（Assumptions），本特性未扩大并发/多用户范围。词库数据规模参考：核实 `iDvel/rime-ice` 的 `cn_dicts/` 目录原始文本合计约 44MB（`research.md` §1），供词库加载性能预算参考。模型体积范围：低配约 2GB 级别（延续 001）、中配约 9GB、高配约 19.85GB（FR-012 已锁定数值）。

**本阶段新发现、现已由用户委派 Agent 完成最终选型的事项清单**（均为本次规划新发现，非 spec 遗留——spec 既有的 8 项已全部于 `/speckit.clarify` 阶段澄清完毕，详见 `checklists/requirements.md`；以下 7 项原标记 `[NEEDS CLARIFICATION]`，本轮用户已整体委派"按 Recommendation 执行"，已全部转为 Decision）：

| # | 事项 | 判定程序命中项 | 最终选型（用户委派，Agent 采纳 Recommendation） | 对应小节 |
|---|------|----------------|----------|----------|
| 1 | Rime `.dict.yaml` 解析方案：手写最小解析器 / yaml-cpp / ryml | (d) 新增依赖 | 手写最小化解析器（零新增依赖） | `research.md` §2 |
| 2 | 词库目录下格式错误文件的处理策略 | (a) 外部行为 | 跳过该文件，仅写诊断日志，不新增用户可见提示 | `research.md` §4.1 |
| 3 | 细胞词库间同词条重复、权重冲突的合并策略 | (a)/(b) 外部行为/数据模型 | 复用 `INSERT OR IGNORE`，按文件名字典序，先加载者权重生效 | `research.md` §4.2 |
| 4 | Lattice 每格位候选集合规模上限 | (e) 性能量级 | 固定 Top-K 截断，K = 8（实测前起点，待 SC-003 实测调参） | `research.md` §5.4 |
| 5 | 设置页面对"无法加载模型文件"的呈现方式 | (a) 外部行为（UI） | 展示+"不可用"标记，标记依据为 GGUF 文件头魔数校验 | `research.md` §7.3 |
| 6 | 用户选择超出硬件能力档位模型时是否限制 | (a) 外部行为（UI） | 不限制，复用 FR-013 既有自动回退+提示链路 | `research.md` §7.4 |
| 7 | 模型选择是否需跨应用重启持久化 | (a) 外部行为 | 不持久化，每次启动固定使用低配默认模型 | `research.md` §9 |

以上 7 项**均已解决**（委派依据：用户本轮对话原话"全部委托 Agent 按 research.md 中各自的 Recommendation 执行"），本计划的 Project Structure/`data-model.md`/`contracts/` 对涉及以上事项的设计产物已按最终选型回填具体取值/行为，构成已拍板的设计决策；`research.md` §12"用户委派及 Agent 实际选择"记录完整委派依据与选型理由。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Language (宪章《语言与文档规范》)：本 plan 及同特性 research/data-model/quickstart/contracts 的说明性文字 MUST 使用简体中文；API/路径/库名等标识符可保留英文。本文件已满足该要求。*

**原则 I（显式需求是唯一授权范围）**：Technical Context 中每一条"已确认"表述均可追溯至 spec.md 的 FR/SC/Clarifications 条款，或用户本轮任务指令原话（settings 页面 UI 方案委派；本轮新增的"7 项全部委托 Agent 按 Recommendation 执行"整体委派）；7 项新发现事项均未被 Agent 单方面代为决策，而是在取得用户本轮显式委派后，依据 `research.md` 已记录的 Recommendation 完成最终选型，过程留痕可追溯，未出现 Agent 超出授权范围自行发明新需求的情况。**PASS**。

**原则 II（未决策不得默认，Assumption 不得充当决策）**：7 项新发现事项均命中判定程序 (a)/(d)/(e)，在取得用户本轮显式委派**之前**，本计划严格未自行选默认值，均在 `research.md` 标记 `[NEEDS CLARIFICATION]` 而非拍板；取得委派**之后**，最终选型均有明确来源（用户委派 + `research.md` 已记录 Recommendation），不构成"未决策却默认"，而是"决策权已被明确授予 Agent 后行使"。`data-model.md` 中涉及这些事项的字段（如 `RimeDictionarySourceFile.loadStatus`、`ModelDescriptor.availability`）已按最终选型回填具体取值/行为（详见下文"用户委派及 Agent 实际选择"）。已有同类实现取值的复用（如沿用 001 `ModelPaths::conventionalModelDirectory()`、沿用 001 既有 `INSERT OR IGNORE` sqlite3 语义）均已在 `research.md` 对应小节写明来源文件位置（`babel/src/slowpath/ModelPaths.cpp`、`babel/src/fastpath/DictionarySchema.cpp`）。**PASS**。

**原则 III（推荐不是批准）**：`research.md` 各小节给出的 Recommendation 原文均保留"非批准"标注，本轮最终选型的生效依据是**用户本轮显式委派**（"全部委托 Agent 按 Recommendation 执行"）而非 Agent 单方面将 Recommendation 视为既定选型——该委派行为本身满足宪章原则 III 对"须经显式决策方可转为既定方案"的要求，7 项均在 `research.md` 各小节"最终选型"处同时注明"用户委派"与"采纳的 Recommendation 内容"两重依据；spec FR-006/FR-007/FR-008/Edge Cases（许可证）中**已由 spec 原文显式委托**给 `/speckit.plan` 设计阶段确定的事项，以及用户本轮任务指令中显式委派的"设置页面 UI 实现方式"，同样均已注明委托来源依据。**PASS**。

**原则 IV（内部细节低摩擦，外部行为高控制）**：不改变外部可观察行为的内部细节（如 `PinyinTrie` 查询方法返回类型变更为携带权重、`SessionController::m_llmEngine` 由 `unique_ptr` 改为 `shared_ptr`、`ModelDescriptor.tierHint` 的文件名启发式分类规则、开发诊断日志的具体文本格式）均已由本计划在 `research.md`/`data-model.md` 中自主决定并记录依据，未就此类纯内部实现细节提问用户。**PASS**。

**原则 V（最小必要修改与实施边界）**：本阶段（`/speckit.plan`）仅创建/更新 `specs/002-lattice-dict-model-switch/{plan.md, research.md, data-model.md, contracts/*, quickstart.md}` 五类设计产物，未改动任何既有源码（`babel/**`）或构建文件（`CMakeLists.txt`、`vcpkg.json`、`CMakePresets.json`）；`data-model.md` 中 `NormalizedCandidateText` 的改动方案刻意选择"新增 `lattice` 字段、保留既有 `content`/`updatedAt` 字段"而非整体重写该结构体，以最小化对 001 既有 QML 契约（`rawInputSlotText`）的影响面。7 项最终选型本身亦一贯体现最小必要修改取向（YAML 解析选零依赖方案、词库合并复用既有 SQL 语义、超出硬件档位不新增检测能力、模型选择不新增持久化机制）；`vcpkg.json` 因最终选型为"手写最小化解析器"（零新增依赖），**不需要**补充 YAML 解析库依赖声明，该事项已闭环，无需留待后续阶段决策。**PASS**。

**原则 VI（预授权质量基线不得发明产品语义）**：本阶段未产出代码，暂不适用；`research.md` §4.1/§7.3 等涉及"错误/异常如何提示"的设计讨论原先仅列候选方案，本轮最终选型均经用户本轮显式委派后方才确定具体对外提示/呈现规则（如 §4.1 的"仅诊断日志、不新增用户可见提示"、§7.3 的"GGUF 文件头魔数校验+不可用标记"），不存在 Agent 借本条款预授权基线自行发明未经授权产品行为的情况——产品行为的决定权始终来自用户委派，而非 Agent 单方面援引"预授权质量基线"。**PASS**。

### 用户委派及 Agent 实际选择

用户在本轮对话中明确表态："针对 …… 7 项 `[NEEDS CLARIFICATION]`……全部委托 Agent 按 research.md 中各自的 Recommendation 执行。"该表态构成宪章原则 III 所要求的显式决策/委派，Agent 据此将 `research.md` 对应小节的 Recommendation 转化为最终 Decision（个别小节 Recommendation 含多个条件分支时，已在该小节"最终选型"处说明 Agent 在委派权限内选定其中一支的理由，不超出"按 Recommendation 执行"的委派范围）。最终选型汇总：

| # | 事项 | 最终选型 | 来源小节 |
|---|------|----------|----------|
| 1 | Rime `.dict.yaml` 解析方案 | 手写最小化解析器（零新增依赖） | `research.md` §2 |
| 2 | 词库目录下格式错误文件处理策略 | 跳过该文件，仅写诊断日志，不新增用户可见提示 | `research.md` §4.1 |
| 3 | 细胞词库同词条权重合并策略 | 复用 `INSERT OR IGNORE`，文件名字典序，先加载者权重生效 | `research.md` §4.2 |
| 4 | Lattice 每格位候选规模上限 | 固定 Top-K 截断，K = 8（实测前起点） | `research.md` §5.4 |
| 5 | 设置页面不可加载模型呈现方式 | 展示+"不可用"标记，依据 GGUF 文件头魔数校验 | `research.md` §7.3 |
| 6 | 超出硬件能力档位选择限制 | 不限制，复用 FR-013 自动回退+提示链路 | `research.md` §7.4 |
| 7 | 模型选择跨应用重启持久化 | 不持久化，每次启动固定使用低配默认模型 | `research.md` §9 |

`data-model.md`、`contracts/qml-cpp-session-interface.md`、`contracts/llm-prompt-annotation-contract.md`、`quickstart.md` 中原先因这 7 项未决而留白/占位/保守措辞之处（详见各文件相应小节）均已同步按上表最终选型回填为具体内容，不再存在"结构预留但未定义行为"的表述。

**判定程序门禁**：对本 plan 中每一项用户未显式指定的事项已逐条执行 (a)–(e) 判断；7 项命中 (a)/(d)/(e) 的事项，在 Phase 0/Phase 1 产出时点曾标记 `[NEEDS CLARIFICATION]`，现已因用户本轮显式委派而全部解决（见上表）；其余内部细节（`PinyinLattice`/`ModelCatalog`/`RimeDictLoader` 等新类的目录位置与命名、`shared_ptr` 并发安全实现方式、诊断日志具体文本、设置页面 Window 内部实现方式等）判定结果全部为"否"，已按原则 IV 自主决定并单行记录于 `research.md`/`data-model.md` 对应小节。

**Gate 结论**：**PASS，无遗留 `[NEEDS CLARIFICATION]`**——设计产物已生成，Phase 0/Phase 1 产出的全部内容对已授权事项均可追溯来源；7 项新发现事项已全部经用户本轮显式委派完成最终选型（见上表与 `research.md` §12），`/speckit.tasks` 可直接将全部任务（含原涉及这 7 项的任务）排期为可执行的实现任务，无需再等待进一步决策。

### Post-Design Re-check（Phase 1 完成后，含本轮委派回填后的复查）

`data-model.md` 与 `contracts/` 中定义的实体字段、QML/C++ 接口方法均逐条对照 spec 的 FR-001～FR-015 与 Key Entities 核对来源：

- `contracts/qml-cpp-session-interface.md` 新增的 `switchModel()`/`availableModels`/`modelSwitching` 等均可追溯至 FR-009～FR-014；`rawInputSlotText` 语义澄清（取值来源变更但外部可观察效果不变）符合 FR-008 的"不向用户呈现候选集合"约束；§六"本契约未决定的事项"三条原占位条目均已回填为具体规则（`availability` 迁移规则、不新增硬件检测、不新增持久化），不再存在未决占位。
- `contracts/llm-prompt-annotation-contract.md` 定义的标注格式严格保持 `generate()` 签名为 `QString`，符合 FR-007 硬性约束；"候选规模上限"占位已回填为"K=8 截断在 `PinyinLattice` 构建阶段完成，不改变标注语法本身"的具体规则。
- `data-model.md` 中原 7 处标注"本阶段不预设具体行为"的字段（`RimeDictionarySourceFile.loadStatus`、`DictionaryEntry` 合并策略说明、`PinyinLatticeCell`/`CandidateEntry` 候选规模上限说明、`ModelDescriptor.availability`、`ActiveModelRuntimeState` 持久化范围声明）均已核对并回填为与上表一致的具体取值/行为，不再使用"留待决策"措辞。
- `quickstart.md` 中原以"[按已决策行为验证]"占位的验证步骤（场景一前置条件、场景四步骤 4）均已替换为按最终选型的具体验证步骤与预期结果。

**本次复查结论**：**PASS，无新增违规，无遗留 `[NEEDS CLARIFICATION]`**；7 项事项均已经用户显式委派完成最终选型并回填至全部设计产物，设计产物已整体自洽、可直接供 `/speckit.tasks` 排期。

## Project Structure

### Documentation (this feature)

```text
specs/002-lattice-dict-model-switch/
├── spec.md               # 已有：特性规格（含 8 项已澄清 Clarifications）
├── plan.md               # 本文件（/speckit.plan 输出）
├── research.md           # Phase 0 输出
├── data-model.md         # Phase 1 输出
├── quickstart.md         # Phase 1 输出
├── contracts/             # Phase 1 输出
│   ├── qml-cpp-session-interface.md       # 对 001 同名契约的增量变更
│   └── llm-prompt-annotation-contract.md  # 新增：LlmEngine::generate() 标注文本格式契约
├── checklists/
│   └── requirements.md   # 已有：规格质量检查单
└── tasks.md               # Phase 2 输出（由 /speckit.tasks 生成，本命令不创建）
```

### Source Code (repository root)

本特性延续 001 已确立的单一桌面应用工程布局（`babel/` 可执行目标 + `content/` QML 资源 + `src/` 纯 C++ 业务逻辑目录），**不**引入新的顶层项目结构。以下标注 **[新增]**/**[修改]** 的文件/目录为本计划针对该特性建议的扩展，供 `/speckit.tasks` 排期与 `/speckit.implement` 落地参考；本阶段仅做结构规划，不创建/修改这些源文件。

```text
babel/
├── content/
│   ├── FloatingWindow.qml        # [修改] 新增右上角设置按钮 + 视图切换容器（research.md §11）
│   └── SettingsPanel.qml         # [新增] 设置页面：模型列表/三档推荐信息/切换操作
└── src/
    ├── fastpath/                   # 快流：对应方向二（真实词库）、方向一（Lattice）
    │   ├── FastCorrector.{h,cpp}        # [修改] correctToken()/correct() 改为基于候选合并构建 PinyinLattice；
    │   │                                 #        新增 buildLattice() 对外方法，correct() 改为基于其派生（保持向后兼容）
    │   ├── PinyinTrie.{h,cpp}           # [修改] queryByFullPinyin()/queryByAbbrev() 返回类型由
    │   │                                 #        std::vector<std::string> 改为携带权重（data-model.md §1 既有信号，research.md §5.1）
    │   ├── PinyinLattice.{h,cpp}        # [新增] CandidateEntry/PinyinLatticeCell/PinyinLattice 结构与渲染方法（data-model.md §1-3）
    │   ├── RimeDictLoader.{h,cpp}       # [新增] 解析约定目录下全部 .dict.yaml（frontmatter + TSV 数据行），自动合并细胞词库
    │   ├── DictionaryPaths.{h,cpp}      # [新增] 约定词库目录路径（镜像 ModelPaths 既有模式，research.md §3/§7.1）
    │   ├── DictionarySchema.{h,cpp}     # [修改] builtinSeedDictionary() 降级为兜底来源，主数据来源改为 RimeDictLoader 产出
    │   └── KeyboardProximityMap.{h,cpp} # 不变（延续 001）
    ├── slowpath/                   # 慢流：对应方向一（标注文本）、方向三（可切换模型）
    │   ├── LlmEngine.{h,cpp}            # [修改] 系统提示词补充标注语法说明（research.md §5.3）；
    │   │                                 #        新增卸载/重新加载支持（已有 tryLoadModel()/releaseModel() 可复用，
    │   │                                 #        需确认其可在同一实例生命周期内被安全重复调用，或改为"每次切换新建实例"——
    │   │                                 #        具体选择属内部实现细节，/speckit.tasks 阶段按 research.md §8 落地）
    │   ├── ModelPaths.{h,cpp}           # 不变（继续提供约定目录/低配默认模型路径，research.md §7.1）
    │   ├── ModelCatalog.{h,cpp}         # [新增] 扫描约定模型目录产出 ModelDescriptor 列表 + 档位启发式分类（data-model.md §6）
    │   └── DebounceScheduler.{h,cpp}    # 不变（既有 llama_context 非线程安全教训已记录于既有代码注释，research.md §8 衔接引用）
    ├── session/
    │   ├── DraftTypes.h                 # [修改] NormalizedCandidateText 新增 lattice 字段（data-model.md §1，最小必要修改）
    │   └── SessionController.{h,cpp}    # [修改] m_llmEngine 改为 shared_ptr；新增 switchModel()/openSettings()/closeSettings()
    │                                     #        等方法与 modelSwitching/currentModelName/availableModels/recommendedProfiles/
    │                                     #        fallbackNoticeText/settingsVisible 等属性（contracts/qml-cpp-session-interface.md）
    └── CMakeLists.txt                   # [修改] 登记上述新增源文件；最终选型为手写最小化解析器（research.md §2，零新增依赖），不需要新增第三方 YAML 库链接

<词库数据随包分发目录>/                # [新增，具体路径由 /speckit.tasks 按 DictionaryPaths 约定确定]
├── *.dict.yaml                         # iDvel/rime-ice 词库文件（GPLv3）
├── LICENSE-rime-ice.txt                # [新增] GPLv3 许可证全文（research.md §3）
└── NOTICE-rime-ice.txt                 # [新增] 来源/版权/上游版本声明（research.md §3）

vcpkg.json                              # 不变：research.md §2 最终选型为手写最小化解析器（零新增依赖），不需要补充 YAML 解析库声明

tests/
├── fastpath/
│   ├── PinyinLatticeTest.cpp            # [新增]
│   └── RimeDictLoaderTest.cpp           # [新增]
└── slowpath/
    └── ModelCatalogTest.cpp             # [新增]
```

**Structure Decision**：沿用 001 已确立的单工程布局与"快流/慢流/session/hotkey/inject/focus"分区方式，不新增顶层目录；本特性新增的 `fastpath/PinyinLattice.*`、`fastpath/RimeDictLoader.*`、`fastpath/DictionaryPaths.*`、`slowpath/ModelCatalog.*` 均落入既有 `fastpath`/`slowpath` 分区，`content/SettingsPanel.qml` 落入既有 `content/` QML 资源目录，保持与 001 结构一致性，不采用独立子项目/多项目结构选项。

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

无宪章违规需要豁免，本表留空。
