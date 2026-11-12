# Implementation Plan: macOS 原生输入法接入原型与 IPC 骨架

**Branch**: `008-macos-imk-stub-ipc` | **Date**: 2026-11-12 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/008-macos-imk-stub-ipc/spec.md`

## Summary

本特性作为混合输入法迈向 macOS 原生输入法体系的 Phase 1（Step 1），核心落地：
1. **Swift IMKit 输入法 Bundle**：新建纯 Swift 编写的输入法轻量宿主 `BabelInputMethod.app`，安装于 `~/Library/Input Methods/`，负责拦截按键并调用 `[client insertText:]` 原生直接上屏。
2. **Unix Domain Socket (UDS) IPC 通信层**：在 `$HOME/.local/run/babel.sock` 建立前后端管道，按键 RTT 延迟 $< 1\text{ms}$，在后端未就绪时自动降级直通。
3. **模型存储路径迁移**：重构 `ModelPaths`，将默认模型放置目录由 `~/Library/Application Support/Babel/models` 迁移至 `$HOME/.local/models`，支持 `BABEL_MODEL_PATH` 环境变量优先覆盖。
4. **统一关键事件日志**：Swift 前端接入 Apple `os.Logger`，C++ 守护进程接入 `spdlog`（滚动输出至 `~/.local/state/babel/daemon.log`）。
5. **开发期 Qt 浮窗模式保留**：C++ 应用支持 `--standalone` 与 `--daemon` 区分运行模式，保障原有 IDE 开发体验。

---

## Technical Context

**Language/Version**: 
* 前端：Swift 6.0+（macOS 14+ SDK，使用 AppKit 与 InputMethodKit）；
* 后端：C++23（沿用既有工程语言标准）；
* 胶水与构建：CMake 3.29+（开启 Swift 与 CXX 混合编译支持）。

**Primary Dependencies**: 
* Apple `InputMethodKit.framework`、`AppKit.framework`、`os.Logger`；
* Qt 6（用于既有 Standalone 浮窗与事件循环）；
* `spdlog`（结构化日志）、`unofficial-sqlite3`、`llama.cpp`（既有依赖）。

**Storage**: 
* 模型文件目录：`$HOME/.local/models/`；
* 运行态 IPC 套接字：`$HOME/.local/run/babel.sock`；
* 守护进程日志：`$HOME/.local/state/babel/daemon.log`。

**Testing**: 
* GoogleTest（后端 C++ 单元测试：`ModelPathsTest`、`IpcProtocolTest`）；
* 手动/集成测试（TextEdit 输入法激活与上屏验证、Console.app 日志排查）。

**Target Platform**: macOS (Apple Silicon / Intel, macOS 14.0+)。

**Project Type**: Desktop Input Method Bundle (.app) + Background Daemon CLI / Standalone Qt App。

**Performance Goals**: 
* 按键 IPC 往返响应延迟（RTT）中位数 $< 1\text{ms}$；
* 前端 Stub 内存占用 $< 30\text{MB}$；
* 守护进程冷启动在模型加载后 1s 内就绪。

**Constraints**: 
* 输入法宿主主线程绝不执行任何耗时计算，IPC 异常时无感降级为直通；
* 剪贴板内容零污染；
* 既有 Qt 浮窗模式（`--standalone`）完全向后兼容。

---

## Constitution Check

*Language (宪章《语言与文档规范》)：本 plan 及同特性 research/data-model/quickstart/contracts 的说明性文字 MUST 使用简体中文；API/路径/库名等标识符可保留英文。*

**原则 I–VI 逐条核对**：
* **原则 I（显式需求是唯一授权范围）**：设计范围严格锁定于用户明确提出的 5 项诉求（Swift 前端、UDS IPC、`~/.local/models` 路径迁移、前后端日志、保留 Qt 浮窗），未引入超出范围的整句大模型微调或复杂候选窗。
* **原则 II（未决策不得默认，Assumption 不得充当决策）**：所有假设均以客观系统机制（IMKit API、macOS 文件系统规范）为依据，协议格式与目录设计均在 `research.md` 中进行对比推导。
* **原则 III（推荐不是批准）**：未将未授权的建议视同既定需求。
* **原则 IV（内部细节低摩擦，外部行为高控制）**：IPC 报文 Header 格式（4字节大端）、内部序列号、日志 category 命名均属于内部实现细节，不改变外部约定。
* **原则 V（最小必要修改与实施边界）**：对既有代码修改严格限定于 `ModelPaths` 及 `main.cpp` 启动入口，既有 `FastCorrector` 逻辑保持稳定。
* **原则 VI（预授权质量基线不得发明产品语义）**：IPC 断连时的直通降级属于输入法安全可用性底线，未改变产品业务语义。

**Gate 结论**：**PASS**（无条件通过）。

---

## Project Structure

### Documentation (this feature)

```text
specs/008-macos-imk-stub-ipc/
├── plan.md                                              # 本实施计划
├── research.md                                          # 关键技术选型与调研
├── data-model.md                                        # IPC 实体与状态机模型
├── quickstart.md                                        # 编译、运行与多场景验证指南
├── checklists/
│   └── requirements.md                                  # 需求质量检查清单
└── contracts/
    ├── ipc-protocol-contract.md                         # UDS 通信协议契约
    ├── model-path-resolution-contract.md                # 模型路径解析与优先级契约
    └── logging-observability-contract.md                # 前后端结构化日志契约
```

### Source Code (repository root)

```text
# 1. Swift 原生输入法前端 Bundle
macos/BabelInputMethod/
├── Info.plist                                           # 输入法 Bundle 专属配置
├── main.swift                                           # 输入法启动入口
├── BabelServer.swift                                    # IMKServer 包装
├── BabelInputController.swift                           # IMKInputController 按键拦截与上屏
├── IpcClient.swift                                      # Unix Domain Socket 客户端
└── Logger.swift                                         # os.Logger 封装

# 2. C++ 守护进程与核心逻辑
babel/
├── CMakeLists.txt                                       # 新增 macOS 下编译 BabelInputMethod Target
├── main.cpp                                             # 支持 --daemon / --standalone 模式分流
└── src/
    ├── ipc/                                             # [新增] Unix Domain Socket 服务端
    │   ├── UnixDomainSocketServer.h
    │   └── UnixDomainSocketServer.cpp
    ├── slowpath/
    │   ├── ModelPaths.h                                 # [更新] 默认路径迁移至 ~/.local/models
    │   └── ModelPaths.cpp                               # [更新] 支持 BABEL_MODEL_PATH 与环境变量展开
    └── logging/                                         # [新增] 守护进程结构化日志封装
        ├── DaemonLogger.h
        └── DaemonLogger.cpp

# 3. 单元测试增补
tests/
├── slowpath/
│   └── ModelPathsTest.cpp                               # [更新] 验证 ~/.local/models 与优先级
└── ipc/
    └── IpcProtocolTest.cpp                              # [新增] 验证 IPC 分帧与编解码
```

---

## 阶段实施里程碑

1. **里程碑 1 (M1)**：重构 `ModelPaths`，支持 `~/.local/models` 与 `BABEL_MODEL_PATH` 覆盖，更新并跑通全部现有测试。
2. **里程碑 2 (M2)**：在 `babel_core` 中实现 C++ `UnixDomainSocketServer`，`main.cpp` 支持 `--daemon` 与 `--standalone` 启动分流。
3. **里程碑 3 (M3)**：创建 `macos/BabelInputMethod` Swift 工程，实现 `IMKInputController` 按键拦截、IPC 请求发送与 `[client insertText:]` 上屏。
4. **里程碑 4 (M4)**：接入 Swift `os.Logger` 与 C++ `spdlog` 文件日志，完成端到端安装与打字验证。
