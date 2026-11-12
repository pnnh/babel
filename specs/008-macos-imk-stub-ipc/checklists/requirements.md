# Specification Quality Checklist: macOS 原生输入法接入原型与 IPC 骨架

**Purpose**: 验证规范完整性、边界明确度与质量，确保进入 planning 阶段前无模糊需求
**Created**: 2026-11-12
**Feature**: [spec.md](../spec.md)

## 内容质量检查

- [x] 语言合规：所有说明性正文、用户故事、功能需求与验收场景均采用简体中文编写。
- [x] 授权范围明确：严格覆盖用户提出的五项要求（Swift IMKit Stub、Unix Domain Socket IPC、~/.local/models 路径迁移、前后端关键事件日志、保留 Qt 浮窗模式）。
- [x] 优先级明确：按 P1 ~ P5 拆解为可独立测试的细分故事。
- [x] 成功标准可量化：包含安装识别、零剪贴板污染上屏、IPC 延迟 <1ms、Console.app 日志可观测、浮窗回归验证等可衡量指标。
- [x] 边界与异常覆盖：包含安全输入框（Secure Input）、IPC 故障直通回退、路径波浪号展开等边界场景。

