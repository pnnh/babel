# Contract: 全局热键绑定

**Spec**: [spec.md](../spec.md) ｜ **Research**: [research.md](../research.md) §3

## 适用范围

本契约定义全局热键模块对外（对 `SessionController`，见 `qml-cpp-session-interface.md`）暴露的行为契约。绑定库选型已由用户显式委派、Agent 采纳 `research.md` §3 的 Recommendation 定稿为**方案 1：分平台自研薄封装**（直接调用 Windows `RegisterHotKey` / macOS Carbon `RegisterEventHotKey` 或 Cocoa 全局事件监听 / Linux `libxcb`/`libX11` 的 `XGrabKey`，不引入第三方热键库）；该实现 MUST 满足本契约定义的行为，本契约文本因与具体实现解耦而无需因此次选型修改。

## 默认绑定（FR-001，固定值，MVP 不提供自定义入口）

| 平台 | 组合键 |
|------|--------|
| Windows / Linux | `Ctrl+Alt+Space` |
| macOS | `Control+Option+Space` |

## 行为契约

| 动作 | 触发条件 | 效果 |
|------|----------|------|
| `ToggleFloatingWindow` | 用户按下默认组合键，且当前会话处于 `Hidden` | 捕获 `TargetFocusContext`（见 `data-model.md` §4），会话迁移至 `Invoked`，浮窗显示（FR-001） |
| `ToggleFloatingWindow` | 用户再次按下默认组合键，且当前会话处于非 `Hidden` 状态 | 会话迁移至 `Cancelled` → `Hidden`，MUST NOT 产生任何剪贴板写入或粘贴动作（Acceptance Scenario：User Story 1 场景 6） |

## 已知限制（继承自 spec，非本契约新增）

- Linux Wayland 桌面环境下，热键注册/捕获能力不保证可用；该限制属于"自动粘贴注入"降级路径的同类问题（FR-009/FR-012 统一提示"已复制到剪贴板，请手动粘贴"），不在本契约中单独定义差异化行为。
- 热键与系统已有快捷键冲突的探测与用户提示（spec Edge Cases 提出但未转化为 FR）：本契约不定义该行为，任何实现 MUST NOT 自行发明冲突提示文案或处理策略。
