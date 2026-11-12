# Research: macOS 原生输入法接入原型与 IPC 骨架

**Feature**: `008-macos-imk-stub-ipc` | **Date**: 2026-11-12

---

## 1. Swift IMKit 输入法 Bundle 架构与生命周期

### 1.1 核心机制与系统集成
macOS 的输入法体系基于 `InputMethodKit.framework`。输入法必须打包为一个符合特定规范的 macOS Application Bundle（`.app`），并安装于 `~/Library/Input Methods/` 或 `/Library/Input Methods/`。
系统的 `TextInputMenuAgent` 和 `loginwindow` 负责在用户切换输入源时加载或激活此 Bundle。

### 1.2 关键配置与 Info.plist 契约
输入法 Bundle 的 `Info.plist` 必须包含以下关键键值：
* `CFBundlePackageType`: `APPL`
* `LSUIElement`: `true`（使应用作为后台常驻输入法运行，不显示在 Dock 栏与强制切换窗口中）
* `InputMethodConnectionName`: 唯一连接标识符（例如 `Babel_1_Connection_Name`），必须与 Swift 中 `IMKServer` 实例初始化时传入的名称完全一致。
* `InputMethodServerFilterStyles`: 设置为 `{ "All": "NO" }`。
* `tsInputMethodCharacterRepertoireKey`: `[ "Latn", "Hans" ]`，声明支持英文字符集与简体中文字符集。
* `ComponentInputModeDict`: 声明主输入模式，绑定对应的 Script 模式。

### 1.3 控制器与按键分发（Swift 6）
* **`IMKServer`**：负责监听系统的连接请求，关联 `Info.plist` 中的 `InputMethodConnectionName`。
* **`IMKInputController`**：
  * 重写 `- (BOOL)handleEvent:(NSEvent *)event client:(id)sender;`：
    * 捕获 `NSEvent.EventType.keyDown`；
    * 判定按键是否被输入法消费：若判定由输入法处理（如组合键中未被占用的字母、回车、空格等），返回 `true`；若为透传按键（如未处于编辑状态下的功能键或快捷键），返回 `false` 交给宿主应用原生处理；
  * 调用 `[client insertText:replacementRange:]`：原子化上屏最终文本，无需剪贴板中转，无需辅助功能权限。

---

## 2. 跨进程通信（IPC）方案比对与选型

### 2.1 候选方案比对

| 方案 | 往返延迟 (RTT) | 跨语言支持 (Swift & C++) | 沙盒与权限依赖 | 调试复杂度 | 结论 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Unix Domain Socket (UDS)** | **20 ~ 80 µs** | **原生极简** (Swift Network/POSIX + C++ ASIO/POSIX) | **无需特殊权利** (本地文件套接字) | **极低** (可用 `nc -U` 调试) | **选用** |
| **Apple XPC** | 100 ~ 250 µs | 复杂 (C++ 包装非原生) | 强依赖 App Groups / Codesign | 较高 (依赖系统控制) | 排除 |
| **Mach Message / Port** | 10 ~ 30 µs | 极其底层，无跨平台抽象 | 权限严密，极易引发内核端口泄露 | 极高 | 排除 |
| **共享内存 + 环形缓冲区** | < 10 µs | 良好 | 需同步通知原语（仍需信号/Socket唤醒） | 高 | 留待后续极致优化 |

### 2.2 协议设计决策
* **传输介质**：本地套接字，文件路径约定为 `~/.local/run/babel.sock`。
* **分包格式**：`[4字节大端长度前缀] + [UTF-8 JSON Payload]`。
  * 理由：在击键载荷极小（< 200 bytes）的情况下，JSON 解析开销在现代 CPU 上耗时 $< 20\text{µs}$，相比二进制 Protobuf，无需生成语言绑定，同时具有极高可读性与调试诊断便利性。

---

## 3. 本地模型存储路径迁移（`~/.local/models`）

### 3.1 路径解析重构
* **历史实现**：`ModelPaths::conventionalModelDirectory()` 使用 `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) / "models"`，解析到 `~/Library/Application Support/Babel/models`。
* **新设计**：
  * 用户家目录通过 `$HOME`（Windows 下 `%USERPROFILE%`）展开，标准目录定义为 `$HOME/.local/models`。
  * 默认模型文件名保持为 `qwen2.5-3b-instruct-q4_k_m.gguf`。
* **优先级决策**：
  1. 优先读取环境变量 `BABEL_MODEL_PATH`（若存在且非空，直接作为模型文件绝对路径）；
  2. 其次读取默认约定路径 `$HOME/.local/models/qwen2.5-3b-instruct-q4_k_m.gguf`；
  3. 兼容回退：若上述均不存在但历史目录中存在，则回退兼容并输出警告提示用户迁移。
* **测试隔离保障**：
  * 既有单元测试依赖 `QCoreApplication::setApplicationName()` 隔离。新方案中，测试用例通过设置环境变量 `BABEL_MODEL_PATH` 或设置测试临时目录环境变量进行强隔离，避免对用户物理环境造成副作用。

---

## 4. 全链路关键事件结构化日志方案

### 4.1 Swift 前端日志：Apple `os.Logger`
* **Subsystem**: `com.babel.inputmethod`
* **Categories**:
  * `lifecycle`: 输入法服务创建、激活（`activateServer`）、注销（`deactivateServer`）。
  * `event`: 按键码（keyCode）、修饰键（modifiers）、消费判定（consume / pass-through）。
  * `ipc`: UDS 连接成功/断开重试、请求序列号、响应耗时（RTT）。
  * `commit`: 文本上屏字符数、调用耗时。
* **排查方式**：无需附加组件，直接在终端执行 `log stream --predicate 'subsystem == "com.babel.inputmethod"'` 或通过 macOS 自带的 `Console.app` 实时筛选。

### 4.2 C++ 后端日志：`spdlog`
* 现有 CMakeLists.txt 已链接 `spdlog::spdlog`。
* **输出目标**：
  1. 控制台标准输出（带有彩色日志级别，便于命令行调试）；
  2. 循环文件日志（`~/.local/state/babel/daemon.log`，单文件上限 10MB，保留 3 个历史备份）。
* **记录内容**：
  * IPC 客户端接入/断开；
  * 按键派发与快流 Trie 检索耗时；
  * 慢流防抖计时器状态；
  * 模型加载事件与内存占用。

---

## 5. Qt 独立浮窗模式与 Daemon 模式的共存

### 5.1 启动参数设计
* 在 `babel/main.cpp` 中通过命令行参数解析：
  * `--daemon`: 启动无图形界面的后台守护进程，初始化快流/慢流引擎并启动 `UnixDomainSocketServer`；
  * `--standalone`（或无参数）：保持现有行为，启动完整的 Qt QML 浮窗界面，注册全局热键（`Control+Option+Space`），使用现有 `PasteInjector_mac.mm`。
* **收益**：算法开发与 UI 调整不需要每次重装系统输入法，可在 IDE 中直接以独立应用形式运行并断点调试。

