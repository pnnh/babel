# Contract: 剪贴板写入与粘贴注入结果

**Spec**: [spec.md](../spec.md) ｜ **Research**: [research.md](../research.md) §4.2 ｜ **Data Model**: [data-model.md](../data-model.md) §10

## 适用范围

本契约定义"确认（`confirm()`）→ 剪贴板写入 → 粘贴注入尝试"链路的抽象行为契约。注入库选型已由用户显式委派、Agent 采纳 `research.md` §4.2 的 Recommendation 定稿为**方案 1：分平台自研薄封装**（直接调用 Windows `SendInput` / macOS `CGEventPost` / Linux X11 `XTest` 扩展，不引入第三方注入库）；该实现 MUST 满足本契约定义的行为，本契约文本因与具体实现解耦而无需因此次选型修改。

## 调用顺序（FR-007/FR-008，不可颠倒）

1. `ClipboardWriter.write(standardOutputText: string) -> void`：将 `StandardOutputPreview.content` 写入系统剪贴板（基于 `QClipboard`，见 `research.md` §4.1）。**MUST** 在步骤 2 之前完成。
2. `PasteInjector.attemptPaste(targetFocusContext: TargetFocusContext) -> InjectionAttemptResult`：尝试向 `TargetFocusContext` 对应窗口模拟标准粘贴操作（Windows/Linux `Ctrl+V`，macOS `Cmd+V`）。

## `InjectionAttemptResult` 契约（见 `data-model.md` §10）

| `outcome` 取值 | 触发场景 | 对外 UI 行为（FR-009/FR-012，统一、不可拆分） |
|----------------|----------|----------------------------------------------|
| `Succeeded` | 注入执行成功 | 浮窗正常关闭，不展示额外提示 |
| `UnsupportedEnvironment` | 平台/合成器不支持注入（典型 Linux Wayland） | 展示"已复制到剪贴板，请手动粘贴" |
| `PermissionDenied` | 权限未授予（如 macOS 未授予 Accessibility 权限） | 展示"已复制到剪贴板，请手动粘贴"（与上一行**文案完全相同**） |
| `OtherFailure` | 平台理论上支持但本次执行失败（User Story 3 场景 3） | 展示"已复制到剪贴板，请手动粘贴"（与上两行**文案完全相同**） |

## 强约束

- **MUST**：无论 `outcome` 取何值（除 `Succeeded`），用户看到的提示文案 **MUST** 完全一致，均为"已复制到剪贴板，请手动粘贴"（FR-012 显式要求"不区分具体失败原因、不展示差异化文案"）。
- **MUST NOT**：任何实现 **MUST NOT** 以日志/调试信息之外的形式，向最终用户暴露 `outcome` 的具体取值或失败原因细节。
- **MUST**：`ClipboardWriter.write()` 的成功执行是 `PasteInjector.attemptPaste()` 调用的前置条件；即便 `attemptPaste()` 的 `outcome` 非 `Succeeded`，剪贴板内容也 **MUST** 已经写入完毕，保证用户手动粘贴时内容正确（FR-012）。
