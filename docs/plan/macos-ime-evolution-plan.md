# macOS 原生混合输入法演进方案与实施规划

---

## 1. 概述与核心目标

本项目当前阶段已基于 Qt 6 / C++20 与 `llama.cpp` 交付了 Phase 2 桌面浮窗原型（`specs/001` ~ `007`）。该形态通过全局热键（`Control+Option+Space`）唤起无边框浮窗并在确认后通过 `CGEventPost` 模拟粘贴。

本演进方案旨在将应用重构并升级为**真正的 macOS 系统级输入法**，达成以下目标：
1. **基础输入法能力**：随时随地拦截键盘输入，支持拼音单字、词组、常规句子的即时选字上屏。
2. **AI 混合输入纠偏**：输入中英混杂、Chinglish 或粗糙英文长句时，触发双流管线，输出经本地大模型消歧与润色后的地道中文句子。
3. **原生无感上屏**：彻底摒弃剪贴板中转与 `CGEventPost` 模拟粘贴，利用 `IMKInputController` 原生 `[client insertText:]` API 零延迟直落目标窗口。
4. **开发期平滑过渡**：保留现有的 Qt 独立浮窗模式（Standalone Mode），供开发调试核心算法与双流交互。
5. **存储优化与可观测性**：模型文件路径收敛至 `~/.local/models` 避免冗余占用；建立贯通输入法前端（Swift）与后端（C++）的结构化日志体系。

---

## 2. 关键决策与技术选型

### 2.1 开发语言选型
* **输入法前端（IMKit Bundle）**：**使用现代 Swift（Swift 6 / 5.10+）**。
  * macOS `InputMethodKit` 完全支持 Swift。使用 Swift 编写 `IMKServer`、`IMKInputController` 与原生候选浮窗（AppKit `NSPanel`），语法安全且无历时遗留的技术包袱。
* **核心后端与既有代码**：**保留 C++20 与既有 Obj-C++**。
  * `FastCorrector`（Trie 树、Lattice、编辑距离矩阵）与 `LlmEngine`（llama.cpp）继续采用高性能 C++20；已有的 `GlobalHotkey_mac.mm`、`PasteInjector_mac.mm` 保留在独立模式中使用，不废弃已有资产。

### 2.2 进程架构：Frontend Stub + Core Daemon 分离架构
为满足 macOS 对输入法的硬实时约束及内存安全要求，采用 C/S 分离架构：
```text
┌────────────────────────────────────────────────────────┐
│                   宿主应用 (WeChat, Safari, VS Code)    │
│                                                        │
│  [client insertText:] ◄─────┐  ▲ [attributesFor...]   │
│  handleEvent: ──────┐       │  │ (光标绝对坐标)         │
└─────────────────────┼───────┼──┼───────────────────────┘
                      ▼       │  │
┌────────────────────────────────────────────────────────┐
│  前端轻量级输入法宿主 (BabelInputMethod.app)            │
│  - 语言: 纯 Swift                                      │
│  - 职责: 拦截击键、维护 Marked Text、原生候选窗/光标跟随 │
│  - 内存占用: < 30 MB (无 Metal / 无 LLM 模型常驻)       │
└─────────────────────┬───────▲──────────────────────────┘
                      │       │ 低延迟 IPC (<1ms)
                      │       │ (Unix Domain Socket)
                      ▼       │
┌────────────────────────────────────────────────────────┐
│  后端守护进程 (babel_daemon / Core Service)             │
│  - 语言: C++20 (基于现有 babel_core 模块)               │
│  - 模块: FastCorrector, PinyinLattice, LlmEngine       │
│  - 模型存储: ~/.local/models                           │
└────────────────────────────────────────────────────────┘
```

* **分离的核心理由**：
  1. **按键实时性保证**：macOS IMKit 的 `handleEvent:client:` 在主 RunLoop 同步执行。将耗时的词库解析与 LLM 推理隔绝在守护进程的工作线程中，输入法前端能在微秒级决定消费或透传，彻底避免丢键与彩虹轮。
  2. **避免内存被系统杀掉**：LLM 模型（Qwen2.5-3B 等）占用 2GB~4GB 显存/内存。若直接集成进 IMKit Bundle，在系统内存压力下会被 OS 优先强杀，引发系统输入卡死；分离后守护进程崩溃不会拉垮输入法主进程。
  3. **沙盒与路径隔离**：`~/.local/models` 的大文件读取与 Metal 加速均由用户空间的守护进程完成，IMKit 前端无需申请穿透沙盒的复杂权限。

### 2.3 开发期模式保留（Qt 浮窗保留）
* 原有的 Qt 6 / QML 独立可执行程序（`babel`）继续保留。
* 通过启动参数区分模式：
  * `babel --standalone`（或默认无参数）：启动原有 Qt 浮窗界面与全局热键，供日常开发算法、界面动效快速验证。
  * `babel --daemon`：作为后台无头守护进程（Daemon）运行，监听 IPC 套接字，服务于 Swift 原生输入法。

### 2.4 模型统一路径规范
* 将既有硬编码或依赖 Qt `AppDataLocation`（`~/Library/Application Support/Babel/models`）的模型加载路径，统一重构为：
  * **主路径**：`~/.local/models`（对应全路径 `$HOME/.local/models`）。
  * **兼容优先级**：
    1. 显式配置/环境变量 `BABEL_MODEL_PATH`（若存在）；
    2. 用户统一目录 `$HOME/.local/models/qwen2.5-3b-instruct-q4_k_m.gguf`；
    3. 既有旧目录 `$HOME/Library/Application Support/Babel/models/...`（作为向上兼容的回退）。

### 2.5 关键事件日志与排错体系
* **前端（Swift IMKit）**：
  * 采用 Apple 官方 `os.Logger`（Subsystem: `com.babel.inputmethod`）。
  * 关键日志点：
    * `lifecycle`：输入法激活（`activateServer`）、休眠（`deactivateServer`）。
    * `key_event`：按键捕获、键码、消费（`YES`）还是透传（`NO`）判定。
    * `ipc`：连接 Daemon 状态、请求发出、响应接收、往返耗时（RTT）。
    * `ui`：宿主光标屏幕矩形位置计算、候选窗展示与隐藏。
    * `commit`：最终上屏文本内容与耗时。
* **后端（C++ Daemon）**：
  * 结构化日志输出（支持控制台 + 本地日志文件 `~/.local/state/babel/daemon.log`）。
  * 关键日志点：
    * IPC 客户端连接接入与断开。
    * 快流（FastPath）前缀查询与 Lattice 构建耗时。
    * 慢流（SlowPath）防抖调度触发、LLM 推理首字延迟（TTFT）与总耗时。
    * 安全输入检测状态。

---

## 3. 分阶段实施演进路线 (Roadmap)

### Phase 1: Swift IMKit 极简 Stub + IPC 通信骨架（当前 Step 1）
* **目标**：打通系统级输入法接入全链路，验证系统注册、击键拦截与 `[client insertText:]` 原生上屏。
* **核心交付**：
  1. Swift 编写的 `BabelInputMethod.app`，安装在 `~/Library/Input Methods/`，系统“键盘 -> 输入源”可成功添加与激活。
  2. 实现基础 Unix Domain Socket IPC 通信机制（Swift 与 C++ 通信）。
  3. 完成 `ModelPaths` 迁移至 `~/.local/models`。
  4. 落地 Swift `os.Logger` 与 C++ 结构化日志。
  5. 保留现有 Qt 浮窗模式不受影响。

### Phase 2: 基础拼音候选与光标跟随原生 Panel
* **目标**：满足日常普通拼音输入法使用需求（单字、词组）。
* **核心交付**：
  1. 原生 AppKit `NSPanel` 候选浮窗，获取 `[client attributesForCharacterIndex:lineHeightRectangle:]` 光标绝对坐标并贴合跟随。
  2. 完整的拼音切分与候选状态机：数字键 1~9 选词、空格确认首选、翻页键（`- =`）。
  3. 宿主输入框内 Marked Text（带下划线预编辑字符串）渲染与退格编辑。

### Phase 3: 双流融合与 AI 整句落字
* **目标**：将中英混杂小模型推理融入原生输入体验。
* **核心交付**：
  1. 混杂检测与慢流异步调度（长拼音、英文单词、空格分隔自动唤醒 llama.cpp）。
  2. 原生候选窗中加入 Ghost Text 实时整句预览区域与学习提示气泡。
  3. 回车键原子化触发标准中文整句直落上屏。

### Phase 4: 安全模式、边界加固与打包分发
* **目标**：生产级发布准备。
* **核心交付**：
  1. 密码框 `SecureEventInput` 监听与纯英文透传降级。
  2. 签名、Notarization 公证与安装引导打包脚本。

