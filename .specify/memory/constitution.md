<!--
Sync Impact Report
- Version change: 1.0.0 → 2.0.0（MAJOR：核心原则被整体重定义，旧编号语义作废）
- 用户决策：本轮由用户以编号选项选择"替换核心 + 保留语言条款"，非 Agent 自行默认
- Modified principles:
  - I. 简体中文优先（文档与沟通） → 降级为章节《语言与文档规范》，MUST 规则保留不变
  - II. 源码为真相 → 移除（被新 I/II 的"授权范围 + Assumption 须客观成立"覆盖）
  - III. 简洁优先 → 移除（被新 I/V 覆盖）
  - IV. 精准手术式修改 → 由新 V. 最小必要修改与实施边界 取代并收紧
  - V. 目标驱动与可验证交付 → 移除（被新 VI. 预授权质量基线 部分覆盖）
  - 新增 I. 显式需求是唯一授权范围（NON-NEGOTIABLE）
  - 新增 II. 未决策不得默认，Assumption 不得充当决策
  - 新增 III. 推荐不是批准
  - 新增 IV. 内部细节低摩擦，外部行为高控制
  - 新增 V. 最小必要修改与实施边界
  - 新增 VI. 预授权质量基线不得发明产品语义
- Added sections: 判定程序（(a)–(e) 统一入口）；提问规则与阶段预算；阶段报告（每命令强制输出）
- Removed sections: 开发工作流与质量门禁（其"上下文/门禁/验证"条目并入判定程序与 Governance）
- Templates:
  - .specify/templates/spec-template.md → ✅ 语言引用由"宪章原则 I"改为"《语言与文档规范》"
  - .specify/templates/tasks-template.md → ✅ 同上
  - .specify/templates/checklist-template.md → ✅ 同上
  - .specify/templates/plan-template.md → ✅ 语言引用与 Constitution Check 门禁改为原则 I–VI + 判定程序
  - .specify/templates/constitution-template.md → ⚠ 保持上游模板形态（runtime 以 memory/constitution.md 为准）
- Follow-up TODOs:
  - specs/001–007 既有产物按旧宪章 v1.0.0 生成，不追溯改写；仅当被本轮之后的会话修订时适用本版
-->

# Requirement Boundary & Decision Control

**适用项目**：希波万象（Proxima）全仓库及其全部 Spec Kit 流程与 AI 代理协作。

## Core Principles

### I. 显式需求是唯一授权范围（NON-NEGOTIABLE）

只有以下事项构成授权范围：用户明确写入 `spec.md` / clarification / `plan.md` 的内容、本轮用编号选项确认的选择、或用户显式委派的事项。

- 实现已授权事项所**必需的内部步骤** MAY 自动执行，无需逐条确认。
- 用户未写明的功能、字段、权限、错误语义、兼容策略、性能目标、UX 规则，**MUST NOT** 被视为需求。
- 发现 bug、技术债、优化机会、可顺便重构之处，**MUST NOT** 因此获得修改授权；**MUST** 列入 Out of Scope 或 Follow-up。

**理由**：防止 Agent 把补全、优化、统一风格当成任务本身。

### II. 未决策不得默认，Assumption 不得充当决策

- Assumptions 章节 **MUST** 仅记录客观成立、或已被现有代码证明的前提；**MUST NOT** 用 Assumption 承载一项未经用户决策的选择。
- 凡两个以上方案会改变外部可观察行为、公共契约、数据模型、权限、安全/隐私、兼容性或性能量级的事项，**MUST** 标记 `[NEEDS CLARIFICATION]`；**MUST NOT** 自行选默认值，**MUST NOT** 以"最佳实践"消解。
- 已存在的同类实现取值 MAY 沿用，但 **MUST** 在 `plan.md` 中写明来源文件位置；偏离既有模式 **MUST** 提问。

**理由**：Spec Kit 默认的 specify/plan 会用"合理默认值"和 research 填空，本原则明确覆盖并禁止该行为。

### III. 推荐不是批准

- Agent MAY 给出 2–4 个编号方案并标注 Recommendation，但推荐 **MUST NOT** 被当作需求或既定决策。
- 有效批准**仅限**：用户选择编号、用户写出决策、或用户表述"你决定 / 委派给你"。
- "继续""按 plan 实施""LGTM" **仅**批准已写入产物中的内容，**MUST NOT** 外推为对未写入建议的批准。
- 委派 **MUST** 在阶段报告中记为"用户委派"，并记录 Agent 的实际选择。

**理由**：防止把建议写进 spec 后当成已定案。

### IV. 内部细节低摩擦，外部行为高控制

- 纯内部实现（不改变可观察行为、契约、数据模型、权限、安全/隐私、性能量级、部署运维）**MUST** 由 Agent 自主决定，在 `plan.md` 中单行记录，**MUST NOT** 就此提问。
- 本宪法 **MUST NOT** 被当作对内部命名、函数拆分、局部变量、私有结构的审批流程。

**理由**：避免把开发变成问答。

### V. 最小必要修改与实施边界

- 变更 **MUST** 仅覆盖完成当前需求所必需的范围。
- `/speckit-implement` MAY 新增该任务模块下必需的源文件与测试文件，但 **MUST** 在 Boundary Report 中列出其必要性。
- **禁止**：未在 `plan.md` 中确认的依赖变更；为让测试通过而修改断言、删除测试或放宽校验；修改与本需求无必要关系的既有模块；静默改变既有外部行为。
- 遇阻塞 **MUST** 停止并报告，**MUST NOT** 换方案绕过。

**理由**：把发挥空间留在已授权模块内部，而不是整库。

### VI. 预授权质量基线不得发明产品语义

实现任何已授权需求时，以下事项视为已授权，无需额外确认：

1. 为**新增**的公共入口做输入校验；
2. 为**新增**逻辑补对应测试；
3. 异常 **MUST NOT** 未处理向上裸奔。

上述基线 **MUST NOT** 改变既有错误语义、既有测试断言、既有模块结构；**MUST NOT** 用于自行决定对外错误码、错误文案、重试/幂等/数据保留等可观察策略。

**理由**：保证基本工程质量，同时堵住"借异常处理发明产品行为"。

## 判定程序

本程序是所有条款的统一入口；条款之间产生冲突时，**以本程序的判定结果为准**。

对任一用户未显式指定的事项，依次判断：

- **(a)** 是否改变外部可观察行为（接口、输出、错误语义、状态流转、UI）？
- **(b)** 是否改变公共契约、数据模型、库表或权限模型？
- **(c)** 是否涉及安全、隐私、认证授权、数据保留/删除、对外共享？
- **(d)** 是否新增/变更依赖、部署或运维方式？
- **(e)** 是否带来显著成本或性能量级变化？

**任一为是** → 标记 `[NEEDS CLARIFICATION]`，本阶段 **MUST NOT** 自行决定。
**全部为否** → 属内部细节，**MUST** 自主决定并单行记录（原则 IV）。

### 提问规则

- 需求/行为类问题 **MUST** 只在 `/speckit-specify` 与 `/speckit-clarify` 阶段提出。
- 每轮最多 **5** 题；每题 **MUST** 带 2–4 个编号选项和 1 个 Recommendation。
- `/speckit-specify` **MUST NOT** 用猜测消除产品未决项；超出本轮问题预算的标记 **MUST** 保留在产物中，留待下一轮 `/speckit-clarify`。
- `/speckit-plan` **MUST NOT** 通过 research 或最佳实践消除仍会影响 (a)–(e) 的未决项。
- 进入 `/speckit-plan` 前，影响实现方向的未决项 **MUST** 已被回答、已被委派，或已被用户明确放弃。

## 阶段报告

每个 speckit 命令结束时 **MUST** 输出以下五项，**MUST NOT** 用一句话概括代替：

1. **本阶段新增 Assumption**：逐条写明其客观依据（引用源文件或用户原话）。
2. **本阶段设置的默认值**：没有则写"无"。
3. **超出显式需求的事项**：**MUST** 落入 Out of Scope 或 `[NEEDS CLARIFICATION]`。
4. **实际触碰的文件及必要性**：逐个文件说明为何必须改动。
5. **用户委派及 Agent 实际选择**：无委派则写"无"。

## 语言与文档规范

（本章节承接宪章 v1.0.0 原则 I，规则不变，仅由"原则"降级为"章节"。）

### 适用范围

- Spec Kit 目录：`specs/**`、`.specify/bugs/**`、`.specify/memory/**` 中由代理生成的叙述性内容。
- 用户可见的代理对话输出。

### 语言规则（可测试）

| 内容类型 | 语言要求 |
|----------|----------|
| 用户故事、验收标准、需求条目、任务描述、检查清单条目、报告叙述、阶段报告 | **MUST** 简体中文 |
| 标识符、路径、命令、第三方名称 | **MAY** 英文 |
| 代码本身 | 遵循各平台既有风格；注释 **SHOULD** 优先中文或中英双语 |
| 历史英文产物 | **MAY** 保留；新修订 **SHOULD** 改为简体中文 |

### 违规判定

若新建或本次会话修改的 `spec.md` / `plan.md` / `tasks.md` / 检查清单 / 缺陷报告主体说明为英文且无中文叙述，视为违规，**MUST** 在合并前改正。

## Governance

1. 本宪法**优先于** Agent 的习惯性补全，也**优先于** Spec Kit 命令模板中"使用合理默认值 / 用 research 消掉澄清项"的默认做法。当命令模板指示与本宪法冲突时，**MUST** 以本宪法为准。
2. 与 `AGENTS.md` / `CLAUDE.md` 冲突时，以更严格且可测试的条款为准，并 **SHOULD** 同步修订二者。
3. **修订程序**：修正本宪法 **MUST** 由用户明确发起；修订时 **MUST** 更新本文件、递增版本号、填写 **Last Amended** 日期，并在文首 Sync Impact Report 记录影响面。
4. **版本策略**（语义化）：
   - **MAJOR**：收紧或废除既有原则、删除或重新定义原则导致既有流程不兼容；
   - **MINOR**：新增原则或实质性扩展约束；
   - **PATCH**：措辞澄清、笔误、无语义变化的整理。
5. **合规审查**：`/speckit-plan` 的 Constitution Check **MUST** 逐条核对原则 I–VI 并执行判定程序 (a)–(e)；PR 与代理交付物审查 **SHOULD** 抽查阶段报告完整性、产物语言与 diff 的最小性。
6. 运行时开发指引见根目录 `AGENTS.md` / `CLAUDE.md`；特性执行以 `specs/<feature>/` 为准。

**Version**: 2.0.0 | **Ratified**: 2026-07-26 | **Last Amended**: 2026-09-04
