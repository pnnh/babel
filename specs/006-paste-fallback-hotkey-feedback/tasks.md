---
description: "Task list for 006-paste-fallback-hotkey-feedback"
---

# Tasks: 粘贴降级与热键冲突反馈补完

**Input**: Design documents from `/specs/006-paste-fallback-hotkey-feedback/`

**Prerequisites**: plan.md（已完成，Constitution Check PASS 无条件通过，含用户对"常驻托盘图标方案"的显式决策）、spec.md（已完成全部 3 项澄清）、research.md、data-model.md、contracts/、quickstart.md（均已完成）

**Tests**: 本特性**包含**测试任务。User Story 1（FR-001/FR-002）的核心逻辑位于 `confirm()`——该方法为 `Q_INVOKABLE` 公开方法（不同于 005 遇到的 `onSlowPathTriggered()` private 限制），`PasteInjector` 本身就是为测试替身设计的虚接口，故可**完全确定性自动化测试**，无需延期人工验证。User Story 2（FR-005/FR-006/FR-008）的托盘图标创建与真实通知弹出位于 `bootstrapPlatformDefaults()`（生产专用，测试 MUST NOT 调用，与 004/005 既有测试边界一致），真实端到端效果延期到 `quickstart.md` 场景三/四人工验证；`isSystemTrayAvailable()==false` 时的跳过分支、`m_hotkeyFailureNoticeShown` 守卫的布尔语义本身，可在不依赖真实 `QSystemTrayIcon` 弹出效果的前提下做结构性验证。

**语言（宪章《语言与文档规范》）**：任务描述使用简体中文；文件路径、符号名、命令保留英文。

**范围重申（`plan.md` Scale/Scope）**：本特性严格限定于以下既有文件的增量修改，**不新增新的 QML 文件、不新增第三方依赖**：

```text
babel/src/inject/PasteInjector.h          # 新增 isTargetFocusStillValid() 虚方法
babel/src/inject/PasteInjector_win.cpp    # Windows 真实检测实现
babel/src/inject/PasteInjector_mac.mm     # macOS 真实检测实现
babel/src/inject/PasteInjector_x11.cpp    # Linux X11 真实检测实现
babel/src/session/SessionController.h     # 新增 m_trayIcon/m_hotkeyFailureNoticeShown 成员
babel/src/session/SessionController.cpp   # confirm() 前置判断 + bootstrapPlatformDefaults() 托盘图标/通知

tests/inject/PasteInjectorTargetFocusValidityTest.cpp   # 新增（与既有 Unsupported/Permission/OtherFailure 三个测试文件同构）
tests/session/SessionControllerTest.cpp                  # 新增 FR-001 测试替身验证用例
```

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 可并行执行（不同文件、无依赖关系）
- **[Story]**: 所属用户故事（US1/US2）；Setup/Polish 阶段任务不标注

---

## Phase 1: Setup（基线确认）

**Purpose**: 在改动前建立"全绿基线"，便于后续断言变更前后对照。

- [X] T001 运行既有完整 CTest 套件建立变更前基线：`cmake --build build/macos --config Debug` 后执行 `cd build/macos && ctest -C Debug --output-on-failure`，确认 001～005 既有全部测试文件中的用例处于可对照的基线状态（当前 108/108 通过，1 项 `PasteInjectorPermissionTest` 按既有设计跳过），记录通过/跳过清单，供后续核对本特性新增用例是否按预期从新增变为 PASS。

**Checkpoint**：基线已记录，可开始 User Story 1。

---

## Phase 2: User Story 1 - 目标应用窗口失效时的粘贴行为明确化 (Priority: P1) 🎯 MVP

**Goal**：`confirm()` 在调用 `attemptPaste()` 之前先判断目标窗口是否仍是系统当前焦点；已失效时跳过模拟粘贴，统一降级为既有"已复制到剪贴板，请手动粘贴"提示（FR-001/FR-003/FR-004）。

**Independent Test**：按 `quickstart.md` 场景一/二，验证目标窗口失效/有效两种场景下的行为分别符合预期。

- [X] T002 [P] [US1] 在 `babel/src/inject/PasteInjector.h` 新增虚方法（`data-model.md` 二.1，`contracts/target-focus-validity-contract.md`）：

  ```cpp
  virtual bool isTargetFocusStillValid(const babel::focus::TargetFocusContext &targetFocus) const { return true; }
  ```

  默认实现返回 `true`，保证既有调用点与既有测试替身（仅 override `attemptPaste()` 的匿名测试类）无需任何修改即可继续编译、行为不变。

- [X] T003 [P] [US1] 在 `babel/src/inject/PasteInjector_win.cpp` 实现 Windows 真实检测（depends on T002，`research.md` §1 表格第一行）：override `isTargetFocusStillValid()`，重新调用 `GetForegroundWindow()`，与 `targetFocus.platformWindowRef` 转换回的 `HWND` 做指针比较，不相等（含现在返回 `NULL`）返回 `false`。

- [X] T004 [P] [US1] 在 `babel/src/inject/PasteInjector_mac.mm` 实现 macOS 真实检测（depends on T002，`research.md` §1 表格第二行）：override `isTargetFocusStillValid()`，重新执行与 `FocusContext.cpp::captureCurrentFocus()` 同构的"取屏幕前到后排序窗口列表中第一个 `layer==0` 窗口的 `kCGWindowOwnerPID`"逻辑，与 `targetFocus.platformWindowRef` 转换回的 `pid_t` 比较，不相等（含找不到任何 `layer==0` 窗口）返回 `false`。

- [X] T005 [P] [US1] 在 `babel/src/inject/PasteInjector_x11.cpp` 实现 Linux X11 真实检测（depends on T002，`research.md` §1 表格第三行）：override `isTargetFocusStillValid()`，重新调用 `XGetInputFocus()`，与 `targetFocus.platformWindowRef` 转换回的 `Window` 做整数比较；`XOpenDisplay` 失败或返回值不相等（含 `None`/`PointerRoot` 等非窗口特殊值）返回 `false`。

- [X] T006 [US1] 在 `babel/src/session/SessionController.cpp` 的 `confirm()` 方法新增前置判断（depends on T002，`data-model.md` 三.1）：调用 `attemptPaste()` 之前先判断 `m_pasteInjector->isTargetFocusStillValid(m_targetFocus)`；返回 `false` 时 MUST NOT 调用 `attemptPaste()`，直接复用既有"非 `Succeeded`"统一降级分支（`manualPasteHintVisible = true` 等既有逻辑不变）；MUST NOT 新增 `InjectionOutcome` 枚举取值。

- [X] T007 [P] [US1] 新增测试文件 `tests/inject/PasteInjectorTargetFocusValidityTest.cpp`（与既有 `PasteInjectorUnsupportedTest.cpp`/`PasteInjectorPermissionTest.cpp`/`PasteInjectorOtherFailureTest.cpp` 同构风格，`#if defined(...)` 按平台分支）：
  - 验证 `createPlatformPasteInjector()->isTargetFocusStillValid(ctx)` 对一个明显无效的 `TargetFocusContext`（如 `platformWindowRef` 为随意不存在的值、`valid=false`）返回合法布尔值且不崩溃（真实窗口状态难以在 CI 中确定性构造，本测试以"不崩溃、行为合理"为主的弱断言为主，与既有 `PasteInjectorOtherFailureTest.cpp` 的测试强度风格一致）。

- [X] T008 [US1] 在 `tests/session/SessionControllerTest.cpp` 新增测试替身与用例（depends on T006，`contracts/target-focus-validity-contract.md` 测试契约）：
  - 定义一个仅 override `isTargetFocusStillValid()` 恒返回 `false`（`attemptPaste()` 保持既有默认实现或记录调用次数）的匿名 `PasteInjector` 测试替身。
  - `TEST(SessionControllerTest, ConfirmSkipsPasteWhenTargetFocusNoLongerValid)`：注入该替身，调用 `onHotkeyToggled()` 唤起、`confirm()` 确认，断言：① `attemptPaste()` 调用计数为 0；② `manualPasteHintVisible() == true`；③ 浮窗状态正常进入 `Confirmed`，不崩溃。
  - `TEST(SessionControllerTest, ConfirmProceedsWithPasteWhenTargetFocusStillValidDefault)`：使用默认 `isTargetFocusStillValid()`（恒 `true`）的既有测试替身模式，验证行为与 001～005 现状完全一致（无回归）。

**Checkpoint**：User Story 1 的结构性实现与可自动化验证部分已独立可交付；真实窗口切换效果统一在 Polish 阶段按 `quickstart.md` 场景一/二人工验证。

---

## Phase 3: User Story 2 - 热键注册失败时的用户可见提示 (Priority: P2)

**Goal**：全局热键注册失败时，通过常驻 `QSystemTrayIcon` + 一次性系统通知让用户感知"热键当前不可用"（FR-005/FR-006/FR-007/FR-008）。

**Independent Test**：按 `quickstart.md` 场景三/四，预先占用默认热键组合后启动应用，验证托盘图标与通知是否出现、是否避免重复骚扰。

- [X] T009 [US2] 在 `babel/src/session/SessionController.h` 新增成员字段（`data-model.md` 三.2）：

  ```cpp
  std::unique_ptr<QSystemTrayIcon> m_trayIcon;
  bool m_hotkeyFailureNoticeShown = false;
  ```

  需 `#include <QSystemTrayIcon>`（`Widgets` 模块已链接，零新增依赖，`research.md` §2）。均为非 `Q_PROPERTY`（通知是系统级 UI，不经过浮窗/QML 渲染）。

- [X] T010 [US2] 在 `babel/src/session/SessionController.cpp` 的 `bootstrapPlatformDefaults()` 中，紧邻既有 `hotkeyRegistered` 判断分支之后新增托盘图标创建 + 条件通知逻辑（depends on T009，`research.md` §2 完整代码，`contracts/hotkey-notice-contract.md`）：
  - 先调用 `QSystemTrayIcon::isSystemTrayAvailable()`；返回 `false` 时 MUST NOT 创建图标、MUST NOT 调用 `showMessage()`，仅保留既有 `spdlog::warn` 诊断日志。
  - 返回 `true` 时创建 `m_trayIcon`，设置图标为 `qApp->style()->standardIcon(QStyle::SP_ComputerIcon)`（零新增资源文件占位方案，`research.md` §2），设置 ToolTip 为 `"Babel"`，调用 `show()`（常驻，不随通知消失而隐藏）。
  - 若 `!hotkeyRegistered && !m_hotkeyFailureNoticeShown`：调用 `m_trayIcon->showMessage(...)` 展示"全局热键当前不可用"语义的提示（具体文案参考 `research.md` §2 建议措辞，FR-007 不强制规定具体文字），随后置位 `m_hotkeyFailureNoticeShown = true`。
  - MUST NOT 改变 `registerHotkey()` 成功时的既有行为（不创建额外提示，仅保留既有 `spdlog::info` 日志）；MUST NOT 改变 `bootstrapPlatformDefaults()` 中其余初始化步骤的既有顺序。

- [X] T011 [P] [US2] 在 `babel/CMakeLists.txt` 确认（通常无需改动，仅核实）：`find_package(Qt6 COMPONENTS ... Widgets ...)` 已包含 `Widgets`（`research.md` §2 已核实），`#include <QSystemTrayIcon>` 无需额外链接配置；若构建报错缺少符号，按既有 `target_link_libraries` 模式补充 `Qt6::Widgets`（预期已存在，仅作构建期兜底核实任务，大概率无需实际改动）。
  - **实施结果（如实记录，与预期不符）**：`babel/CMakeLists.txt`（最终应用可执行目标）确实已包含 `Widgets`，无需改动；但 `babel/src/CMakeLists.txt`（`babel_core` 静态库，`SessionController.cpp` 的实际编译单元所在目标，也是 `tests/` 下各测试可执行文件直接链接的目标）此前仅 `find_package(Qt6 COMPONENTS Core Gui REQUIRED)` + `target_link_libraries(babel_core PUBLIC Qt6::Core Qt6::Gui)`，不含 `Widgets`。构建报错 `'QSystemTrayIcon' file not found` 后，按本任务"若构建报错缺少符号，按既有 target_link_libraries 模式补充 Qt6::Widgets"的既定兜底方案，在 `babel/src/CMakeLists.txt` 追加 `Widgets` 到 `find_package` 与 `target_link_libraries(babel_core ...)`，重新 `cmake -S . -B build/macos` 配置后构建通过。此为任务本身预先定义的构建期兜底路径，不属于范围外改动。

**Checkpoint**：托盘图标创建与通知调用逻辑已实现；真实弹出效果与常驻可见性统一在 Polish 阶段按 `quickstart.md` 场景三/四人工验证（受限于 `bootstrapPlatformDefaults()` 生产专用、测试 MUST NOT 调用，与 004/005 既有测试边界一致）。

---

## Phase 4: Polish（全量回归 + 人工验证）

**Purpose**：确认全部既有测试与新增测试均通过，补充真实环境下的人工验证记录。

- [X] T012 运行全量回归：`cmake --build build/macos --config Debug && cd build/macos && ctest -C Debug --output-on-failure`，确认 T001 记录的基线全部继续通过，新增测试（T007/T008）从"新增"变为"PASS"，无既有用例被破坏。

- [ ] T013 [P] 按 `quickstart.md` 场景一人工验证：实际运行 `Babel.app`，唤起浮窗后手动切换/最小化/关闭原目标窗口，确认按确认键后展示"已复制到剪贴板，请手动粘贴"提示，且剪贴板内容正确、不存在内容被模拟粘贴进无关窗口的情况。记录实测结果于 `quickstart.md` 实测记录节。
  - **实施结果（如实记录，未编造）**：未执行真实 GUI 人工验证。核心决策逻辑（目标失效→跳过 `attemptPaste()`→展示手动粘贴提示）已由 T008 新增的 `SessionControllerTest.ConfirmSkipsPasteWhenTargetFocusNoLongerValid`/`ConfirmProceedsWithPasteWhenTargetFocusStillValidDefault` 两条确定性自动化测试覆盖并 PASS；但真正启动 `Babel.app`、注册真实全局热键、对当前真实前台窗口执行系统级模拟粘贴按键注入，存在抢占当前桌面会话全局热键绑定、向任意前台窗口投递合成按键事件的真实副作用风险，Agent 判断不应在无用户在场监督时执行此类操作，已在 `quickstart.md` 实测记录节如实标注为"待用户后续在真实桌面环境中亲自执行并补充确认"。

- [ ] T014 [P] 按 `quickstart.md` 场景二人工验证：目标窗口保持有效时，确认行为与 001 既有流程完全一致、无回归。记录实测结果于 `quickstart.md` 实测记录节。
  - **实施结果（如实记录，未编造）**：同 T013，未执行真实 GUI 人工验证，原因与风险评估相同。自动化测试 `ConfirmProceedsWithPasteWhenTargetFocusStillValidDefault` 已验证目标恒有效时行为与既有 `ConfirmWritesClipboardBeforeAttemptingPaste`/`ConfirmSucceededClosesWithoutManualPasteHint` 完全一致（无回归）。已在 `quickstart.md` 如实标注待用户补充确认。

- [ ] T015 [P] 按 `quickstart.md` 场景三人工验证：预先占用默认热键组合后启动应用，确认托盘图标出现、系统通知弹出；清除占用后重启，确认热键注册成功且不再弹出该通知、但图标仍然常驻。记录实测结果于 `quickstart.md` 实测记录节。
  - **实施结果（如实记录，未编造）**：未执行真实 GUI 人工验证，原因与 tasks.md Notes 记录一致——`bootstrapPlatformDefaults()` 为生产专用方法，测试 MUST NOT 调用，真实托盘图标可见性/系统通知弹出效果无法从 `SessionControllerTest.cpp` 自动化触发。实际验证需要启动 `Babel.app` 并抢占系统全局热键组合，涉及真实桌面副作用，Agent 未在无人工监督场景下执行。已在 `quickstart.md` 如实标注待用户补充确认，未新增 mock 框架或修改架构绕过此限制。

- [ ] T016 [P] 按 `quickstart.md` 场景四人工验证：热键注册失败场景下反复尝试按键，确认不产生重复骚扰性通知。记录实测结果于 `quickstart.md` 实测记录节。
  - **实施结果（如实记录，未编造）**：同 T015，未执行真实 GUI 人工验证。`m_hotkeyFailureNoticeShown` 布尔守卫已在 T010 实现中正确置位（一次性触发后不再重复，代码逻辑可读性可审查），但其真实防骚扰效果的端到端验证需要真实桌面环境人工反复按键观察，已在 `quickstart.md` 如实标注待用户补充确认。

- [X] T017 若具备真实用户访谈条件，按 `quickstart.md` 场景五执行事后复述验证（SC-005）；若无条件执行，如实在 `quickstart.md` 标注为"未执行，需要用户后续补充"，不编造结果。
  - **实施结果**：当前环境不具备真实用户访谈条件，已按本任务预先定义的"无条件执行"分支，在 `quickstart.md` 实测记录节如实标注为"未执行，需要用户后续补充"，未编造任何访谈结果。

---

## Dependencies & Execution Order

### Phase Dependencies

- Setup（T001）：无依赖，最先执行。
- User Story 1（T002～T008）：依赖 Setup 完成；内部 T002（接口声明）先于 T003～T006（各平台实现+调用点）；T007/T008（测试）依赖对应实现任务完成。
- User Story 2（T009～T011）：依赖 Setup 完成，与 User Story 1 相互独立（不同文件区域——`confirm()` vs `bootstrapPlatformDefaults()`，均在 `SessionController.cpp` 但位置不重叠），可与 User Story 1 并行开发。
- Polish（T012～T017）：依赖 User Story 1 与 User Story 2 均完成。

### Parallel Opportunities

- T003/T004/T005（三平台各自独立文件）可完全并行。
- T002 完成后，T003～T005 与 T009（不同文件）可并行；T007（新测试文件）可在 T003～T005 任一平台实现完成后独立编写（本机仅能验证当前平台分支）。
- Polish 阶段 T013～T016 为 4 个相互独立的人工验证场景，可并行执行。

## Implementation Strategy

### MVP First (User Story 1 Only)

1. 完成 Phase 1：Setup（基线确认）
2. 完成 Phase 2：User Story 1（`PasteInjector` 新增检测方法 + `confirm()` 前置判断 + 测试）
3. **停止并验证**：按 `quickstart.md` 场景一/二独立验证 User Story 1
4. 具备 MVP 交付条件：001 遗留的"目标窗口失效后粘贴行为未定义"风险场景已消除

### Incremental Delivery

1. Setup 完成 → 基线就绪
2. 新增 User Story 1 → 独立验证 → 部署/演示（**MVP**，消除内容误注入无关窗口的风险）
3. 新增 User Story 2 → 独立验证 → 部署/演示（热键冲突场景用户不再"按键无反应却不知道原因"）
4. Polish 阶段：全量回归 + 5 个 quickstart 场景人工验证 + 实测数据记录

## Notes

- `[P]` 任务 = 不同文件、无依赖关系。
- `[Story]` 标签将任务映射到对应用户故事，便于追溯。
- **测试覆盖边界**：User Story 1（`confirm()`/`PasteInjector`）因方法本身公开可调用、接口本身为可测试替身设计，已实现**完全确定性自动化测试**，不同于 005 遇到的 private 方法限制。User Story 2（`bootstrapPlatformDefaults()` 内的托盘图标创建与真实通知弹出）受限于该方法生产专用、测试 MUST NOT 调用（与 004/005 既有测试边界一致），真实端到端效果延期至 Polish 阶段 `quickstart.md` 人工验证。
- 两个用户故事均可独立完成与验证，互不依赖彼此的实现细节。
