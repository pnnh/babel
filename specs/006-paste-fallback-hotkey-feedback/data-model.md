# Data Model: 粘贴降级与热键冲突反馈补完

**Spec**: [spec.md](./spec.md) ｜ **Research**: [research.md](./research.md)

## 阅读说明

本文件记录本特性对既有实体（`specs/001-floating-ime-converter/data-model.md` §4 `TargetFocusContext`、§10 `InjectionAttemptResult`）的**增量变更**。既有实体中未被下表提及的部分原样有效，未变更。本文件全部字段均已回填最终值（唯一一项架构决策点——通知常驻图标方案——已由用户在 `research.md` §2 显式决策），不存在任何"【待决】"占位。

---

## 一、`TargetFocusContext`（001 §4 刻意留白条款，本特性正式补齐）

001 `data-model.md` §4 明确记录"目标窗口在浮窗唤起期间被切换/最小化/关闭后应如何表现"为刻意留白，未定义失效判断规则。本特性据此补齐，但**不新增字段**——`TargetFocusContext` 结构体本身（`platformWindowRef`/`capturedAt`/`valid`）保持不变，补齐的是"如何判断该结构体此刻是否仍然有效"这一行为规则，承载于新增的 `PasteInjector::isTargetFocusStillValid()` 方法（见下）。

## 二、`PasteInjector` 接口增量（`babel/src/inject/PasteInjector.h`）

### 1. 新增虚方法（默认实现向后兼容）

```cpp
class PasteInjector {
 public:
  virtual ~PasteInjector() = default;
  virtual InjectionAttemptResult attemptPaste(const babel::focus::TargetFocusContext &targetFocus) = 0;

  /// 006-paste-fallback-hotkey-feedback（FR-001/FR-002，research.md §1）：
  /// 判断 targetFocus 捕获时刻记录的目标窗口/进程，此刻是否仍是系统当前
  /// 焦点。默认实现返回 true（既有行为：不检测、总是视为仍然有效），保证
  /// 001～005 既有调用点与既有测试替身（仅覆盖 attemptPaste()）完全源码
  /// 兼容。
  virtual bool isTargetFocusStillValid(const babel::focus::TargetFocusContext &targetFocus) const { return true; }
};
```

| 字段/方法 | 返回类型 | 取值规则 | 来源 |
|---|---|---|---|
| `isTargetFocusStillValid(targetFocus)` | `bool` | `true`：`targetFocus` 对应窗口/进程此刻仍是系统当前焦点；`false`：已失效（含判断本身不确定的情形，统一归为 `false`，见 Acceptance Scenario 3） | FR-001/FR-002 |

### 2. 三平台真实实现（`research.md` §1 表格，`PasteInjector_{win,mac,x11}.*`）

| 文件 | 实现方式 |
|---|---|
| `PasteInjector_win.cpp` | 重新调用 `GetForegroundWindow()`，与 `targetFocus.platformWindowRef` 转换回的 `HWND` 做指针比较 |
| `PasteInjector_mac.mm` | 重新执行"取屏幕前到后排序窗口列表中第一个 `layer==0` 窗口的 `kCGWindowOwnerPID`"逻辑（与 `FocusContext.cpp::captureCurrentFocus()` 同构），与 `targetFocus.platformWindowRef` 转换回的 `pid_t` 比较 |
| `PasteInjector_x11.cpp` | 重新调用 `XGetInputFocus()`，与 `targetFocus.platformWindowRef` 转换回的 `Window` 做整数比较；`XOpenDisplay` 失败时返回 `false`（判断不确定 → 按失效处理） |

## 三、`SessionController` 行为增量（`babel/src/session/SessionController.{h,cpp}`）

### 1. `confirm()` 方法增量（FR-001/FR-003/FR-004）

```cpp
void SessionController::confirm() {
  if (m_state == FloatingSessionState::Hidden) {
    return;
  }
  if (m_clipboardWriter) {
    m_clipboardWriter->write(m_outputPreview.content);
  }

  // 006-paste-fallback-hotkey-feedback（FR-001，data-model.md 三.1）：在
  // 调用 attemptPaste() 之前先判断目标窗口是否仍是系统当前焦点；已失效时
  // MUST NOT 仍尝试模拟粘贴（FR-004 风险防范），直接走既有"已复制到剪贴板，
  // 请手动粘贴"降级分支（与 001 FR-012 完全相同文案，FR-003 不新增差异化
  // 文案）。
  const bool targetStillValid =
      !m_pasteInjector || m_pasteInjector->isTargetFocusStillValid(m_targetFocus);

  babel::inject::InjectionAttemptResult result;
  if (targetStillValid && m_pasteInjector) {
    result = m_pasteInjector->attemptPaste(m_targetFocus);
  } else {
    result.outcome = babel::inject::InjectionOutcome::OtherFailure;  // 复用既有非 Succeeded 降级分支，不新增枚举值
  }

  if (targetStillValid && result.outcome == babel::inject::InjectionOutcome::Succeeded) {
    // 既有成功路径（001 FR-008/FR-009），不变。
    ...
  } else {
    // 既有统一"已复制到剪贴板，请手动粘贴"降级路径（001 FR-012），不变、不新增文案。
    ...
  }
}
```

**MUST NOT 新增 `InjectionOutcome` 枚举值**（FR-003 要求不改变既有 FR-008/FR-009 成功路径；`targetStillValid == false` 时直接复用既有的"非 Succeeded 统一降级"分支即可，不需要为"目标已失效"单独定义一个新的 outcome 类型）。

### 2. 新增成员字段（`SessionController.h`）

```cpp
private:
  std::unique_ptr<QSystemTrayIcon> m_trayIcon;       // FR-005，仅生产环境 bootstrapPlatformDefaults() 创建
  bool m_hotkeyFailureNoticeShown = false;           // FR-008 防重复提示守卫
```

均为非 `Q_PROPERTY`（通知是系统级 UI，不经过浮窗/QML 渲染，无需 QML 可见状态，`research.md` §2）。

### 3. `bootstrapPlatformDefaults()` 增量（FR-005/FR-006/FR-008，`research.md` §2 完整代码）

紧邻既有 `hotkeyRegistered` 判断分支之后，新增托盘图标创建 + 条件通知逻辑；不改变既有 `spdlog::info`/`spdlog::warn` 诊断日志行为。

## 四、与既有契约的关系

- 不改变 001 `contracts/injection-outcome-contract.md` 定义的 `InjectionAttemptResult.outcome` 四取值集合与对应 UI 文案（`targetStillValid == false` 复用既有"非 Succeeded"统一文案分支，未新增取值）。
- 不改变 001 `contracts/global-hotkey-contract.md` "MUST NOT 自行发明冲突提示文案或处理策略"的既有约束——本特性新增的是"提示存在性与承载机制"（托盘通知），具体文案由实现阶段参考 `research.md` §2 建议措辞但非规格强制。
