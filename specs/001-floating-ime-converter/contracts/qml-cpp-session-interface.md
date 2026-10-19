# Contract: QML ↔ C++ 浮窗会话接口

**Spec**: [spec.md](../spec.md) ｜ **Data Model**: [data-model.md](../data-model.md)

## 适用范围

本应用为单机桌面 GUI 应用，不对外暴露网络 API；项目实际暴露的"接口"是 **QML 前端界面与 C++ 后端逻辑之间的契约**（`content/FloatingWindow.qml` 与 `src/` 下各模块的交互面）。本契约文档定义该交互面的属性（property）、信号（signal）、可调用方法（`Q_INVOKABLE`），供 `/speckit.tasks` 拆分任务与 `/speckit.implement` 落地时遵循，不规定具体 C++ 类的内部实现细节。

命名统一采用 spec 原文的区域名称（原始输入槽 / 中文输出预览 / 学习提示气泡），避免引入 spec 之外的概念。

## 契约对象：`SessionController`

暴露给 QML 的单例/上下文属性对象，承载一次浮窗会话（`FloatingSessionState`，见 `data-model.md` §6）的全部可观察状态。

### 只读属性（Properties）

| 属性 | 类型 | 对应 spec 条款 | 说明 |
|------|------|----------------|------|
| `rawInputSlotText` | string | FR-002（原始输入槽）、FR-003 | 展示 `NormalizedCandidateText.content`，**不是**用户原始击键的逐字回显 |
| `outputPreviewText` | string | FR-002（中文输出预览）、FR-004 | 展示 `StandardOutputPreview.content` |
| `outputPreviewState` | enum: `Idle`/`Debouncing`/`Generating`/`Ready`/`Stale` | FR-004、User Story 3 验收场景 2 | 驱动"可感知的等待状态提示"展示，不规定具体文案/样式 |
| `learningTipText` | string | FR-002（学习提示气泡） | 可选展示内容 |
| `learningTipVisible` | bool | FR-002 | 为 `false` 时浮窗不渲染该区域 |
| `modelAvailable` | bool | FR-011 | 为 `false` 时，`outputPreviewState` 固定为不可用态，QML 侧展示"标准中文生成功能不可用"提示 |
| `manualPasteHintVisible` | bool | FR-009、FR-012 | 确认后若需要展示"已复制到剪贴板，请手动粘贴"提示则置 `true`；此属性 MUST NOT 携带具体失败原因（见 `injection-outcome-contract.md`） |

### 可调用方法（Invokable Methods）

| 方法 | 参数 | 对应 spec 条款 | 说明 |
|------|------|----------------|------|
| `submitKeystrokes(text: string)` | 当前完整原始输入草稿 | FR-003、FR-004、FR-005 | 每次击键后由 QML 调用，驱动快流同步纠错与慢流防抖调度；内部更新 `RawDraftText`/`NormalizedCandidateText` |
| `confirm()` | 无 | FR-006、FR-007、FR-008 | 回车确认：触发剪贴板写入与粘贴注入尝试，随后会话状态迁移至 `Confirmed` → `Hidden` |
| `cancel()` | 无 | FR-006 | Esc 或再次触发热键：会话状态迁移至 `Cancelled` → `Hidden`，MUST NOT 产生任何剪贴板写入或粘贴动作 |

### 信号（Signals）

| 信号 | 参数 | 对应 spec 条款 | 说明 |
|------|------|----------------|------|
| `sessionInvoked()` | 无 | FR-001 | 全局热键触发浮窗显示时发出，供 QML 侧播放显示动效/聚焦输入框 |
| `sessionClosed(reason: enum Confirmed/Cancelled)` | 关闭原因 | FR-006 | 浮窗隐藏时发出 |

## 前置/后置条件

- `confirm()` 的前置条件：调用时 `outputPreviewText` 所代表的内容即为将被写入剪贴板的最终文本；若 `outputPreviewState` 处于 `Debouncing`/`Generating`（慢流尚未就绪），具体处理策略（等待/使用现有内容/提示用户）对应 spec Edge Cases 第 2 条，**未**被转化为正式 FR，本契约不代为规定，留待该 Edge Case 被澄清后再定义。
- `confirm()` 的后置条件：剪贴板写入（FR-007）**MUST** 先于粘贴注入尝试（FR-008）完成，确保即便注入失败，用户仍可通过剪贴板手动粘贴（FR-012）。
