# Contract: QML ↔ C++ 浮窗会话接口（004 变更增量）

**Spec**: [spec.md](../spec.md) ｜ **Data Model**: [data-model.md](../data-model.md) ｜ **Research**: [research.md](../research.md) ｜ **基线**: [002 契约](../../002-lattice-dict-model-switch/contracts/qml-cpp-session-interface.md)

## 适用范围与阅读方式

本文件记录本特性对 `specs/002-lattice-dict-model-switch/contracts/qml-cpp-session-interface.md`（002 已确立、003 未修改）的**唯一一处增量**。FR-002 明文要求本特性"MUST NOT 改变 001/002/003 已确立的 QML↔C++ 会话契约"；用户本轮在发起本特性时已预先明确授权一项例外——"新增设置页面展示'未启用加速'提示所需的只读属性"。本文件据此仅记录该一项增量，002 原文件中未被下表提及的全部属性/方法/信号（含 `switchModel()`/`availableModels`/`recommendedProfiles`/`currentModelName`/`modelSwitching`/`fallbackNoticeText`/`settingsVisible` 等）**原样有效，未变更**。

## 一、新增属性

| 属性 | 类型 | 对应 spec 条款 | 说明 |
|------|------|----------------|------|
| `speculativeDecodingNoticeText` | string | FR-006 | 当前选定目标模型档位是否启用投机解码加速及原因的说明文案（`data-model.md` 四）：低配档位恒为空字符串（不展示、FR-009）；中/高配档位且未配置对应草稿模型时为非空提示（如"未检测到草稿模型，投机解码未启用"）；中/高配档位且已配置时为空字符串或正向提示（具体文案留待 `/speckit.tasks`） |
| `recommendedDraftProfiles` | list（元素含 `size`/`modelName`/`quantization`/`approxSizeDescription`，见 `data-model.md` 二） | FR-004 | 固定两档草稿模型推荐说明性数据，供设置页面展示候选规格（User Story 2） |

## 二、变更影响评估（判定程序 (a)–(e) 核对）

- **(a) 外部可观察行为**：新增两个只读属性本身是一种外部可观察行为的"新增"，但均为**只读展示型**数据（无新增可调用方法、无新增信号、无对既有属性语义的修改），且该新增已由用户本轮明确预先授权（见"适用范围"）。
- **(b) 公共契约/数据模型**：本契约文件即是该增量的记录载体；不改变既有 `data-model.md` 条目的结构或语义。
- **(c)/(d)/(e)**：均为否——不涉及安全/隐私/权限，不新增依赖/部署方式，不带来显著成本或性能量级变化（纯文本展示）。

**结论**：本契约增量不构成需要额外 `[NEEDS CLARIFICATION]` 的事项，已在"适用范围"中注明其获得用户预先授权的依据。

## 三、本契约未决定的事项

- `speculativeDecodingNoticeText` 的具体文案措辞（除 FR-006 原文示例"未检测到草稿模型，投机解码未启用"外）留待 `/speckit.tasks`/`/speckit.implement` 阶段按宪章原则 IV 确定（纯文案措辞，不改变属性语义本身）。
- `recommendedDraftProfiles` 中 `approxSizeDescription` 的具体数值留待 `/speckit.tasks` 阶段按实际 GGUF 文件大小填入（`data-model.md` 二已注明）。
- 是否需要在 `switchModel()` 完成后额外发出一个新信号（如 `speculativeDecodingStatusChanged`）而非仅依赖属性的 `NOTIFY`：本契约选择**不新增信号**，`speculativeDecodingNoticeTextChanged`/等既有 `NOTIFY` 信号命名模式已足以满足 QML 侧绑定更新需求，不新增额外信号以保持改动面最小（宪章原则 V）。
