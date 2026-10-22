# Contract: QML ↔ C++ 浮窗会话接口（007 变更增量）

**Spec**: [spec.md](../spec.md)（FR-001/002/006/010/011/012/013/014）｜ **Data Model**: [data-model.md](../data-model.md) 七 ｜ **Research**: [research.md](../research.md) §3/§6 ｜ **基线**: [002 契约](../../002-lattice-dict-model-switch/contracts/qml-cpp-session-interface.md)（004/005/006 已各增量一次，分别见其对应 `contracts/qml-cpp-session-interface-increment.md`）

## 适用范围与阅读方式

本文件记录本特性对 002 既有契约的**第四处增量**。002/004/005/006 原文件中未被下表提及的全部属性/方法/信号均**原样有效，未变更**。

## 一、方法签名变更

| 方法 | 变更前 | 变更后 | 对应 FR |
|---|---|---|---|
| `submitKeystrokes` | `Q_INVOKABLE void submitKeystrokes(const QString &text)` | `Q_INVOKABLE void submitKeystrokes(const QString &text, int cursorPosition = -1)` | FR-011 |

**向后兼容承诺（已与用户确认，`research.md` §3）**：`cursorPosition` 省略或传入 `-1` 时，**MUST** 退化为"视作光标在 `text` 末尾"（即 `text.length()`），与变更前行为完全等价。既有以单参数形式调用本方法的全部代码（`FloatingWindow.qml` 变更前调用点、`tests/session/SessionControllerTest.cpp` 既有 2 处用例）**MUST** 无需修改即可继续编译通过、行为不变。

## 二、新增只读属性

| 属性 | 类型 | 对应 spec 条款 | 说明 |
|---|---|---|---|
| `currentCandidates` | `QVariantList`（元素 `QVariantMap{number:int, text:QString}`） | FR-001/002/003/009/010/013 | 当前光标锚定词的候选集合，按展示顺序编号（1 起，不跳号），由 `candidatesChanged`-等价的既有 `rawInputSlotTextChanged`-同批次信号驱动的新信号 `currentCandidatesChanged` 通知；无当前词时为空数组 |

**MUST NOT** 携带 `normalizedWeight`/`rawWeight`/`source` 等权重或来源字段（FR-001 背景条款"候选的具体权重数值本身仍 MUST NOT 直接展示给用户"的延续约束，002 FR-008 对应部分继续有效）。

## 三、新增可调用方法

| 方法 | 对应 spec 条款 | 说明 |
|---|---|---|
| `Q_INVOKABLE void selectCandidate(int number)` | FR-006/FR-012 | `number` 超出 `[1, currentCandidates.length]` 范围或当前无锚定词时 **MUST** 为空操作（no-op），**MUST NOT** 抛出异常或使会话进入未定义状态（宪章原则 VI 第 3 条）；命中有效候选时按 `candidate-selection-replacement-contract.md` 规则计算替换结果并通过下述信号回传 |

## 四、新增信号

| 信号 | 对应 spec 条款 | 说明 |
|---|---|---|
| `void candidateReplacementReady(const QString &newText, int newCursorPosition)` | FR-006/FR-007/FR-020 | `selectCandidate()` 命中有效候选后发出；QML **MUST** 据此将 `rawInput.text`/`cursorPosition` 更新为给定值，并显式重新调用 `submitKeystrokes(newText, newCursorPosition)`（`TextInput.text` 程序化赋值不会自动触发 `onTextEdited`，`research.md` §6） |
| `void currentCandidatesChanged()` | — | `currentCandidates` 的 `Q_PROPERTY` 既有 `NOTIFY` 惯例，`submitKeystrokes()` 每次完成 `buildLattice()` 后统一 emit |

## 五、数字键 1–9 拦截规则（FR-012，QML 侧实现，非 C++ 契约但记录于此以保持完整性）

QML `rawInput`（`TextInput`）`Keys.onPressed` **MUST**：数字键 1–9 且无修饰键（`Qt.NoModifier`）按下时，若 `数字 <= currentCandidates.length`，调用 `selectCandidate(数字)` 并 `event.accepted = true`；否则（含数字超出候选项数量、或存在修饰键）**MUST NOT** 设置 `accepted`，回落到 `TextInput` 默认按键插入行为（FR-012 第二句"超出候选项数量时数字正常插入"）。

## 六、变更影响评估（判定程序 (a)–(e) 核对）

- **(a) 外部可观察行为**：新增候选列表 UI 区域与数字键选择能力——这正是本特性 spec.md 的核心授权范围本身（FR-001/FR-006 等），非超出显式需求的新增。
- **(b) 公共契约/数据模型**：本契约文件即是该增量的记录载体；`data-model.md` 七已同步记录。
- **(c) 安全/隐私**：否——纯本地文本编辑交互，不涉及任何数据留存/共享。
- **(d) 依赖/部署**：否——复用既有 `Q_PROPERTY`/`Q_INVOKABLE`/信号机制与既有 `QVariantMap` 风格，不新增第三方依赖。
- **(e) 成本/性能量级**：`selectCandidate()`/`currentCandidates()` 均为 O(候选数≤9) 的轻量派生计算，不改变 `buildLattice()` 既有延迟量级（FR-016）。

**结论**：本契约增量全部内容均可直接追溯至 spec.md FR-001/002/006/010/011/012/013/014 原文或本轮 `/speckit.plan` 对话中用户已显式决策的 `submitKeystrokes()` 签名方案（`research.md` §3），不构成需要额外 `[NEEDS CLARIFICATION]` 的事项。

## 七、本契约未决定的事项

候选区域具体视觉样式（字体、间距、颜色）留待 `/speckit.tasks`/`/speckit.implement` 阶段按既有浮窗 QML 视觉风格自主确定（spec Assumptions 已明确授权，宪章原则 IV）。
