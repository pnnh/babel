# Feature Specification: macOS 原生输入法接入原型与 IPC 骨架

**Feature Branch**: `008-macos-imk-stub-ipc`

**Created**: 2026-11-12

**Status**: Draft

**Input**: User description: "把混合输入法改造成真正的macOS系统级输入法第一阶段（Step 1）：使用Swift开发原生IMKit Stub，建立Unix Domain Socket低延迟IPC通信，统一模型路径至~/.local/models，补全前后端关键事件日志，并保留既有Qt浮窗模式用于开发调试"

**语言（宪章《语言与文档规范》）**：用户故事、验收场景、功能需求、成功标准等说明性文字 MUST 使用简体中文；类名、路径、协议等技术标识可保留英文。

## 背景与演进定位

本项目在 `specs/001` ~ `007` 中基于 Qt 6 / C++20 与 `llama.cpp` 交付了 Phase 2 桌面浮窗输入原型。该形态依赖全局热键呼出浮窗、并在确认后通过 `PasteInjector_mac.mm`（写入剪贴板 + `CGEventPost` 模拟 `Cmd+V`）进行文本注入，存在依赖系统辅助功能（Accessibility）权限、污染剪贴板、无法随时无感打字等局限。

本特性作为《macOS 原生混合输入法演进方案》（`docs/plan/macos-ime-evolution-plan.md`）的 **Phase 1（Step 1）**，旨在完成从独立桌面应用到 macOS 系统级输入法的基础骨架搭建：
1. **Swift 原生 IMKit 输入法插件**：利用现代 Swift 开发轻量级输入法 Bundle（`~/Library/Input Methods/`），实现系统级输入法注册、按键拦截与 `[client insertText:]` 原生无感上屏。
2. **前后端低延迟 IPC 管道**：基于 Unix Domain Socket 建立 Swift 前端与 C++ 守护进程之间的通信管道。
3. **模型路径迁移规范化**：将本地大模型约定目录统一迁移至 `~/.local/models`，消除存储冗余。
4. **全链路关键事件可观测性**：引入 Swift `os.Logger` 与 C++ 守护进程结构化日志。
5. **开发期双模式保留**：继续保留现有 Qt 浮窗可执行程序（Standalone 模式），确保 IDE 内快速联调与算法验证不受干扰。

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - 原生输入法注册、按键拦截与原子化上屏 (Priority: P1)

用户将构建生成的输入法应用放入 `~/Library/Input Methods/` 并在系统设置中启用后，在任意文本编辑区（如 TextEdit、备忘录、终端）切换到 Babel 输入法，能够正常拦截按键流，并在完成输入时通过系统原生 API 直接上屏文本，无需辅助功能权限，不改变剪贴板内容。

**Why this priority**: 这是脱离“快捷键浮窗 + 模拟粘贴”形态、成为真正系统输入法的最核心地基。

**Independent Test**: 用户在系统输入源中选择 Babel，打开 TextEdit 键入字母，触发输入并在确认后直接在光标处插入文本，检查剪贴板未被修改，且系统辅助功能列表中无 Babel 授权依赖。

**Acceptance Scenarios**:
1. **Given** 用户已将 `BabelInputMethod.app` 安装至 `~/Library/Input Methods/`，**When** 用户在系统“键盘 -> 输入源”中添加并切换至 Babel，**Then** 系统菜单栏输入法图标正确显示 Babel 图标。
2. **Given** Babel 输入法处于激活状态，**When** 用户在文本框中键入字母并按特定触发键（如回车/空格），**Then** 文本直接调用 `[client insertText:replacementRange:]` 原子化上屏到光标位置。
3. **Given** 文本成功上屏，**When** 用户检查系统剪贴板（如通过 `pbpaste` 或粘贴板历史），**Then** 剪贴板原内容保持不变。

---

### User Story 2 - Swift 前端与 C++ 守护进程的低延迟 IPC 通信 (Priority: P2)

Swift 编写的 IMKit 前端进程与后台常驻的 C++ 守护进程（Daemon）通过本地套接字建立通信。前端将拦截到的原始击键事件传递给后端，后端执行逻辑处理（快流/会话状态更新）后将响应返回给前端进行处理或呈现。

**Why this priority**: 满足 macOS 硬实时按键响应要求与大模型/词库分离的架构需求，保证输入法主进程轻量且不卡顿。

**Independent Test**: 后台启动 C++ 守护进程，Swift 前端连接套接字并发送按键包，测量 IPC 往返耗时（RTT）低于 1ms，且在后端未启动或断开连接时，前端自动降级为安全直通模式（Pass-through）。

**Acceptance Scenarios**:
1. **Given** 后端守护进程已启动监听，**When** Swift 前端初始化激活，**Then** 双方自动完成握手建立会话。
2. **Given** 前端拦截到按键，**When** 发送击键事件至 IPC 管道，**Then** 后端在 1ms 内返回处理决策或候选数据。
3. **Given** 后端守护进程意外退出或未启动，**When** 用户继续键盘打字，**Then** 前端 `handleEvent:client:` 返回 `NO`，将所有按键无缝透传给宿主应用，键盘不卡顿、不丢键。

---

### User Story 3 - 本地模型路径迁移至 `~/.local/models` (Priority: P3)

应用加载本地大模型（如 `qwen2.5-3b-instruct-q4_k_m.gguf`）时，从标准目录 `~/.local/models` 读取，避免占用 `~/Library/Application Support/Babel/models` 造成多份存储冗余。

**Why this priority**: 满足用户明确的存储规范要求，方便与其他本地 AI 工具共用模型目录。

**Independent Test**: 将模型文件放置在 `~/.local/models/` 下，启动守护进程，检查模型成功加载并记录对应路径。

**Acceptance Scenarios**:
1. **Given** 用户在 `~/.local/models/` 放置了合法的 GGUF 模型，**When** 守护进程初始化 `ModelPaths::conventionalModelDirectory()`，**Then** 解析为 `$HOME/.local/models` 并成功识别该模型。
2. **Given** 用户设置了环境变量 `BABEL_MODEL_PATH=/custom/path/model.gguf`，**When** 系统加载模型，**Then** 优先使用环境变量指定的自定义路径。
3. **Given** `~/.local/models/` 目录中无模型文件，**When** 守护进程启动，**Then** 给出清晰日志提示与引导信息，不发生异常崩溃。

---

### User Story 4 - 前后端关键事件结构化日志追踪 (Priority: P4)

为 Swift 输入法前端和 C++ 守护进程引入统一的关键事件日志体系，方便开发者与用户排查输入法状态机流转、按键丢弃、IPC 连接与模型加载故障。

**Why this priority**: 输入法与系统紧密集成，缺乏界面时日志是定位按键延迟、IPC 故障与权限问题的唯一手段。

**Independent Test**: 打开系统 Console.app，过滤 `subsystem: com.babel.inputmethod`，输入法击键与生命周期日志实时可见；同时查看 `~/.local/state/babel/daemon.log` 包含后端按键接收与快流调度记录。

**Acceptance Scenarios**:
1. **Given** 用户在宿主应用中切换输入法，**When** 触发激活与休眠，**Then** Swift 前端通过 `os.Logger` 记录输入源事件与当前客户端 bundle identifier。
2. **Given** 发生按键事件，**When** 前端完成判定，**Then** 日志记录按键键码、消费决策（Consume/Pass-through）及 IPC 耗时。
3. **Given** 守护进程运行中，**When** 接收请求或执行快流/慢流处理，**Then** 结构化记录处理耗时与错误详情。

---

### User Story 5 - 保留现有 Qt 独立浮窗模式便于开发调试 (Priority: P5)

开发者在日常修改词典算法、调优 UI 动效时，无需每次重新编译并安装输入法 Bundle，可以直接运行 Qt 独立应用（通过 `--standalone` 启动或默认配置），保留全局热键唤起浮窗的完整交互。

**Why this priority**: 保护既有开发调试效率，避免频繁重启系统输入法服务。

**Independent Test**: 运行 `./build/bin/babel --standalone`，按 `Control+Option+Space`，现有 Qt QML 浮窗正常弹出并可用。

**Acceptance Scenarios**:
1. **Given** 运行编译生成的 `babel` 可执行程序且带 `--standalone` 参数，**When** 按下 `Control+Option+Space`，**Then** 原有 QML 浮窗正常唤起，支持打字与预览。
2. **Given** 运行 `babel --daemon`，**When** 启动时检测到守护进程参数，**Then** 以无头后台服务形式运行并监听 IPC 套接字。

---

### Edge Cases

- **安全输入模式（Secure Input）**：当焦点处于密码输入框时（系统开启安全事件输入），Swift 前端必须检测并自动进入直通英文模式，不得将按键送入 IPC，避免系统安全阻断。
- **IPC 异常断开重连**：当后端守护进程在输入过程中重启，Swift 前端应优雅处理断开并在下一次击键时尝试重新连接，断连期间按键保持直通不卡手。
- **路径中包含波浪号与环境变量**：路径解析函数必须正确将 `~` 展开为用户实际的 `$HOME` 物理路径。

---

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: 必须创建独立的 Swift 输入法 Bundle（`BabelInputMethod.app`），配置合法的 `Info.plist`（包含 `InputMethodConnectionName`, `tsInputMethodCharacterRepertoireKey`, `InputMethodServerFilterStyles`, `LSUIElement = true`）。
- **FR-002**: Swift 前端必须实现 `IMKServer` 与继承 `IMKInputController`，并在 `handleEvent:client:` 中截获按键事件。
- **FR-003**: Swift 前端必须支持调用 `[client insertText:replacementRange:]` 将文本直接注入当前聚焦的应用。
- **FR-004**: 系统必须建立基于 Unix Domain Socket 的 IPC 传输协议，通信格式采用紧凑型二进制或轻量 JSON。
- **FR-005**: C++ 守护进程必须支持 `--daemon` 命令行参数，启动后在固定路径（如 `~/.local/run/babel.sock`）监听前端连接。
- **FR-006**: `ModelPaths::conventionalModelDirectory()` 必须解析为 `$HOME/.local/models`，模型文件默认为 `qwen2.5-3b-instruct-q4_k_m.gguf`。
- **FR-007**: 系统必须优先支持通过 `BABEL_MODEL_PATH` 环境变量指定模型绝对路径。
- **FR-008**: Swift 前端必须使用 Apple `os.Logger`（Subsystem: `com.babel.inputmethod`）记录生命周期、按键决策、IPC 通信与提交事件。
- **FR-009**: C++ 守护进程必须支持结构化日志输出至控制台与日志文件（默认存放于 `~/.local/state/babel/daemon.log`）。
- **FR-010**: C++ 应用必须保留 `--standalone` 启动模式，完整运行现有 Qt 6 / QML 浮窗界面与热键逻辑。
- **FR-011**: 当 IPC 连接不可用时，Swift 前端必须将击键自动回退为直通模式（返回 `NO`），严禁阻塞主线程。

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: `BabelInputMethod.app` 安装至 `~/Library/Input Methods/` 后，macOS 系统“键盘 -> 输入源”可成功检测并添加该输入法。
- **SC-002**: 在系统原生应用（如 TextEdit）中，使用 Babel 输入法按特定触发键完成测试文本上屏，剪贴板内容零污染。
- **SC-003**: Swift 前端与 C++ 守护进程单次 IPC 按键转发与确认往返耗时（RTT）中位数小于 1ms。
- **SC-004**: 模型路径成功指向 `~/.local/models/qwen2.5-3b-instruct-q4_k_m.gguf`，且相关单元测试全部通过。
- **SC-005**: 在 Console.app 中可通过 `subsystem:com.babel.inputmethod` 实时筛选观测输入法生命周期与关键事件。
- **SC-006**: 独立运行 `./babel --standalone`，现有浮窗与全局热键功能完全可用，无回归退化。

---

## Assumptions

- 开发者在 macOS 上拥有安装输入法到 `~/Library/Input Methods/` 的用户权限。
- 目标开发机安装有 CMake 3.25+、Xcode 命令行工具或完整 Xcode（支持 Swift 5.10 / Swift 6）。
- 用户的常用模型文件已放置或将放置于 `~/.local/models/` 目录下。
- 本特性专注于输入法接入、IPC 与基础设施搭建，完整的拼音候选窗与多候选翻页逻辑留待 Phase 2 实现。

