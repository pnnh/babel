# Contract: 前后端结构化日志契约

**适用范围**: Swift 输入法前端与 C++ 后端日志记录。

---

## 1. Swift 前端日志契约（Apple `os.Logger`）

* **Subsystem**: `com.babel.inputmethod`
* **日志分类与级别标准**:
  * `lifecycle` (Info/Notice):
    * `[activateServer]` 当前输入法激活，记录客户端 Bundle ID。
    * `[deactivateServer]` 当前输入法失活。
  * `event` (Debug/Info):
    * `[keyDown]` 记录 `keyCode`、`modifiers`、判定结果 `consumed: true/false`。
  * `ipc` (Debug/Error):
    * `[ipc_connect]` UDS 连接成功。
    * `[ipc_error]` 连接失败、写入超时、解析错误（包含错误码）。
    * `[ipc_roundtrip]` 记录请求序列号与往返延迟 `rtt_ms`。
  * `commit` (Info):
    * `[insertText]` 记录成功上屏文本长度与字符预览。

---

## 2. C++ 守护进程日志契约（`spdlog`）

* **输出目标**:
  1. 控制台标准输出（带色彩区分与时间戳）。
  2. 滚动文件: `$HOME/.local/state/babel/daemon.log`（单文件 10MB，保留 3 个）。
* **格式规范**:
  `[YYYY-MM-DD HH:mm:ss.ms] [level] [thread_id] [module] message`
* **必记事件**:
  * `[DaemonServer]` 监听 Unix Domain Socket 路径及权限。
  * `[ModelPaths]` 模型路径解析源（环境变量 / 标准路径 / 历史回退）。
  * `[FastCorrector]` 击键分词与 Trie 匹配耗时（微秒级）。
  * `[LlmEngine]` 慢流触发、推理耗时、首字响应延迟（毫秒级）。

