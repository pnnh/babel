# Contract: QML ↔ C++ 浮窗会话接口（005 变更增量）

**Spec**: [spec.md](../spec.md) ｜ **Data Model**: [data-model.md](../data-model.md) ｜ **Research**: [research.md](../research.md) ｜ **基线**: [002 契约](../../002-lattice-dict-model-switch/contracts/qml-cpp-session-interface.md)（004 已增量一次，见 [004 契约增量](../../004-speculative-decoding/contracts/qml-cpp-session-interface-increment.md)）

## 适用范围与阅读方式

本文件记录本特性对 002 既有契约的**第二处增量**（第一处为 004 新增的 `speculativeDecodingNoticeText`/`recommendedDraftProfiles`，已生效、未变更）。002 原文件、004 增量文件中未被下表提及的全部属性/方法/信号均**原样有效，未变更**，含 `learningTipText`/`learningTipVisible` 本身——本特性**不改变**这两个只读属性的读取语义（仍是 `READ`-only、`NOTIFY` 驱动），只是首次为它们提供有意义的内容来源（spec.md Input 描述的既有缺口）。

## 一、新增属性

| 属性 | 类型 | 对应 spec 条款 | 说明 |
|------|------|----------------|------|
| `learningTipEnabled` | `bool`（可读写） | FR-008 | 学习提示气泡展示功能开关，默认 `true`；用户在设置页面切换；`WRITE` 方法 `setLearningTipEnabled()` 切至 `false` 时 MUST 立即将 `learningTipVisible` 置为不可见（不等待下一轮生成完成），`data-model.md` 三.1 |

本特性**不**新增/修改任何方法（`Q_INVOKABLE`）或信号——`learningTipTextChanged`/`learningTipVisibleChanged` 两个信号 002 阶段已声明，本特性只是首次真正触发它们（在 `onSlowPathTriggered()` 完成回调与 `resetSessionData()` 中），信号本身的声明/语义不变；新增属性 `learningTipEnabled` 的 `WRITE`/`NOTIFY` 均为全新声明（`learningTipEnabledChanged`），与既有信号互不干扰。

## 二、变更影响评估（判定程序 (a)–(e) 核对）

- **(a) 外部可观察行为**：新增一个可读写开关属性，且首次让已声明两年多（001 起）但从未展示过内容的 `learningTipText`/`learningTipVisible` 产生真实效果——这正是本特性 spec.md 的核心授权范围本身（FR-001～FR-004），不是"超出显式需求的新增契约"。
- **(b) 公共契约/数据模型**：本契约文件即是该增量的记录载体，`data-model.md` 已同步记录 `m_learningTipEnabled` 字段。
- **(c) 安全/隐私**：否——纯 UI 展示开关，不涉及任何数据留存/共享。
- **(d) 依赖/部署**：否——复用既有 `Q_PROPERTY`/QML 绑定机制，不新增依赖。
- **(e) 成本/性能量级**：开关本身零开销；间接效果是开关开启且处于中/高配档位时，`generate()` 单次调用的采样上限从 256 提升到 400（`research.md` §6），这一变化范围已被 spec.md Clarifications"仅中/高配两档启用"明确接受（低配档位零影响，满足 FR-009）。

**结论**：本契约增量全部内容均可直接追溯至 spec.md FR-008 原文，不构成需要额外 `[NEEDS CLARIFICATION]` 的事项。

## 三、本契约未决定的事项

- 设置页面开关的具体视觉样式细节（文案措辞、颜色）留待 `/speckit.tasks`/`/speckit.implement` 阶段按宪章原则 IV 确定，`data-model.md` 五已指明复用 004 `speculativeDecodingEnabled` 开关完全相同的实现模式（不新增依赖）。
