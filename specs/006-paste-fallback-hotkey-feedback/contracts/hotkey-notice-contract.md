# Contract: 热键注册失败系统通知

**Spec**: [spec.md](../spec.md) ｜ **Research**: [research.md](../research.md) §2 ｜ **Data Model**: [data-model.md](../data-model.md) 三

## 适用范围

本契约定义 `SessionController::bootstrapPlatformDefaults()` 中全局热键注册失败后，通过 `QSystemTrayIcon` 承载的用户可感知提示的行为契约（FR-005/FR-006/FR-007/FR-008）。

## 行为契约

1. **触发条件**（FR-006）：应用启动、`GlobalHotkey::registerHotkey()` 返回 `false`（注册失败）后，MUST 在同一次 `bootstrapPlatformDefaults()` 执行流程内立即触发通知，MUST NOT 延后至用户实际尝试按下热键之后。
2. **承载方式**（FR-005，含 `/speckit.plan` 阶段修订）：MUST 通过 `QSystemTrayIcon::showMessage()` 呈现；应用 MUST 创建并保持一个常驻系统托盘图标（`show()`，不随通知消失而隐藏）。
3. **可用性兜底**：创建托盘图标前 MUST 先调用 `QSystemTrayIcon::isSystemTrayAvailable()`；返回 `false` 时 MUST NOT 创建图标、MUST NOT 尝试调用 `showMessage()`，仅保留既有 `spdlog::warn` 诊断日志（不崩溃、不抛异常）。
4. **防骚扰**（FR-008）：同一次应用运行期内，MUST NOT 重复展示该通知超过一次；具体实现为布尔守卫字段 `m_hotkeyFailureNoticeShown`，首次触发后置位，不要求规格级别的数值化节流算法。
5. **文案**（FR-007）：本契约 MUST NOT 规定强制文案；`research.md` §2 给出的措辞仅为实现阶段参考建议，非规格强制值。MUST 满足的唯一文案约束：用户阅读后 MUST 能理解"全局热键当前不可用"这一事实。
6. **图标资源**：MUST 使用零新增资源文件的方案（如 `Qt` 内置标准图标）；自定义品牌图标美术设计不在本契约范围内（Out of Scope）。
7. **不影响既有流程**：本契约新增的逻辑 MUST NOT 改变 `registerHotkey()` 成功时的既有行为（不创建额外提示，仅保留既有 `spdlog::info` 日志）；MUST NOT 改变 `bootstrapPlatformDefaults()` 中其余初始化步骤（模型加载、防抖调度器、剪贴板/粘贴注入器构造）的既有顺序与行为。

## 强约束

- **MUST NOT**：将应用改造为完整意义上的"常驻托盘应用"（右键菜单、从托盘退出应用等）——本契约仅要求最小化的图标创建 + 单次通知能力（Out of Scope）。
- **MUST**：托盘图标与通知调用均发生在主 GUI 线程（`bootstrapPlatformDefaults()` 运行时机本身已在主线程，`QSystemTrayIcon` 本身也要求在 GUI 线程构造/操作，不涉及新增跨线程同步）。

## 测试契约边界（如实记录，`research.md` §2 末尾）

`bootstrapPlatformDefaults()` 为生产专用方法（测试 MUST NOT 调用，与 004/005 既有测试边界一致）。本契约定义的真实端到端效果（托盘图标真实可见、通知真实弹出）无法从 `SessionControllerTest.cpp` 自动化触发，MUST 延期至 `quickstart.md` 人工验证。可自动化验证的确定性子集：若 `m_hotkeyFailureNoticeShown` 防重复逻辑被封装为独立可调用单元，其布尔切换语义本身可脱离真实 `QSystemTrayIcon` 实例单独测试。
