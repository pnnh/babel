# Research: 粘贴降级与热键冲突反馈补完

**Spec**: [spec.md](./spec.md)

## 阅读说明

本文件记录 `/speckit.plan` 阶段对 spec.md 已澄清决策（FR-001/FR-005/FR-006，见 `## Clarifications`）的技术落地方式研究，均基于**直接阅读现有代码**（而非网络搜索）得出，具体文件引用见各节。

---

## §1 FR-001/FR-002：目标窗口有效性判断——三平台均可复用既有"焦点捕获"逻辑，无需新增检测 API

### 关键发现：三平台 `PasteInjector::attemptPaste()` 均不针对特定窗口句柄投递按键，而是投递到"系统当前焦点"

逐一核实 `babel/src/inject/PasteInjector_{win,mac,x11}.*` 后确认：

- **Windows**（`PasteInjector_win.cpp`）：`SendInput()` 向系统级输入队列投递合成 `Ctrl+V` 按键事件，由操作系统路由至**当前拥有键盘焦点的窗口**，与 `targetFocus` 参数完全无关（该参数当前标记为 `/*targetFocus*/` 未使用）。
- **macOS**（`PasteInjector_mac.mm`）：`CGEventPost(kCGHIDEventTap, ...)` 同样投递到 HID 事件分发层，由系统路由至**当前前台应用**，`targetFocus` 参数同样未使用。
- **Linux X11**（`PasteInjector_x11.cpp`）：`XTestFakeKeyEvent()` 通过 XTest 扩展在 X 服务器层面合成按键事件，同样投递给**当前拥有输入焦点的窗口**，`targetFocus` 参数同样未使用。

**结论（决定了本特性的核心设计）**：三个平台的粘贴注入实现从一开始就是"投递给当前系统焦点"，而非"投递给某个具体窗口句柄"。这意味着 FR-001/FR-004 描述的风险场景——"内容被静默注入到一个用户未预期的无关窗口"——的**真实根源**是"当前系统焦点已经不是唤起浮窗时记录的那个窗口/进程"，而不是传统意义上的"窗口句柄已失效（被销毁）"。因此，正确的有效性判断不是"该窗口是否还存在"，而是**"该窗口/进程现在是否仍是系统当前焦点"**——这恰好是 `babel/src/focus/FocusContext.cpp` 的 `captureCurrentFocus()` 已经在做的事情（捕获"当前焦点"），只是此前只在浮窗唤起瞬间调用一次。

### 三平台具体比对方式（复用 `FocusContext.cpp` 既有条件编译分支，不引入新 API）

| 平台 | `TargetFocusContext::platformWindowRef` 捕获时刻存的是什么 | 确认时刻如何判断"是否仍是当前焦点" |
|---|---|---|
| Windows | `GetForegroundWindow()` 返回的 `HWND` | 重新调用 `GetForegroundWindow()`，与存储的 `HWND` 做指针比较；不相等（含现在返回 `NULL`，如前台窗口已关闭）即判定为失效 |
| macOS | 屏幕前到后排序窗口列表中第一个 `layer==0`（正常层级）窗口的 `kCGWindowOwnerPID`（`CGWindowListCopyWindowInfo`） | 重新执行同一套"取 `layer==0` 的第一个窗口的 owner pid"枚举逻辑，与存储的 pid 做整数比较；不相等（含现在找不到任何 `layer==0` 窗口）即判定为失效 |
| Linux X11 | `XGetInputFocus()` 返回的 `Window`（XID） | 重新调用 `XGetInputFocus()`，与存储的 `Window` 做整数比较；不相等（含 `XOpenDisplay` 失败、或返回 `None`/`PointerRoot` 等非窗口特殊值）即判定为失效 |

**与 FR-002"若某平台不存在可行原生检测 API，该平台 MUST 保持现状"条款的关系**：经核实，**三个平台均存在可行检测能力**（直接复用各自既有的焦点捕获实现，做一次"重新捕获 + 比较"），不存在"某平台完全无法实现"的情形，FR-002 的"平台能力兜底"条款在本特性的具体实现中不会被触发（记录于此，供日后复核；若未来某平台的捕获逻辑本身被重写导致不再可行，需重新评估）。

**Acceptance Scenario 3（判断本身不确定时按失效处理）的落地**：若重新捕获过程本身失败（如 X11 `XOpenDisplay` 返回空、macOS 窗口列表为空），新捕获结果的 `valid` 字段为 `false`；"失效的新捕获"与"存储的有效捕获"比较结果必然不相等，自然落入"判定为失效"分支，无需额外的第三态处理逻辑。

### 设计落地：新增 `PasteInjector::isTargetFocusStillValid()` 虚方法（默认返回 `true`，向后兼容）

不新增独立的 `FocusContext` 自由函数（会与 `PasteInjector` 接口产生"由谁负责判断"的歧义），而是直接在 `babel::inject::PasteInjector` 抽象接口新增一个虚方法：

```cpp
class PasteInjector {
 public:
  virtual InjectionAttemptResult attemptPaste(const babel::focus::TargetFocusContext &targetFocus) = 0;

  /// 006-paste-fallback-hotkey-feedback（FR-001/FR-002）：判断 targetFocus
  /// 捕获时刻记录的目标窗口/进程，此刻是否仍是系统当前焦点。默认实现返回
  /// true（既有行为：不检测、总是视为仍然有效），保证 001～005 既有调用点
  /// 与既有测试替身（仅覆盖 attemptPaste()）完全源码兼容；三个平台的真实
  /// 实现 override 为上表描述的"重新捕获 + 比较"逻辑。
  virtual bool isTargetFocusStillValid(const babel::focus::TargetFocusContext &targetFocus) const { return true; }
};
```

**可测试性收益**：`SessionController::confirm()` 是 `Q_INVOKABLE` 公开方法（非 005 遇到的 `onSlowPathTriggered()` 那种 private 限制），`m_pasteInjector` 已是既有可注入依赖（`setPasteInjector()`）。测试可注入一个仅 override `isTargetFocusStillValid()` 返回 `false` 的测试替身，确定性验证"目标失效→跳过 attemptPaste()→展示手动粘贴提示"这条路径，无需依赖真实操作系统窗口状态。

---

## §2 FR-005/FR-006：热键注册失败通知——`QSystemTrayIcon`（Widgets 模块已链接，零新依赖）

### 关键发现：`Widgets` 模块已经链接，`QSystemTrayIcon` 无需新增依赖

核实 `babel/CMakeLists.txt`：`find_package(Qt6 COMPONENTS Core Qml Quick Gui QuickControls2 Widgets ShaderTools QuickDialogs2 REQUIRED)` 已包含 `Widgets`；`QSystemTrayIcon` 定义于 `QtWidgets/qsystemtrayicon.h`（已在本机 Qt 安装目录核实头文件存在，`#include <QSystemTrayIcon>` 可直接使用，vcpkg.json/CMakeLists.txt 均无需改动）。

### `/speckit.plan` 阶段修订：常驻图标而非"用完即藏"

最初设想"操作系统级一次性通知、不新增常驻 UI"，但核实 `QSystemTrayIcon::showMessage()` 头文件签名后发现：该方法挂在一个 `QSystemTrayIcon` 实例上，多数平台后端要求图标已 `show()`（至少短暂可见）才能可靠弹出通知；若追求"真正零图标"，需改为各平台原生 API（macOS `UNUserNotificationCenter` 需用户授权通知权限、Windows Toast 需 AUMID 注册、Linux 需 D-Bus `org.freedesktop.Notifications`），工程代价高且三平台行为不一致。与用户确认后（见 `spec.md` Clarifications "`/speckit.plan` 阶段修订"），采纳**常驻托盘图标**方案：应用启动后创建 `QSystemTrayIcon` 并保持 `show()`，不再"用完即藏"；该图标同时作为后续规划中新功能所需的常驻基础设施提前落地。

### 图标资源：核实项目当前无任何图标资源文件

`find` 全仓库未发现任何 `.icns`/`.ico`/图标 PNG 资源，`CMakeLists.txt`/`main.cpp`/QML 均无 `setWindowIcon`/`QIcon(...)` 调用——应用目前完全依赖平台默认图标。本特性为托盘图标选用 Qt 内置标准图标（`QApplication::style()->standardIcon(QStyle::SP_ComputerIcon)`，零新增资源文件）作为占位符；自定义品牌图标美术设计超出本特性范围（见 Out of Scope），后续如需替换，仅需替换这一处 `QIcon` 构造调用。

### 可用性兜底：`QSystemTrayIcon::isSystemTrayAvailable()`

部分精简 Linux 桌面环境（如某些纯平铺式窗口管理器）可能不提供系统托盘协议支持。创建前 MUST 调用 `isSystemTrayAvailable()`；返回 `false` 时跳过图标创建与通知调用，仅保留既有 `spdlog::warn` 诊断日志（与现有 X11 Wayland 环境下 `PasteInjector` 判定 `UnsupportedEnvironment` 的"尽力而为、不崩溃"风格一致）。

### 集成点：`SessionController::bootstrapPlatformDefaults()`

复用既有 `hotkeyRegistered` 布尔分支（`babel/src/session/SessionController.cpp` 现有 `spdlog::warn("全局热键注册失败...")` 所在位置）：

```cpp
if (QSystemTrayIcon::isSystemTrayAvailable()) {
  m_trayIcon = std::make_unique<QSystemTrayIcon>();
  m_trayIcon->setIcon(qApp->style()->standardIcon(QStyle::SP_ComputerIcon));
  m_trayIcon->setToolTip(QStringLiteral("Babel"));
  m_trayIcon->show();
  if (!hotkeyRegistered && !m_hotkeyFailureNoticeShown) {
    m_trayIcon->showMessage(/*title=*/QStringLiteral("Babel"),
                             /*msg=*/QStringLiteral("全局热键当前不可用，可能与其他应用的快捷键冲突"),
                             QSystemTrayIcon::Warning);
    m_hotkeyFailureNoticeShown = true;
  }
}
```

- `m_hotkeyFailureNoticeShown` 守卫对应 FR-008（防骚扰重复提示）：当前代码库 `registerHotkey()` 仅在 `bootstrapPlatformDefaults()` 内被调用一次（无重试循环，核实 `grep registerHotkey` 全仓库引用后确认），该守卫在当前架构下是面向未来的防御性设计（若后续特性引入热键重新注册能力，此处已天然防止重复提示），不改变当前单次调用场景下的行为。
- 文案"全局热键当前不可用，可能与其他应用的快捷键冲突"为实现阶段的具体措辞提案，FR-007 明确"MUST NOT 预先写定"为规格强制要求——此处仅作为 plan 阶段的参考实现建议，`/speckit.tasks`/`/speckit.implement` 阶段可视情况调整具体文字，不视为对 FR-007 的违反（FR-007 约束的是"规格本身不得强制规定文案"，非"实现时不能给出参考文案"）。

### 可测试性边界（与 005 同类限制，如实记录）

`m_trayIcon` 的创建与 `showMessage()` 调用均位于 `bootstrapPlatformDefaults()`（生产专用，测试 MUST NOT 调用，与 005 `tasks.md` Notes 记录的同一限制），故该路径的真实端到端效果（真实弹出系统通知、真实常驻图标可见）无法从 `SessionControllerTest.cpp` 自动化触发，留待 `quickstart.md` 人工验证（与 004/005 既有处理方式一致）。可自动化验证的确定性子集：`m_hotkeyFailureNoticeShown` 防重复触发的布尔逻辑本身（若封装为独立可调用的小函数/方法）、`QSystemTrayIcon::isSystemTrayAvailable()` 返回 `false` 时的跳过分支（可通过既有测试环境本身大概率是无图形桌面的 CI/沙箱环境间接验证"不崩溃"这一消极断言，但不构成对通知内容的正面验证）。

---

## §3 Technical Context 决策汇总

| 维度 | 决策 | 依据 |
|---|---|---|
| 新增依赖 | 无（`QSystemTrayIcon` 已随 `Widgets` 模块可用） | `babel/CMakeLists.txt`/`vcpkg.json` 核实 |
| 窗口有效性判断 | 复用既有 `FocusContext.cpp` 捕获逻辑做"重新捕获+比较"，三平台均可行，不新增平台原生 API | §1 |
| 接口改动 | `PasteInjector` 新增 `isTargetFocusStillValid()` 虚方法，默认 `true` 向后兼容 | §1 |
| 通知机制 | `QSystemTrayIcon`，常驻图标（非一次性），图标用 Qt 内置标准图标占位 | §2（含用户对 spec.md 的修订决策） |
| 测试边界 | `confirm()`/`PasteInjector` 测试替身路径可确定性自动化测试；`bootstrapPlatformDefaults()` 内的托盘图标创建/真实通知弹出延期至 quickstart.md 人工验证（与 004/005 同类限制一致） | §1 末尾、§2 末尾 |

**本阶段无遗留 `[NEEDS CLARIFICATION]`**：spec.md 已完成的 3 项澄清（FR-001/FR-005/FR-006）均已在本研究中落实为具体技术方案；唯一在 research 过程中新发现、需要用户决策的架构分歧点（通知机制的"常驻图标 vs 真零图标"权衡）已取得用户显式决策并回填至 `spec.md` Clarifications（"`/speckit.plan` 阶段修订"）。
