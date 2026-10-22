# Specification Quality Checklist: 候选词列表手动纠偏

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-10-22
**Feature**: [spec.md](../spec.md)

**语言（宪章《语言与文档规范》）**：检查清单条目与说明 MUST 使用简体中文；路径与技术标识可保留英文。

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- 首轮撰写时发现 3 处 [NEEDS CLARIFICATION] 标记（FR-011 光标位置定位范围、FR-012 数字键劫持规则、FR-013 候选列表可见性规则），已以编号选项形式提交用户确认，并已于 2026-10-22 获得用户逐一答复（见 spec.md `## Clarifications` 一节）：
  - FR-011 → 采用方案 C：新增光标位置参数传递机制，候选列表始终针对光标当前所在词展示。
  - FR-012 → 采用方案 A：只要候选列表处于展示状态即劫持数字键 1–9，不叠加额外触发条件。
  - FR-013 → 采用方案 A：始终展示候选列表，即便只有唯一候选，编号固定为"1"。
- 分词边界标点符号清单（原第 4 个候选澄清点）已改为 Assumptions 中的合理默认处理，未计入 [NEEDS CLARIFICATION] 名额。
- 已据此更新 spec.md 对应 FR-011/FR-012/FR-013、Edge Cases 与 Key Entities 条款，三项标记均已解决，检查清单全部通过，具备进入 `/speckit.clarify`（如仍需补充其他澄清）或 `/speckit.plan` 的前置条件。
- **第二轮澄清（2026-10-22 第二轮）**：用户提出补充需求（前缀级候选预测、未显式选择时的默认行为、扩展标点/分隔符范围、候选列表仅在输入中展示、候选替换后拉丁字母合并风险、n-gram 后续迭代定位、docs/IME.md §2.2 定位复核）。经甄别，6 项由用户自行给出明确结论或经代码核实（`PinyinTrie`/`EnglishWordList`/`FastCorrector::buildLattice()`/`looksLikeLatin()`）确认为技术事实，直接落地为 FR-017/FR-018/FR-019/FR-020/FR-021 与 Out of Scope/定位澄清章节新增条目；另有 3 项真实开放决策点以单选形式逐一提交用户确认并已获得答复：
  - 前缀预测候选池溢出上限策略 → 方案 B：单独放宽，与默认自动应用候选池解耦（落地为 FR-022）。
  - 前缀预测最小触发长度 → 方案 C：统一 2 字符，拼音/英文不区分（落地为 FR-023，并据此修正 FR-017 示例）。
  - 扩展标点/分隔符清单 → 方案 B：采纳完整清单但排除连字符/下划线（落地为 FR-005 最终清单与例外规则）。
  - 新增 SC-006/SC-007 分别对应前缀预测延迟一致性与候选替换后拉丁字母识别正确性的可测量验收标准。
  - 本轮全部 3 项开放问题均已解决，无遗留 [NEEDS CLARIFICATION] 标记，检查清单复核后全部条目仍然通过，具备进入 `/speckit.plan` 的前置条件。
