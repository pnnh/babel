# Data Model: macOS 原生输入法接入原型与 IPC 骨架

**Feature**: `008-macos-imk-stub-ipc` | **Date**: 2026-11-12

---

## 1. IPC 协议数据模型

Swift 前端与 C++ 守护进程通过 Unix Domain Socket 交互，消息采用 `[4字节长度前缀 (大端)] + [UTF-8 JSON Payload]` 序列化。

### 1.1 基础消息包（`IpcEnvelope`）

```json
{
  "seq": 1001,
  "type": "KEY_DOWN",
  "payload": { ... }
}
```

* `seq` (uint64): 自增序列号，用于请求-响应匹配。
* `type` (string): 消息类型，取值为：
  * `"HELLO"`: 前端连入握手。
  * `"KEY_DOWN"`: 击键事件请求。
  * `"RESET"`: 会话重置请求（焦点转移或用户取消）。
  * `"COMMIT"`: 确认上屏请求。
  * `"HEARTBEAT"`: 连接保活（可选）。

### 1.2 关键载荷定义

#### 握手响应（`HelloResponse`）
```json
{
  "version": "0.1.0",
  "daemonStatus": "READY",
  "modelAvailable": true,
  "modelPath": "/Users/user/.local/models/qwen2.5-3b-instruct-q4_k_m.gguf"
}
```

#### 击键请求（`KeyDownRequest`）
```json
{
  "keyCode": 49,
  "modifiers": 0,
  "characters": " "
}
```

#### 击键响应（`KeyDownResponse`）
```json
{
  "consumed": true,
  "action": "UPDATE_PREEDIT",
  "markedText": "wo today",
  "commitText": ""
}
```
* `consumed` (bool): 前端是否应当消费当前按键（`handleEvent` 返回 `true`）。若为 `false`，前端透传按键（返回 `false`）。
* `action` (string): 指令动作：
  * `"NONE"`: 无状态变化。
  * `"UPDATE_PREEDIT"`: 更新宿主应用的 Marked Text（下划线组合文本）。
  * `"COMMIT_TEXT"`: 执行文字上屏。
* `markedText` (string): 需通过 `[client setMarkedText:selectionRange:replacementRange:]` 设置的预编辑字符串。
* `commitText` (string): 需通过 `[client insertText:replacementRange:]` 上屏的最终文本。

---

## 2. 模型路径解析模型（`ModelPathResolution`）

### 2.1 路径解析源（`ModelSource`）
```cpp
enum class ModelSource {
  EnvironmentVariable,      // 通过 BABEL_MODEL_PATH 指定
  ConventionalUserDirectory,// 标准用户目录 ~/.local/models/
  LegacyFallback            // 历史目录 ~/Library/Application Support/Babel/models/
};
```

### 2.2 路径解析结果实体（`ResolvedModelPath`）
* `targetPath`: `std::filesystem::path` - 最终用于加载的模型文件物理绝对路径。
* `source`: `ModelSource` - 路径解析来源。
* `exists`: `bool` - 物理文件是否存在且可读。
* `fileSizeBytes`: `uint64_t` - 文件大小（字节）。

---

## 3. 前端输入状态机（`InputMethodSessionState`）

```text
┌─────────────────┐
│     Inactive    │ ◄─── deactivateServer
└────────┬────────┘
         │ activateServer
         ▼
┌─────────────────┐
│   PassThrough   │ ◄─── Reset / Empty Buffer / Secure Input
└────────┬────────┘
         │ 按下字母键且 Daemon 响应 consumed=true
         ▼
┌─────────────────┐
│    Composing    │ ◄─── 持续击键，更新 Marked Text
└────────┬────────┘
         │ 按下确认键（如回车/空格），Daemon 响应 COMMIT_TEXT
         ▼
┌─────────────────┐
│    Committing   │ ───► 调用 [client insertText:] ───► 回到 PassThrough
└─────────────────┘
```

