# Contract: 目标窗口焦点有效性判断

**Spec**: [spec.md](../spec.md) ｜ **Research**: [research.md](../research.md) §1 ｜ **Data Model**: [data-model.md](../data-model.md) 二

## 适用范围

本契约扩展 001 `contracts/injection-outcome-contract.md`，定义 `confirm()` 调用 `PasteInjector::attemptPaste()` **之前**新增的前置判断步骤（FR-001/FR-002）。本契约不修改 `attemptPaste()` 本身的既有行为契约。

## 调用顺序（FR-001，新增步骤 1.5，插入既有步骤 1/2 之间）

1. `ClipboardWriter.write(standardOutputText: string) -> void`（既有步骤，不变）。
2. **新增** `PasteInjector.isTargetFocusStillValid(targetFocus: TargetFocusContext) -> bool`：判断 `targetFocus` 捕获时刻记录的窗口/进程，此刻是否仍是系统当前焦点。
3. 若步骤 2 返回 `false`：**MUST NOT** 调用 `attemptPaste()`，直接按"非 Succeeded"统一降级路径处理（见下）。
4. 若步骤 2 返回 `true`：按既有流程调用 `PasteInjector.attemptPaste(targetFocus) -> InjectionAttemptResult`（既有步骤，不变）。

## 强约束

- **MUST**：`isTargetFocusStillValid()` 返回 `false` 时，系统 MUST 展示与 001 `contracts/injection-outcome-contract.md` 完全相同的"已复制到剪贴板，请手动粘贴"文案，不得新增差异化文案（FR-003）。
- **MUST NOT**：`isTargetFocusStillValid()` 返回 `false` 时，MUST NOT 仍然调用 `attemptPaste()`（FR-001/FR-004，避免内容被模拟粘贴注入到当前实际前台的无关窗口）。
- **MUST**：`isTargetFocusStillValid()` 的判断本身在 Windows/macOS/Linux X11 三个平台上行为语义一致——均为"重新捕获当前系统焦点并与捕获时刻记录值比较"（FR-002，`research.md` §1）。
- **MUST**：若判断过程本身无法得出明确结论（如平台 API 调用失败），MUST 返回 `false`（与"已确认失效"同等对待，Acceptance Scenario 3），MUST NOT 定义第三种返回值或抛出异常。
- **向后兼容**：`PasteInjector` 基类为 `isTargetFocusStillValid()` 提供默认实现 `{ return true; }`；仅覆盖 `attemptPaste()` 的既有测试替身无需任何修改即可继续编译、行为不变（视为"目标恒有效"，与 001～005 现状完全一致）。

## 测试契约（`research.md` §1 可测试性收益）

- `confirm()` 为 `Q_INVOKABLE` 公开方法，可被测试直接调用。
- 注入一个仅 override `isTargetFocusStillValid()` 返回 `false` 的测试替身 `PasteInjector` 子类，断言：① `attemptPaste()` 从未被调用（计数器为 0）；② `manualPasteHintVisible` 变为 `true`；③ 浮窗不因此崩溃或进入未定义状态。
