---
description: "Task list for 008-macos-imk-stub-ipc"
---

# Tasks: macOS 原生输入法接入原型与 IPC 骨架

**Input**: Design documents from `/specs/008-macos-imk-stub-ipc/`

**Prerequisites**: `plan.md`（Constitution Check PASS）、`spec.md`、`research.md`、`data-model.md`、`contracts/`、`quickstart.md` 全部就绪。

**语言（宪章《语言与文档规范》）**：任务描述使用简体中文；文件路径、符号名、命令保留英文。

---

## 任务清单结构总览

- **Phase 1: Setup & Baseline**（基线验证）
- **Phase 2: Foundational - 模型存储路径迁移至 `~/.local/models` (User Story 3)** 🎯
- **Phase 3: C++ 后端守护进程与 IPC 通信服务 (User Story 2 & User Story 5)**
- **Phase 4: Swift 原生 IMKit 输入法 Bundle (User Story 1)** 🎯 MVP
- **Phase 5: 全链路关键事件结构化日志追踪 (User Story 4)**
- **Phase 6: Polish & Verification**

---

## Phase 1: Setup & Baseline（基线确认）

**Purpose**: 在实施改动前运行既有测试套件，确认当前仓库处于完全可编译、测试全通过的基线状态。

- [ ] T001 运行既有测试套件建立变更前基线：执行 `ctest --test-dir build --output-on-failure`（若无 build 目录则先 `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`），确认既有测试全部通过并记录基线。

**Checkpoint**: 基线确认无误，进入 Phase 2。

---

## Phase 2: Foundational - 模型存储路径迁移至 `~/.local/models` (User Story 3)

**Goal**: 将大模型加载目录从 `~/Library/Application Support/Babel/models` 迁移至 `$HOME/.local/models`，并支持 `BABEL_MODEL_PATH` 环境变量优先覆盖（对应 FR-006、FR-007 与 contracts/model-path-resolution-contract.md）。

**Independent Test**: 将合法模型放置于 `~/.local/models/` 或通过 `BABEL_MODEL_PATH` 指定，验证 `ModelPaths::conventionalModelDirectory()` 解析正确且现有测试全绿。

- [X] T002 重构 `babel/src/slowpath/ModelPaths.h` 与 `babel/src/slowpath/ModelPaths.cpp`：
  - 更新 `conventionalModelDirectory()`：通过展开环境变量 `$HOME`（Windows 下 `%USERPROFILE%`）解析至 `.local/models`；
  - 新增 `conventionalModelFilePath()` 解析至 `$HOME/.local/models/qwen2.5-3b-instruct-q4_k_m.gguf`；
  - 新增 `resolveModelFilePath()` 支持优先级：① `BABEL_MODEL_PATH` 环境变量；② 默认标准目录；③ 历史 AppData 目录回退兼容（输出警告提示）。
- [X] T003 [P] 更新模型路径相关测试：
  - 在 `tests/slowpath/ModelCatalogTest.cpp` 与 `tests/slowpath/LlmEngineGenerateTest.cpp` 中适配新路径逻辑，确保隔离测试使用临时目录环境变量而不污染真实 `$HOME/.local/models`；
  - 新增 `tests/slowpath/ModelPathsTest.cpp`，覆盖环境变量覆盖、默认路径展开与波浪号安全解析场景。

**Checkpoint**: 模型路径迁移完成，运行 `ctest -R Model` 全部通过。

---

## Phase 3: C++ 后端守护进程与 IPC 通信服务 (User Story 2 & User Story 5)

**Goal**: 实现基于 Unix Domain Socket 的 IPC 服务端，在 `$HOME/.local/run/babel.sock` 监听前端击键请求；改造 `babel/main.cpp` 支持 `--daemon` 与 `--standalone` 启动分流（对应 FR-004、FR-005、FR-010 与 contracts/ipc-protocol-contract.md）。

**Independent Test**: 启动 `./babel --daemon`，通过测试客户端连接本地套接字发送 `HELLO` 与 `KEY_DOWN` 报文，测量往返耗时 $< 1\text{ms}$；运行 `./babel --standalone` 验证既有 Qt 浮窗回归正常。

- [X] T004 [P] 创建 IPC 报文定义与分帧助手：
  - 新建 `babel/src/ipc/IpcMessage.h` 与 `IpcMessage.cpp`，定义报文类型（`HELLO`、`KEY_DOWN`、`RESET`、`COMMIT`）；
  - 实现 4 字节网络大端序长度前缀的编解码与 JSON 载荷解析。
- [X] T005 实现 Unix Domain Socket 服务端：
  - 新建 `babel/src/ipc/UnixDomainSocketServer.h` 与 `UnixDomainSocketServer.cpp`；
  - 启动并在 `$HOME/.local/run/babel.sock` 上监听（设置权限 0700），接收击键事件并分发给核心处理层，返回 `KeyDownResponse`。
- [X] T006 [P] 编写 IPC 服务端单元测试：
  - 新建 `tests/ipc/IpcProtocolTest.cpp`，测试套接字创建、握手响应、击键处理与客户端非正常断开时的优雅恢复。
- [X] T007 改造 `babel/main.cpp` 启动入口：
  - 增加命令行参数解析：
    - `--daemon`: 启动无图形界面的后台守护进程，初始化 `UnixDomainSocketServer`；
    - `--standalone`（或无参数）：保持现有行为，启动完整的 Qt QML 浮窗应用与全局热键注册。

**Checkpoint**: 后端 IPC 服务端与启动分流完成，`ctest -R Ipc` 通过。

---

## Phase 4: Swift 原生 IMKit 输入法 Bundle (User Story 1) 🎯 MVP

**Goal**: 利用 Swift 6 编写独立的 `BabelInputMethod.app`，接入 macOS `InputMethodKit` 框架，拦截按键并通过 UDS 与后端守护进程通信，调用 `[client insertText:]` 原子化上屏（对应 FR-001、FR-002、FR-003、FR-011）。

**Independent Test**: 将 `BabelInputMethod.app` 安装至 `~/Library/Input Methods/`，在系统设置中添加并切换至 Babel 输入法，在 TextEdit 中输入并上屏文本，验证剪贴板零修改且无需辅助功能权限。

- [X] T008 [P] 搭建 Swift 输入法 Bundle 结构与配置文件：
  - 新建目录 `macos/BabelInputMethod/`；
  - 编写 `macos/BabelInputMethod/Info.plist`，配置 `InputMethodConnectionName`（如 `Babel_1_Connection_Name`）、`LSUIElement = true`、`tsInputMethodCharacterRepertoireKey`（`Latn`, `Hans`）等必要属性。
- [X] T009 编写 Swift UDS IPC 客户端：
  - 新建 `macos/BabelInputMethod/IpcClient.swift`；
  - 使用 POSIX socket 实现针对 `$HOME/.local/run/babel.sock` 的连接、定长分帧发送与超时读取；
  - 当守护进程不可达时，立即返回断连状态，进入直通降级模式。
- [X] T010 编写 Swift 输入法控制器（`BabelInputController.swift`）：
  - 继承 `IMKInputController`，重写 `handleEvent:client:`；
  - 拦截 `keyDown` 事件，调用 `IpcClient` 转发按键；若后端返回 `consumed == true` 则消费按键，否则返回 `false` 透传给宿主；
  - 在收到上屏指令时调用 `[client insertText:replacementRange:]` 直落光标位置。
- [X] T011 [P] 编写输入法主入口与服务声明：
  - 新建 `macos/BabelInputMethod/BabelServer.swift` 与 `macos/BabelInputMethod/main.swift`；
  - 实例化 `IMKServer` 绑定 `InputMethodConnectionName`，启动 NSApplication 事件循环。
- [X] T012 配置 CMake 构建规则与安装脚本：
  - 在 `babel/CMakeLists.txt` 或根目录 `CMakeLists.txt` 中添加对 `BabelInputMethod` 的编译规则（利用 CMake 的 Swift 支持生成 `.app` Bundle）；
  - 提供 `install_imk` 构建目标或 shell 脚本，支持一键将应用部署到 `~/Library/Input Methods/` 并刷新系统缓存。

**Checkpoint**: 可以在 TextEdit 中切换到 Babel 输入法并完成文本拦截与插入。

---

## Phase 5: 全链路关键事件结构化日志追踪 (User Story 4)

**Goal**: 贯通前端 Swift `os.Logger` 与后端 C++ `spdlog` 结构化文件日志，实现全链路可观测性（对应 FR-008、FR-009 与 contracts/logging-observability-contract.md）。

**Independent Test**: 打开 Console.app 过滤 `subsystem:com.babel.inputmethod` 实时查阅前端日志；检查 `$HOME/.local/state/babel/daemon.log` 包含后端按键接收与快流调度记录。

- [X] T013 [P] 封装 Swift 端统一日志：
  - 新建 `macos/BabelInputMethod/Logger.swift`，基于 `os.Logger(subsystem: "com.babel.inputmethod", category: ...)` 划分 `lifecycle`、`event`、`ipc`、`commit`；
  - 在 `BabelInputController` 与 `IpcClient` 关键分支埋点（激活/失活、按键判定、IPC 延迟、上屏字符）。
- [X] T014 封装 C++ 守护进程结构化日志：
  - 新建 `babel/src/logging/DaemonLogger.h` 与 `DaemonLogger.cpp`；
  - 配置 `spdlog` 同时输出至控制台与 `$HOME/.local/state/babel/daemon.log`（按 10MB 自动轮转，保留 3 份历史）；
  - 在 `UnixDomainSocketServer` 与模型加载逻辑中接入日志埋点。

**Checkpoint**: 前后端关键事件日志可在 Console.app 和文件中清晰检索。

---

## Phase 6: Polish & Verification

**Purpose**: 依据 `quickstart.md` 验证场景执行全链路验收测试与回归验证。

- [ ] T015 运行全量自动化测试套件：`ctest --test-dir build --output-on-failure`，确保新增的模型路径测试、IPC 单元测试及既有 100+ 测试全部全绿 PASS。
- [ ] T016 依照 `specs/008-macos-imk-stub-ipc/quickstart.md` 执行端到端实机验证：
  - 场景 1：`./build/bin/babel --standalone` 验证 Qt 浮窗模式回归；
  - 场景 2：`./build/bin/babel --daemon` 验证后台无头守护进程正常加载模型与监听 IPC；
  - 场景 3：安装 `BabelInputMethod.app` 到 `~/Library/Input Methods/`，在 TextEdit 中验证打字上屏；
  - 场景 4：通过 `log stream` 与 `daemon.log` 核验关键日志输出。

---

## Dependencies & Execution Flow

```text
T001 (基线验证)
  │
  ▼
Phase 2: 模型路径迁移 (T002, T003) ──► 确保 ~/.local/models 统一存储
  │
  ▼
Phase 3: C++ 后端 IPC 服务 (T004, T005, T006, T007) ──► 守护进程与启动分流就绪
  │
  ▼
Phase 4: Swift IMKit Bundle (T008, T009, T010, T011, T012) ──► 原生输入法接入与直接上屏
  │
  ▼
Phase 5: 全链路日志 (T013, T014) ──► 增强排错可观测性
  │
  ▼
Phase 6: 全链路验收与回归 (T015, T016)
```
