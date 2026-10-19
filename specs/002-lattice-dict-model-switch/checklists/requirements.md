# Specification Quality Checklist: 拼音多候选格架构、真实词库与可切换本地模型

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-10-19
**Feature**: [spec.md](../spec.md)

**语言（宪章《语言与文档规范》）**：检查清单条目与说明 MUST 使用简体中文；路径与技术标识可保留英文。

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain —— 本特性原共识别出 8 处 `[NEEDS CLARIFICATION]`；`/speckit.clarify` 第一轮（2026-10-19）解决其中 5 处，第二轮（2026-10-19）解决剩余 3 处（FR-013、FR-014、SC-001），全部 8 处均已澄清并写回 spec.md，详见下方 Notes 与 spec.md `## Clarifications`
- [x] Requirements are testable and unambiguous（除标记 `[NEEDS CLARIFICATION]` 的未决项外，其余条款均可直接验证）
- [x] Success criteria are measurable（全部 SC 均已给出可验证的行为性标准或具体数值阈值，含 SC-001 已澄清锁定的 80% 阈值）
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded（见 Out of Scope）
- [x] Dependencies and assumptions identified（见 Assumptions，并已引用 001 既有基线）

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- Items marked incomplete require spec updates before `/speckit.clarify` or `/speckit.plan`
- 本特性为架构级变更（取代 001 FR-013 与 `NormalizedCandidateText` 单一候选设计），依宪章判定程序命中多项 (a)/(b)/(d)/(e)，故保留的 `[NEEDS CLARIFICATION]` 数量（8 处）超出常规指引的 3 条上限；这是宪章《Requirement Boundary & Decision Control》原则 II「未决策不得默认」对本仓库的强制要求，MUST NOT 通过猜测或"合理默认值"消解，需分轮（每轮至多 5 题）交由 `/speckit.clarify` 处理：
  1. ~~许可证开放问题（GPLv3 随包分发 iDvel/rime-ice 词库数据的 copyleft 风险，Edge Cases）~~ —— 已澄清（第一轮）：接受随包分发，履行 GPLv3 copyleft 义务，见 spec.md Edge Cases 与 `## Clarifications`
  2. ~~词库数据更新机制（FR-004）~~ —— 已澄清（第一轮）：纯约定目录模式，见 FR-004
  3. ~~Lattice 多候选信息传递给慢流 LLM 的具体形式（FR-007）~~ —— 已澄清（第一轮）：结构化标注文本，`LlmEngine::generate()` 保持字符串类型，见 FR-007
  4. ~~QML 展示层是否需要呈现候选及权重（FR-008）~~ —— 已澄清（第一轮）：不向用户呈现，仅输出至开发诊断日志，见 FR-008
  5. ~~中配/高配推荐模型的具体型号、参数规模与量化精度（FR-012）~~ —— 已澄清（第一轮）：中配 Qwen2.5-14B-Instruct Q4_K_M，高配 Qwen2.5-32B-Instruct Q4_K_M，见 FR-012
  6. ~~模型不可用时的提示文案与回退行为（FR-013）~~ —— 已澄清（第二轮）：自动回退到此前可用模型并提示，依次回退直至低配默认模型，见 FR-013
  7. ~~切换模型是否需要重启应用/重新加载推理引擎（FR-014）~~ —— 已澄清（第二轮）：应用不重启，推理引擎热切换（卸载/加载期间慢流暂不可用），见 FR-014
  8. ~~语义准确率提升的具体验收阈值（SC-001）~~ —— 已澄清（第二轮）：绝对阈值 80%（高于 001 SC-007 的 60%），见 SC-001

全部 8 处 `[NEEDS CLARIFICATION]` 已于 2026-10-19 分两轮（Q1–Q5、Q6–Q8）全部处理完毕，spec.md 当前不再含任何 `[NEEDS CLARIFICATION]` 标记，可进入 `/speckit.plan` 阶段。
