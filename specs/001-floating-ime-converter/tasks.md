---

description: "Task list for AI 辅助双语浮窗转换器（MVP）"
---

# Tasks: AI 辅助双语浮窗转换器（MVP）

**Input**: Design documents from `/specs/001-floating-ime-converter/`

**Prerequisites**: [plan.md](./plan.md)（必需）、[spec.md](./spec.md)（必需）、[research.md](./research.md)、[data-model.md](./data-model.md)、[contracts/](./contracts/)、[quickstart.md](./quickstart.md)

**Tests**: 本特性已选型 **GoogleTest**（见 `plan.md`/`research.md` §6，vcpkg 官方 port `gtest`），并符合宪章原则 VI 预授权基线"为新增逻辑补对应测试"；以下各用户故事阶段均包含对应测试任务。

**语言（宪章《语言与文档规范》）**：任务描述使用简体中文；文件路径、符号名、命令保留英文。

**Organization**: 任务按用户故事（spec.md 的 P1/P2/P3）分组，确保每个故事可独立实现、独立测试、独立交付增量价值。

## Format: `[ID] [P?] [Story] Description`

- **[P]**：可与同阶段内其他 [P] 任务并行执行（不同文件、无先后依赖）
- **[Story]**：任务所属用户故事（US1/US2/US3），仅用户故事阶段任务标注，Setup/Foundational/Polish 阶段不标注
- 每条任务均含明确文件路径

## Path Conventions

沿用仓库既有单工程桌面应用布局（见 `plan.md` Project Structure）：

- `vcpkg.json`、`CMakeLists.txt`：仓库根目录
- `babel/`：既有 Qt6 Quick 可执行工程（`babel/CMakeLists.txt`、`babel/main.cpp`、`babel/content/`）
- `babel/src/`：本特性新增的纯 C++ 业务逻辑目录，按 `fastpath/`（快流）、`slowpath/`（慢流）、`hotkey/`（全局热键）、`inject/`（剪贴板与注入）、`focus/`（目标焦点上下文）、`session/`（浮窗会话编排）六个子目录划分
- `tests/`：本特性新增的 GoogleTest 测试目录，结构镜像 `babel/src/` 子目录划分

---

## Phase 1: Setup（项目初始化）

**Purpose**：为后续所有阶段准备构建基础设施（vcpkg 依赖声明、CMake 子目录骨架），不包含任何业务逻辑

- [X] T001 [P] 在仓库根 `vcpkg.json` 中新增 `gtest`、`llama-cpp` 依赖声明（`llama-cpp` 引用既有 overlay port `vcpkg/ports/llama-cpp`，macOS 平台启用 `metal` feature，不启用 `download`/`tools`，对应 `research.md` §1/§6 已选型决策）
- [X] T002 [P] 创建 `babel/src/CMakeLists.txt`：声明 `babel_core` 静态库 target 骨架（暂无业务源文件，仅占位 `add_library(babel_core STATIC)` 与 include 目录设置），供后续各子目录任务填充源文件
- [X] T003 [P] 创建 `tests/CMakeLists.txt`：声明 `find_package(GTest CONFIG REQUIRED)`，并为后续各测试目标预留 `gtest_discover_tests` 调用占位（暂无测试源文件）
- [X] T004 在仓库根 `CMakeLists.txt` 中新增 `add_subdirectory(tests)`；在 `babel/CMakeLists.txt` 中新增 `add_subdirectory(src)`，并链接 `babel_core` 静态库与 `llama-cpp`（经 vcpkg `find_package` 引入）到既有 `Babel` 可执行 target（依赖 T001、T002、T003）

**Checkpoint**：构建骨架就绪，`cmake --preset <platform>` 可成功 configure（业务源文件尚为空，属预期）

---

## Phase 2: Foundational（阻塞性前置基础设施）

**Purpose**：建立所有用户故事共享的核心数据类型与编排骨架；**任何用户故事实现 MUST 等待本阶段完成后才能开始**

**⚠️ CRITICAL**：本阶段任务完成前，不得开始 Phase 3+ 的任何用户故事任务

- [X] T005 [P] 定义 `RawDraftText`/`NormalizedCandidateText` 数据类型（`content`/`cursorPosition`/`lastKeystrokeAt`/`updatedAt` 字段，对应 `data-model.md` §1/§2）于 `babel/src/session/DraftTypes.h`
- [X] T006 [P] 定义 `StandardOutputPreview` 数据类型与生成状态迁移枚举 `GenerationState`（`Idle`/`Debouncing`/`Generating`/`Ready`/`Stale`，对应 `data-model.md` §3）于 `babel/src/session/OutputPreviewTypes.h`
- [X] T007 [P] 定义 `TargetFocusContext` 数据类型（`platformWindowRef` 占位类型、`capturedAt`，对应 `data-model.md` §4，仅声明不含平台捕获实现）于 `babel/src/focus/FocusContext.h`
- [X] T008 [P] 定义 `InjectionAttemptResult` 契约类型（`outcome` 枚举 `Succeeded`/`UnsupportedEnvironment`/`PermissionDenied`/`OtherFailure`、`attemptedAt`，对应 `data-model.md` §10、`contracts/injection-outcome-contract.md`）于 `babel/src/inject/InjectionResult.h`
- [X] T009 [P] 定义 `HotkeyBinding` 配置类型（默认组合键 Windows/Linux `Ctrl+Alt+Space`、macOS `Control+Option+Space`，对应 `data-model.md` §9、`contracts/global-hotkey-contract.md`）于 `babel/src/hotkey/HotkeyBinding.h`
- [X] T010 定义 `FloatingSessionState` 会话状态机枚举（`Hidden`/`Invoked`/`Editing`/`Confirmed`/`Cancelled`/`ModelUnavailable`，对应 `data-model.md` §6）于 `babel/src/session/SessionController.h`（依赖 T005、T006、T007、T008）
- [X] T011 实现 `SessionController` 骨架类：声明 `contracts/qml-cpp-session-interface.md` 定义的只读属性（`rawInputSlotText`/`outputPreviewText`/`outputPreviewState`/`learningTipText`/`learningTipVisible`/`modelAvailable`/`manualPasteHintVisible`）、信号（`sessionInvoked`/`sessionClosed`）与可调用方法声明（`submitKeystrokes`/`confirm`/`cancel`，方法体先占位不实现业务逻辑）于 `babel/src/session/SessionController.{h,cpp}`（依赖 T010）
- [X] T012 [P] 创建 `babel/content/FloatingWindow.qml`：无边框浮窗三段式静态布局骨架（原始输入槽、中文输出预览、学习提示气泡三个区域，仅占位展示，不接任何数据绑定，对应 FR-002）
- [X] T013 [P] 定义本地 LLM 模型文件约定本地目录路径常量工具（对应 `research.md` §7 委派结果：用户手动放置，应用检测约定目录）于 `babel/src/slowpath/ModelPaths.{h,cpp}`
- [X] T014 在 `babel/main.cpp` 中将 `SessionController` 注册为 QML 可访问的上下文属性/单例，并在 `babel/content/Main.qml` 中加载 `FloatingWindow.qml`（依赖 T011、T012）

**Checkpoint**：基础骨架就绪——所有用户故事现在可以开始并行实现

---

## Phase 3: User Story 1 - 混杂草稿一键转换为标准中文并自动上屏 (Priority: P1) 🎯 MVP

**Goal**：全局热键唤起浮窗 → 快流实时规整化展示 → 慢流防抖生成标准中文预览 → 回车确认 → 剪贴板写入 + 模拟粘贴注入，在 macOS/Linux X11 环境下标准中文结果无需手动操作即出现在原目标输入框内

**Independent Test**：按 `quickstart.md` 验证场景一，在 macOS 或 Linux X11 下完成"热键唤起→输入混杂草稿→查看预览→回车确认→验证目标应用内容"全链路

### Tests for User Story 1 ⚠️

> 先写测试，确认其在对应实现完成前处于 FAIL 状态

- [X] T015 [P] [US1] `FastCorrector` 单元测试（中英拼音混排样例的编辑距离纠错正确性、目标延迟 <15ms 基准）于 `tests/fastpath/FastCorrectorTest.cpp`
- [X] T016 [P] [US1] `PinyinTrie` 单元测试（全拼/简拼匹配命中率）于 `tests/fastpath/PinyinTrieTest.cpp`
- [X] T017 [P] [US1] `DebounceScheduler` 单元测试（防抖触发时序、输入再次变化时取消/重触发）于 `tests/slowpath/DebounceSchedulerTest.cpp`
- [X] T018 [P] [US1] `GlobalHotkey`/`HotkeyBinding` 契约测试（默认组合键校验、`ToggleFloatingWindow` 状态迁移语义，对应 `contracts/global-hotkey-contract.md`）于 `tests/hotkey/GlobalHotkeyTest.cpp`
- [X] T019 [P] [US1] `SessionController.confirm()`/`cancel()` 状态迁移与"剪贴板写入先于粘贴注入"调用顺序测试（对应 `contracts/injection-outcome-contract.md`）于 `tests/session/SessionControllerTest.cpp`

### Implementation for User Story 1

- [X] T020 [P] [US1] 建立快流词典 sqlite3 schema（`DictionaryEntry` 表：`entryId`/`surfaceForm`/`pinyinFull`/`pinyinAbbrev`/`frequencyWeight`，对应 `data-model.md` §7）于 `babel/src/fastpath/DictionarySchema.{h,cpp}`
- [X] T021 [P] [US1] 实现 `KeyboardProximityMap`（QWERTY 物理键位邻近编辑距离权重表，编译期常量，对应 `data-model.md` §8）于 `babel/src/fastpath/KeyboardProximityMap.{h,cpp}`
- [X] T022 [US1] 实现 `PinyinTrie`（拼音/简拼 Trie 查询，基于 T020 schema 读取词典数据）于 `babel/src/fastpath/PinyinTrie.{h,cpp}`（依赖 T020）
- [X] T023 [US1] 实现 `FastCorrector`（编辑距离前缀索引 + Trie 查询 + 键位邻近加权，对外统一入口：击键 → 规整化候选文本，目标端到端延迟 <15ms，对应 FR-003/SC-002）于 `babel/src/fastpath/FastCorrector.{h,cpp}`（依赖 T021、T022）
- [X] T024 [P] [US1] 实现 `LlmEngine`（基于 llama.cpp 加载 Qwen2.5-3B-Instruct GGUF Q4_K_M，读取 T013 约定目录，对应 FR-013）于 `babel/src/slowpath/LlmEngine.{h,cpp}`（依赖 T013）
- [X] T025 [US1] 实现 `DebounceScheduler`（原始输入变化后防抖触发/取消慢流生成，驱动 `Idle→Debouncing→Generating→Ready/Stale` 迁移，目标首批内容 <300ms、整句 <800ms，对应 FR-004/FR-005/SC-003）于 `babel/src/slowpath/DebounceScheduler.{h,cpp}`（依赖 T024、T006）
- [X] T026 [P] [US1] 实现 `GlobalHotkey` 跨平台抽象接口于 `babel/src/hotkey/GlobalHotkey.h`（依赖 T009）
- [X] T027 [P] [US1] 实现 Windows 全局热键绑定 `RegisterHotKey` 于 `babel/src/hotkey/GlobalHotkey_win.cpp`（依赖 T026）
- [X] T028 [P] [US1] 实现 macOS 全局热键绑定 Carbon `RegisterEventHotKey` 于 `babel/src/hotkey/GlobalHotkey_mac.mm`（依赖 T026）
- [X] T029 [P] [US1] 实现 Linux X11 全局热键绑定 `XGrabKey`（经 `libxcb`/`libX11`）于 `babel/src/hotkey/GlobalHotkey_x11.cpp`（依赖 T026）
- [X] T030 [P] [US1] 实现 `FocusContext` 平台焦点窗口捕获（Windows `HWND`/macOS 前台应用+窗口引用/X11 `Window` ID，对应 FR-001）于 `babel/src/focus/FocusContext.cpp`（依赖 T007）
- [X] T031 [P] [US1] 实现 `ClipboardWriter`（基于 `Qt6::Gui` `QClipboard` 写入最终标准中文文本，对应 FR-007）于 `babel/src/inject/ClipboardWriter.{h,cpp}`
- [X] T032 [P] [US1] 实现 `PasteInjector` 跨平台抽象接口于 `babel/src/inject/PasteInjector.h`（依赖 T008）
- [X] T033 [P] [US1] 实现 macOS 模拟粘贴注入 `CGEventPost`（成功路径，对应 FR-008/SC-004）于 `babel/src/inject/PasteInjector_mac.mm`（依赖 T032）
- [X] T034 [P] [US1] 实现 Linux X11 模拟粘贴注入 `XTest` 扩展（成功路径，对应 FR-008/SC-004）于 `babel/src/inject/PasteInjector_x11.cpp`（依赖 T032）
- [X] T035 [P] [US1] 实现 Windows 模拟粘贴注入 `SendInput`（尽力而为路径，对应 FR-008/FR-010）于 `babel/src/inject/PasteInjector_win.cpp`（依赖 T032）
- [X] T036 [US1] 在 `SessionController` 中实现 `submitKeystrokes()`：击键 → `FastCorrector` → 同步更新 `rawInputSlotText`；驱动 `DebounceScheduler` → `LlmEngine` → 更新 `outputPreviewText`/`outputPreviewState`（对应 FR-003/FR-004/FR-005）于 `babel/src/session/SessionController.cpp`（依赖 T023、T025、T011）
- [X] T037 [US1] 在 `SessionController` 中实现 `confirm()`：`ClipboardWriter.write()` 先于 `PasteInjector.attemptPaste()` 调用（不可颠倒）；`outcome=Succeeded` 时浮窗正常关闭、不展示额外提示；迁移至 `Confirmed→Hidden`（对应 FR-006/FR-007/FR-008）于 `babel/src/session/SessionController.cpp`（依赖 T031、T033、T034、T035、T011）
- [X] T038 [US1] 在 `SessionController` 中实现 `cancel()`：Esc 或再次触发热键 → `Cancelled→Hidden`，MUST NOT 产生任何剪贴板写入或粘贴动作（对应 FR-006）于 `babel/src/session/SessionController.cpp`（依赖 T011）
- [X] T039 [US1] 接入 `GlobalHotkey` → `SessionController`：热键触发时捕获 `TargetFocusContext`，发出 `sessionInvoked()`，迁移至 `Invoked`；再次按下时触发 `cancel()` 流程（对应 FR-001、`contracts/global-hotkey-contract.md`）于 `babel/src/session/SessionController.cpp`（依赖 T026、T027、T028、T029、T030、T011）
- [X] T040 [US1] 在 `babel/content/FloatingWindow.qml` 中绑定 `rawInputSlotText`/`outputPreviewText`/`outputPreviewState` 实时展示，并接入键盘事件（Enter → `confirm()`，Esc → `cancel()`）（依赖 T012、T036、T037、T038）
- [ ] T041 [US1] 按 `quickstart.md` 验证场景一（macOS 或 Linux X11）手工执行全部 7 个步骤，确认 Acceptance Scenario 1-6 与 SC-001~SC-004 均满足（依赖 T040）
  > **阻塞（已修正根因）**：此前记录的"沙箱无法访问 `~/vcpkg`/`~/Qt`"诊断有误——设置 `VCPKG_ROOT` 后 `cmake --preset macos` 可正常 configure/build，`Babel.app` 已在本会话中实际构建成功（过程中发现并修复了 `SessionController.h` 中 `QTimer` 前向声明置于 `babel::session` 命名空间内导致的不完整类型编译错误，见 git 历史）。真正受限的是：手工验证需要真实桌面 WindowServer 会话、人工按键唤起浮窗、观察真实目标应用粘贴结果——这是自动化沙箱无法执行的交互式人工操作，而非构建限制。对应契约行为已由本会话实际运行的 GoogleTest 覆盖并全部通过（经 `codesign --sign -` ad-hoc 签名后执行，详见 Phase 6 Polish 验证记录）。

**Checkpoint**：至此 User Story 1 应完全可独立运行与测试（MVP 完成点）

---

## Phase 4: User Story 2 - 不支持自动粘贴注入环境下的手动粘贴降级 (Priority: P2)

**Goal**：在 Linux Wayland 等不支持模拟粘贴注入的环境下，确认后仍保证剪贴板写入成功，并展示统一"已复制到剪贴板，请手动粘贴"提示，而非静默失败

**Independent Test**：按 `quickstart.md` 验证场景二，在已知不支持自动粘贴注入的合成器环境下完成一次确认，验证剪贴板内容与"请手动粘贴"提示

### Tests for User Story 2 ⚠️

- [X] T042 [P] [US2] `PasteInjector` 在 Wayland 会话下返回 `outcome=UnsupportedEnvironment` 的判定测试于 `tests/inject/PasteInjectorUnsupportedTest.cpp`
- [X] T043 [P] [US2] `SessionController` 针对非 `Succeeded` outcome 统一展示"已复制到剪贴板，请手动粘贴"、不泄露具体 outcome 差异的测试（对应 FR-012 强约束）于 `tests/session/ManualPasteHintTest.cpp`

### Implementation for User Story 2

- [X] T044 [US2] 在 Linux 粘贴注入模块中增加 Wayland/窗口合成器能力探测，探测到不支持注入时返回 `outcome=UnsupportedEnvironment`（对应 FR-009）于 `babel/src/inject/PasteInjector_x11.cpp`（依赖 T034、T008）
- [X] T045 [US2] 在 `SessionController.confirm()` 中处理非 `Succeeded` 的 `outcome`：置 `manualPasteHintVisible=true`，且 MUST NOT 以用户可见形式区分具体 outcome（对应 FR-009/FR-012）于 `babel/src/session/SessionController.cpp`（依赖 T037、T008）
- [X] T046 [US2] 在 `babel/content/FloatingWindow.qml` 中绑定 `manualPasteHintVisible` 展示统一提示文案"已复制到剪贴板，请手动粘贴"（依赖 T040、T045）
- [ ] T047 [US2] 按 `quickstart.md` 验证场景二（Wayland 环境）手工执行，确认 Acceptance Scenario 1-2 与 SC-005 均满足（依赖 T046）
  > **阻塞**：本会话沙箱环境为 macOS，无 Linux Wayland 桌面环境可供手工验证（与构建能力无关——构建本身已验证可行，见 T041 修正记录）；`PasteInjector_x11.cpp` 的 Wayland 探测逻辑已实现并由 T042 单元测试覆盖（已实际运行并通过），手工端到端验证需在具备 Linux Wayland 桌面环境的机器上另行执行。

**Checkpoint**：至此 User Story 1 与 User Story 2 应均可独立运行与测试

---

## Phase 5: User Story 3 - 管道/注入异常时的友好提示而非崩溃 (Priority: P3)

**Goal**：本地模型文件缺失/损坏、慢流耗时异常、或本应支持注入的平台上注入失败（如权限未授予）时，浮窗保持可操作状态并给出可理解提示，而非崩溃或无响应

**Independent Test**：按 `quickstart.md` 验证场景三，分别在"模型文件被移除"与"撤销注入权限"两种条件下完成一次确认流程，验证均给出可理解提示而非崩溃/无提示失败

### Tests for User Story 3 ⚠️

- [X] T048 [P] [US3] `LlmEngine` 模型文件缺失/损坏检测测试（`modelAvailable=false`，对应 FR-011）于 `tests/slowpath/LlmEngineUnavailableTest.cpp`
- [X] T049 [P] [US3] `SessionController` 迁移至 `ModelUnavailable` 状态的测试（快流仍可用、慢流不可用且不崩溃）于 `tests/session/ModelUnavailableTest.cpp`
- [X] T050 [P] [US3] macOS `PasteInjector` 在 Accessibility 权限未授予时返回 `outcome=PermissionDenied` 的判定测试于 `tests/inject/PasteInjectorPermissionTest.cpp`
- [X] T051 [P] [US3] Linux X11 `PasteInjector` 在"本应支持但本次执行失败"场景下返回 `outcome=OtherFailure` 的判定测试于 `tests/inject/PasteInjectorOtherFailureTest.cpp`

### Implementation for User Story 3

- [X] T052 [US3] 在 `LlmEngine` 中增加模型文件存在性/可加载性检测（读取 T013 约定目录），缺失/损坏/无法加载时置 `modelAvailable=false`（对应 FR-011）于 `babel/src/slowpath/LlmEngine.{h,cpp}`（依赖 T024、T013）
- [X] T053 [US3] 在 `SessionController` 中接入 `modelAvailable=false` → 迁移至 `ModelUnavailable` 状态，保持浮窗可操作、不崩溃、不无响应（对应 FR-011/SC-006）于 `babel/src/session/SessionController.cpp`（依赖 T010、T052）
- [X] T054 [US3] 在 macOS 粘贴注入中增加 Accessibility 权限状态检测，未授权时返回 `outcome=PermissionDenied`（对应 FR-012、User Story 3 验收场景 3）于 `babel/src/inject/PasteInjector_mac.mm`（依赖 T033、T008）
- [X] T055 [US3] 在 Linux X11 粘贴注入中区分"平台本应支持但本次执行失败"场景，返回 `outcome=OtherFailure`于 `babel/src/inject/PasteInjector_x11.cpp`（依赖 T034、T008、T044）
- [X] T056 [US3] 在 `babel/content/FloatingWindow.qml` 中为 `outputPreviewState=Debouncing`/`Generating` 增加可感知的等待状态纯文本提示（不要求动画效果，对应 User Story 3 验收场景 2/SC-006）（依赖 T040）
- [X] T057 [US3] 在 `babel/content/FloatingWindow.qml` 中为 `modelAvailable=false` 展示"标准中文生成功能不可用"提示，确保浮窗仍可操作（依赖 T040、T053）
- [ ] T058 [US3] 按 `quickstart.md` 验证场景三（模型文件缺失、慢流耗时异常、macOS 权限撤销三项）手工执行，确认 User Story 3 验收场景 1-3 与 SC-006 均满足（依赖 T056、T057）
  > **阻塞（已修正根因）**：构建本身不再受限（见 T041 修正记录）。对应行为已通过单元测试覆盖且本会话已实际运行全部通过：模型文件缺失场景见 T048/T049（`LlmEngineUnavailableTest`/`ModelUnavailableTest`）；macOS 权限撤销场景见 T050（`PasteInjectorPermissionTest`，本会话运行时进程已被判定为 Accessibility 受信任，`AXIsProcessTrusted()==true`，未授权分支按设计自动 `GTEST_SKIP`，需在未授权的真实桌面会话中运行才能覆盖该分支）。慢流耗时异常场景（Generating 状态下的等待提示观感）需人工观察真实浮窗 UI，这是需要真实桌面交互的部分，留待具备 WindowServer 会话的人工环境执行。

**Checkpoint**：所有用户故事（US1/US2/US3）现在应均可独立运行与测试

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**：跨用户故事的收尾工作

- [X] T059 [P] 实现 `learningTipText`/`learningTipVisible` 只读属性的数据接入（复用慢流生成过程中产生的辅助信息，MVP 内仅纯文本展示，对应 FR-002 可选低优先级区域）于 `babel/src/session/SessionController.{h,cpp}` 与 `babel/content/FloatingWindow.qml`
- [X] T060 [P] 审查 `babel/src/` 各模块头文件注释与跨平台边界条件处理一致性（无代码行为变更，仅补充/修正注释）
- [X] 构建与单元测试验证（补充记录，非 tasks.md 原定编号任务）：设置 `VCPKG_ROOT` 后 `cmake --preset macos` configure 成功（含 llama.cpp 源码构建），`cmake --build --target Babel` 构建出 `Babel.app` 成功；过程中发现并修复一处真实编译错误（`SessionController.h` 的 `QTimer` 前向声明误置于 `babel::session` 命名空间导致遮蔽全局 `::QTimer`，改为在全局作用域前向声明）；全部 11 个 GoogleTest 测试二进制编译链接成功，经 `codesign --sign -` ad-hoc 签名（规避本沙箱对未签名、链接 AppKit/Carbon/ApplicationServices 的可执行文件的随机 SIGKILL）后实际运行，33 个测试用例中 32 个 PASSED、1 个按设计 SKIPPED（`PasteInjectorPermissionTest`，因本沙箱进程 `AXIsProcessTrusted()` 返回 true，未授权分支需在真实未授权桌面会话中覆盖），0 个 FAILED。
- [ ] T061 按 `specs/001-floating-ime-converter/quickstart.md` 验证场景一的"粗略准确率验证"指引，使用多组真实中英混杂草稿样本人工抽样验证 SC-007（AI 生成标准中文"可直接使用或仅需极少修改"比例 MUST 不低于 60%）
  > **阻塞**：需要可运行的、已加载真实 Qwen2.5-3B-Instruct 模型的 `Babel.app` 对多组真实样本做人工抽样评分（构建本身不再受限，见上方验证记录）；模型文件本身需用户手动下载（research.md §7，MVP 不提供自动下载），且"可直接使用或仅需极少修改"的评分需要人工主观判断，留待模型文件就位后由人工执行。
- [ ] T062 [P] 执行 `quickstart.md` 全部三个验证场景的最终端到端回归确认（US1+US2+US3 组合场景不互相破坏）
  > **阻塞**：同 T041/T047/T058——需要真实桌面 WindowServer 会话与人工交互（唤起浮窗、真实目标应用验证粘贴结果），而非构建限制（构建已验证可行，见上方验证记录）；已通过本次新增的全部 11 个 GoogleTest 测试文件（33 个测试用例，32 PASSED/1 按设计 SKIPPED/0 FAILED）在源码层面覆盖 US1/US2/US3 的契约行为，端到端人工回归需在具备真实桌面环境的机器上另行执行。
- [X] T063 更新 `docs/` 下与浮窗转换器相关的现有说明文档（如 `docs/IME.md` 关联章节），标注 MVP 已交付范围与 Out of Scope 边界，避免文档与实现脱节

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**：无前置依赖，可立即开始
- **Foundational (Phase 2)**：依赖 Setup 完成——**阻塞**所有用户故事
- **User Story 1 (Phase 3)**：依赖 Foundational 完成；与 US2/US3 无相互依赖，但 US2/US3 在验证层面复用 US1 建立的端到端链路（`quickstart.md` 场景二/三均以"重复场景一步骤 1-5"为前提）
- **User Story 2 (Phase 4)**：依赖 Foundational 完成；实现上扩展 US1 已建立的 `PasteInjector`/`SessionController.confirm()`（T034、T037、T008），故建议在 US1 完成后再开始，以复用其注入与会话编排代码路径
- **User Story 3 (Phase 5)**：依赖 Foundational 完成；实现上扩展 US1 的 `LlmEngine`/`SessionController`（T024、T010）与 US2 建立的 `PasteInjector` outcome 判定机制（T044），故建议在 US1、US2 完成后再开始
- **Polish (Phase 6)**：依赖所需的用户故事全部完成

> 注：虽然 US2/US3 在**代码复用**层面建议在 US1 之后实施（三者共享同一组 `SessionController`/`PasteInjector` 文件），但三者各自新增的"行为分支"（成功路径 vs 不支持环境 vs 权限/其他失败 vs 模型不可用）在 `data-model.md`/`contracts/` 中定义为相互独立的契约分支，彼此之间无功能性阻塞依赖——这与 spec.md 中三个用户故事按 P1→P2→P3 优先级顺序交付、且各自可独立验收（见各自 Independent Test）的设计意图一致。

### User Story Dependencies

- **User Story 1 (P1)**：Foundational 完成后即可开始；不依赖其他用户故事
- **User Story 2 (P2)**：Foundational 完成后即可开始；复用 US1 的 `PasteInjector`/`SessionController` 实现以扩展行为分支，但不依赖 US1 的测试通过结果，可独立验收
- **User Story 3 (P3)**：Foundational 完成后即可开始；复用 US1 的 `LlmEngine`/`SessionController` 与 US2 的 outcome 判定机制以扩展行为分支，但不依赖 US1/US2 的测试通过结果，可独立验收

### Within Each User Story

- 测试任务 MUST 先于对应实现任务编写，且在实现完成前处于 FAIL 状态
- 数据/类型定义 → 独立模块实现 → `SessionController` 编排接入 → QML 绑定 → `quickstart.md` 手工验收
- 故事内全部任务完成后再进入下一优先级故事（或按团队容量并行）

### Parallel Opportunities

- Setup 阶段 T001、T002、T003 可并行
- Foundational 阶段 T005、T006、T007、T008、T009（数据类型定义）可并行；T012、T013 可与上述及彼此并行
- Foundational 完成后，US1/US2/US3 三个用户故事阶段可由不同开发者并行推进（各自新增文件互不冲突；共享文件 `SessionController.cpp`/`PasteInjector_x11.cpp`/`PasteInjector_mac.mm` 的协作需注意合并顺序）
- US1 内：T015-T019（测试）可并行；T020、T021、T024、T026-T035（多数模型/平台实现，彼此不同文件）可并行
- US2 内：T042、T043（测试）可并行
- US3 内：T048-T051（测试）可并行
- Polish 阶段 T059、T060、T062 可并行

---

## Parallel Example: User Story 1

```bash
# 并行编写 User Story 1 的全部测试：
Task: "FastCorrector 单元测试于 tests/fastpath/FastCorrectorTest.cpp"
Task: "PinyinTrie 单元测试于 tests/fastpath/PinyinTrieTest.cpp"
Task: "DebounceScheduler 单元测试于 tests/slowpath/DebounceSchedulerTest.cpp"
Task: "GlobalHotkey/HotkeyBinding 契约测试于 tests/hotkey/GlobalHotkeyTest.cpp"
Task: "SessionController confirm()/cancel() 状态迁移测试于 tests/session/SessionControllerTest.cpp"

# 并行实现各分平台模块（互不冲突的不同文件）：
Task: "Windows 全局热键绑定于 babel/src/hotkey/GlobalHotkey_win.cpp"
Task: "macOS 全局热键绑定于 babel/src/hotkey/GlobalHotkey_mac.mm"
Task: "Linux X11 全局热键绑定于 babel/src/hotkey/GlobalHotkey_x11.cpp"
Task: "macOS 模拟粘贴注入于 babel/src/inject/PasteInjector_mac.mm"
Task: "Linux X11 模拟粘贴注入于 babel/src/inject/PasteInjector_x11.cpp"
Task: "Windows 模拟粘贴注入于 babel/src/inject/PasteInjector_win.cpp"
```

---

## Implementation Strategy

### MVP First (仅 User Story 1)

1. 完成 Phase 1：Setup
2. 完成 Phase 2：Foundational（**关键阻塞点**——阻塞所有用户故事）
3. 完成 Phase 3：User Story 1
4. **停止并验证**：按 `quickstart.md` 验证场景一独立测试 User Story 1
5. 若满足验收标准即可作为 MVP 演示/部署

### Incremental Delivery

1. 完成 Setup + Foundational → 基础就绪
2. 新增 User Story 1 → 独立测试（`quickstart.md` 场景一）→ 可演示（MVP！）
3. 新增 User Story 2 → 独立测试（`quickstart.md` 场景二）→ 可演示
4. 新增 User Story 3 → 独立测试（`quickstart.md` 场景三）→ 可演示
5. 每个故事均在不破坏已交付故事的前提下新增价值

### Parallel Team Strategy

多开发者协作时：

1. 团队共同完成 Setup + Foundational
2. Foundational 完成后：
   - 开发者 A：User Story 1（核心闭环）
   - 开发者 B：User Story 2（手动粘贴降级，需协调与 A 共享 `PasteInjector_x11.cpp`/`SessionController.cpp` 的合并顺序）
   - 开发者 C：User Story 3（异常友好提示，需协调与 A/B 共享文件的合并顺序）
3. 建议 US2/US3 在 US1 对应共享文件（`SessionController.{h,cpp}`、`PasteInjector_x11.cpp`、`PasteInjector_mac.mm`）合入主干后再提交自身变更，以降低合并冲突

---

## Notes

- [P] 任务 = 不同文件、无依赖关系
- [Story] 标签将任务映射到具体用户故事，便于追溯
- 每个用户故事均应可独立完成与测试
- 实现前先确认测试处于 FAIL 状态
- 建议每完成一个任务或一组逻辑相关任务后提交一次
- 可在任意 Checkpoint 处停下独立验证该故事
- 避免：模糊任务描述、同文件冲突、破坏故事独立性的跨故事依赖
- FR-012 强约束贯穿 US1/US2/US3：无论 `InjectionAttemptResult.outcome` 取何值（除 `Succeeded`），用户可见文案 MUST 完全一致（"已复制到剪贴板，请手动粘贴"），任何任务的实现 MUST NOT 违反此约束
