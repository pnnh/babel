# Data Model: AI 辅助双语浮窗转换器（MVP）

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md) ｜ **Research**: [research.md](./research.md)

## 阅读说明

本文件分两部分：
1. **用户可见实体**：直接对应 `spec.md` §Key Entities 的五个实体，字段与状态均可追溯到具体 FR/SC；
2. **内部支撑实体**：为落地"快流/慢流/热键+浮窗 UI/剪贴板与注入"四个设计分区而新增的设计阶段实体，均标注来源 FR，且按宪章原则 IV 作为内部细节自主记录（不改变外部可观察行为）。

原 `research.md` 中 5 项 `[NEEDS CLARIFICATION]` 实现选型（快流编辑距离/拼音 Trie、全局热键绑定库、模拟粘贴注入库、C++ 单元测试框架、本地 LLM 模型文件供给方式）已由用户显式委派、Agent 采纳对应 Recommendation 全部定稿（见 `research.md` "用户委派" 相关小节），本文件以下涉及该 5 项的字段说明已据此回填为具体实现路径（均为自研 C++ 实现，无第三方算法库绑定）。

---

## 一、用户可见实体（来自 spec Key Entities）

### 1. RawDraftText（原始输入草稿）

用户在浮窗中实时击键产生的中英拼音混杂文本，是快流处理的输入来源（FR-003）。

| 字段 | 类型 | 说明 |
|------|------|------|
| content | string (UTF-8) | 当前完整草稿文本 |
| cursorPosition | int | 当前光标位置，供快流增量纠错使用 |
| lastKeystrokeAt | timestamp | 最近一次击键时间，供慢流防抖判定使用（FR-004） |

**生命周期**：随浮窗唤起（FR-001）创建为空文本；浮窗隐藏/关闭（取消或确认后，FR-006）时丢弃，不跨会话保留（spec Assumptions："不要求支持同时处理多个独立的转换会话"）。

### 2. NormalizedCandidateText（规整化候选文本）

原始输入草稿经快流本地词典/拼音纠错后的展示结果，呈现在"原始输入槽"区域，**不构成最终上屏内容**（FR-002/FR-003）。

| 字段 | 类型 | 说明 |
|------|------|------|
| content | string | 规整化后的展示文本 |
| updatedAt | timestamp | 用于校验 SC-002（更新延迟 <15ms）的内部埋点 |

**更新触发**：原始输入草稿每次变化即同步重新计算（FR-003）。

### 3. StandardOutputPreview（标准中文输出预览）

慢流本地大语言模型基于规整化候选文本生成的标准中文译写结果，是用户确认后成为最终上屏内容的来源（FR-004）。

| 字段 | 类型 | 说明 |
|------|------|------|
| content | string | 当前展示的标准中文译写结果 |
| generationState | enum: `Idle` \| `Debouncing` \| `Generating` \| `Ready` \| `Stale` | 见下文"生成状态迁移" |
| requestedAt | timestamp \| null | 本轮慢流请求发起时间 |
| readyAt | timestamp \| null | 本轮慢流结果就绪时间，用于校验 SC-003 |

**生成状态迁移**（对应 FR-004/FR-005 与 User Story 3 的"等待状态提示"验收场景）：

```
Idle --(原始输入变化)--> Debouncing --(防抖窗口到期)--> Generating --(推理完成)--> Ready
Generating --(原始输入再次变化)--> Stale --(新一轮防抖完成)--> Generating
Ready --(原始输入再次变化)--> Stale --(新一轮防抖完成)--> Generating
```

- `Debouncing`/`Generating` 状态下，浮窗 MUST 展示"可感知的等待状态提示"（User Story 3 验收场景 2），具体文案/动画形式留待实现阶段按 FR-002 的"纯文本展示，不要求动画效果"约束设计，不在本文件定义 UI 细节。
- 模型文件缺失/损坏/无法加载（FR-011）时，`generationState` 不进入上述正常迁移路径，而是置于独立的 `Unavailable` 终止态（见下文"内部支撑实体 §FloatingSessionState"），并由浮窗提示用户。

### 4. TargetFocusContext（目标应用焦点上下文）

浮窗被唤起瞬间记录的、此前获得系统焦点的应用窗口信息，用于确认后定位自动粘贴的目标位置（FR-001 记录、FR-008 使用）。

| 字段 | 类型 | 说明 |
|------|------|------|
| platformWindowRef | 平台相关标识符（Windows `HWND` / macOS 前台应用+窗口引用 / X11 `Window` ID） | 热键与注入模块均已确定采用"分平台自研薄封装"（`research.md` §3/§4.2），故此处类型即为各平台原生窗口引用类型本身，不存在第三方库封装的中间类型 |
| capturedAt | timestamp | 捕获时间（热键唤起瞬间） |

**未定义行为（刻意留白）**：spec Edge Cases 提出"目标应用窗口在浮窗唤起期间被用户手动切换、最小化或关闭，确认时原目标应用已不可用，粘贴动作应如何表现？"——该问题**未**被转化为正式 FR，属于会改变外部可观察行为的产品决策（判定程序 (a) 命中）。本文件仅如实记录 `TargetFocusContext` 需要一个"捕获时刻的窗口引用"字段，**不**在此定义该引用失效时系统应如何降级，避免违反宪章原则 II；该行为规则需待此 Edge Case 被转化为 FR 后再设计。

### 5. LearningTip（学习提示，可选）

围绕本次输入/输出生成的简短英文表达建议文本，MVP 内仅作纯文本展示，不涉及持久化或个性化记忆（FR-002）。

| 字段 | 类型 | 说明 |
|------|------|------|
| content | string \| null | 提示文本；为空时浮窗不展示该区域 |
| visible | bool | 是否展示（低优先级、可选） |

**来源**：复用本次慢流生成过程中产生的辅助信息（spec Assumptions），不要求独立生成流程，不持久化（Out of Scope：个性化学习记忆）。

---

## 二、内部支撑实体（设计阶段新增，供实现参考）

### 6. FloatingSessionState（浮窗会话状态机）

对应 User Story 1/2/3 的整体交互流程，是 `RawDraftText`/`StandardOutputPreview` 等实体的宿主上下文。

| 状态 | 进入条件 | 说明 |
|------|----------|------|
| `Hidden` | 初始状态 / 确认或取消后 | 浮窗不可见，不持有任何会话数据 |
| `Invoked` | 用户按下全局热键（FR-001） | 创建 `TargetFocusContext`，浮窗显示并可输入 |
| `Editing` | 浮窗可输入状态下用户持续击键 | `RawDraftText`/`NormalizedCandidateText` 持续更新；可与 `StandardOutputPreview.generationState` 并行迁移 |
| `Confirmed` | 用户按回车（FR-006） | 触发剪贴板写入（FR-007）与粘贴注入尝试（FR-008/FR-009） |
| `Cancelled` | 用户按 Esc 或再次触发热键（FR-006） | 不产生任何剪贴板写入或粘贴动作 |
| `ModelUnavailable` | 本地 LLM 模型文件缺失/损坏/无法加载（FR-011） | 快流（原始输入槽）仍可用；慢流不可用，浮窗提示用户且不崩溃、不无响应 |

`Confirmed` 与 `Cancelled` 之后均回到 `Hidden`。

**模型文件来源（用户委派，Agent 采纳，见 `research.md` §7）**：`ModelUnavailable` 所检测的本地 LLM 模型文件（Qwen2.5-3B-Instruct GGUF Q4_K_M）由用户手动下载后放置于应用约定的本地目录；应用不随安装包分发该文件，亦不提供应用内自动下载入口。应用启动/首次调用慢流时检测该约定目录下文件是否存在、可加载，缺失/损坏/无法加载均统一进入 `ModelUnavailable` 并提示用户（不区分具体原因，呼应 FR-011 与 FR-012 的统一提示风格）；约定目录的具体路径常量属实现细节，由 `/speckit.tasks`/`/speckit.implement` 阶段按宪章原则 IV 自主确定。

### 7. DictionaryEntry / PinyinTrieNode（快流词典条目）

对应已选型的仓库内自研实现（编辑距离前缀索引 + 拼音/简拼 Trie，`research.md` §2）的数据层字段需求，持久化于 sqlite3（见 `research.md` §5）。

| 字段 | 类型 | 说明 |
|------|------|------|
| entryId | 整数主键 | — |
| surfaceForm | string | 规范化后的目标词形（中文词或英文词） |
| pinyinFull | string \| null | 全拼，供拼音匹配 |
| pinyinAbbrev | string \| null | 简拼（首字母串），供简拼匹配 |
| frequencyWeight | number | 词频权重，参与编辑距离候选排序 |

### 8. KeyboardProximityMap（QWERTY 物理键位邻近表）

静态内置数据（非用户数据），不要求持久化到 sqlite3，可作为编译期常量或随资源打包。

| 字段 | 类型 | 说明 |
|------|------|------|
| key | char | 物理键位字符 |
| neighbors | list<(char, weight)> | 物理邻近键位及其编辑距离权重，供 FR-003 的"键位邻近度加权"使用 |

### 9. HotkeyBinding（全局热键绑定）

| 字段 | 类型 | 说明 |
|------|------|------|
| modifiers | list<enum> | Windows/Linux: `[Ctrl, Alt]`；macOS: `[Control, Option]`（FR-001 默认值） |
| key | enum | `Space`（FR-001 默认值） |

**范围限制**：spec 未要求提供"自定义热键"的用户配置入口，MVP 不新增该能力（避免超出显式需求，属于判定程序 (a)/(b) 命中的产品决策，而非内部细节）。

### 10. InjectionAttemptResult（粘贴注入尝试结果，内部诊断用）

| 字段 | 类型 | 说明 |
|------|------|------|
| outcome | enum: `Succeeded` \| `UnsupportedEnvironment` \| `PermissionDenied` \| `OtherFailure` | 内部区分，便于日志与未来诊断 |
| attemptedAt | timestamp | — |

**强约束（直接对应 FR-012，不得在实现阶段放宽）**：除 `Succeeded` 外的任一 `outcome`，对外 UI 层 **MUST** 统一渲染为同一条"已复制到剪贴板，请手动粘贴"提示文案，**MUST NOT** 依据 `outcome` 的具体取值向用户展示差异化文案或原因说明。`outcome` 枚举仅供内部日志/未来诊断使用，不得作为用户可见文案的数据源。
