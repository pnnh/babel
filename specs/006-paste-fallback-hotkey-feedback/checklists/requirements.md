# Specification Quality Checklist: 粘贴降级与热键冲突反馈补完

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-10-20
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [ ] No [NEEDS CLARIFICATION] markers remain（见 Notes，3 项待澄清）
- [x] Requirements are testable and unambiguous（标记为 [NEEDS CLARIFICATION] 的三项除外，其余 FR 均可测试）
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [ ] All functional requirements have clear acceptance criteria（FR-001/FR-002/FR-005/FR-006 待 [NEEDS CLARIFICATION] 解决后方可完全验收）
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- 本特性按宪章《Requirement Boundary & Decision Control》判定程序（判定程序 (a)：是否改变外部可观察行为/错误语义/状态流转/UI——命中），识别出 3 项涉及产品层决策分歧的 `[NEEDS CLARIFICATION]` 标记，均未在本阶段自行拍板：
  1. **FR-001（Q1）**：目标应用窗口在浮窗唤起期间被切换/最小化/关闭后，确认时系统应采取的粘贴降级行为——是否应主动检测并统一降级为与 001 FR-012 一致的"已复制到剪贴板，请手动粘贴"提示，还是仍尝试执行模拟粘贴（接受可能误粘贴到无关窗口的风险）。此项直接继承自 001 `spec.md` Edge Cases 原文、`data-model.md` §4 记录的"刻意留白"决策点。
  2. **FR-005（Q2）**：全局热键注册失败时，用户可见提示应采用何种承载方式——当前应用没有任何常驻于系统托盘/菜单栏的 UI（浮窗仅在热键唤起后才出现，而此场景下热键恰好不可用），是否需要新增独立于浮窗之外的常驻提示载体（系统托盘图标/系统通知等），还是采用其他替代呈现方式。此项直接继承自 001 `contracts/global-hotkey-contract.md` 记录的"本契约不定义该行为"决策点。
  3. **FR-006（Q3）**：该提示的具体触发时机——应用启动注册失败后立即提示，还是用户实际尝试使用（如按热键无反应）后才被动提示。
  - 另有两项与上述决策相关、但经核实**不**属于产品决策分歧、因此未标记 `[NEEDS CLARIFICATION]` 的事项，已分别以其他方式记录在 spec.md 中：
    - **FR-002 涉及的"目标窗口有效性检测在各平台的原生 API 可行性"**：属技术可行性核实问题，已记录于 Edge Cases 与 Assumptions，留待 `/speckit.plan` 的 research 阶段逐平台核实，不在 `/speckit.specify` 阶段提问或猜测。
    - **FR-007 涉及的"提示具体文案表述"**：契约类前例（001 `contracts/global-hotkey-contract.md`）要求"MUST NOT 自行发明冲突提示文案"，故本规格同样采用"明确记录为留白、不预先写定"的方式处理，而非作为一个需要用户当场选择选项的 `[NEEDS CLARIFICATION]` 问题（选项本身即是被禁止发明的文案），避免与该约束自相矛盾。
- 上述 3 项是用户本轮在发起本特性时明确指出的产品决策分歧点，并明确要求"标记 [NEEDS CLARIFICATION]，不要自行拍板，不要凭空假设"，故已按原样保留在 spec.md 中，未在本次 `/speckit.specify` 执行过程内发起交互式澄清问答（与 `specs/002-lattice-dict-model-switch`～`specs/005-learning-tip-generation` 对同类情形的处理方式一致）。
- 其余条目均已通过本轮验证；在上述 3 项 `[NEEDS CLARIFICATION]` 被解决前，**MUST NOT** 进入 `/speckit.plan`（按宪章判定程序：进入 plan 前，影响实现方向的未决项必须已被回答、委派或放弃）。
- 下一步建议：运行 `/speckit.clarify` 逐项澄清上述 3 个问题（宪章《提问规则》允许每轮最多 5 题，3 题在预算内）。
