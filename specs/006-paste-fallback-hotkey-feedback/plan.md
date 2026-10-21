# Implementation Plan: 粘贴降级与热键冲突反馈补完

**Branch**: `006-paste-fallback-hotkey-feedback`（Git 当前实际所在分支为 `main`，延续 001～005 既有做法——本仓库不为单个特性单独建立 feature branch，特性产物以 `specs/006-paste-fallback-hotkey-feedback/` 目录隔离）

**Date**: 2026-10-21

**Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/006-paste-fallback-hotkey-feedback/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/plan-template.md` for the execution workflow.

## Summary

本特性补齐 001 遗留的两个刻意留白边界场景：(1) 目标应用窗口在浮窗唤起期间失效后的粘贴降级行为；(2) 全局热键注册失败时的用户可见提示。spec.md 2026-10-21 Clarifications 已确定：FR-001 统一降级为与 001 FR-012 一致的"已复制到剪贴板，请手动粘贴"提示，不冒险模拟粘贴；FR-005/FR-006 通过常驻 `QSystemTrayIcon` 托盘图标 + 启动即时系统通知呈现热键失败提示（`/speckit.plan` 阶段修订：由"一次性零图标"改为"常驻图标"，详见 Clarifications）。

核心技术发现（`research.md` 详述）：三平台 `PasteInjector::attemptPaste()` 均是"投递给系统当前焦点"而非"投递给特定窗口句柄"，因此"目标窗口有效性判断"的正确语义是"是否仍是系统当前焦点"，可直接复用 `FocusContext.cpp` 既有的焦点捕获逻辑做"重新捕获+比较"，三平台均可行，无需新增平台原生检测 API；`QSystemTrayIcon` 所需的 `Widgets` 模块已链接，零新增第三方依赖。

实现范围：
1. `PasteInjector` 新增 `isTargetFocusStillValid()` 虚方法（默认 `true`，向后兼容），三平台实现真实检测逻辑。
2. `SessionController::confirm()` 新增前置判断：目标已失效时跳过 `attemptPaste()`，直接走既有"非 Succeeded"统一降级分支。
3. `SessionController::bootstrapPlatformDefaults()` 新增常驻 `QSystemTrayIcon` 创建 + 热键注册失败时的一次性通知。

## Technical Context

**Language/Version**: C++23（沿用 001～005 既有 `CMakeLists.txt` 配置，本特性未变更语言标准）。

**Primary Dependencies**:
- Qt6（沿用既有依赖；`QSystemTrayIcon` 定义于已链接的 `Widgets` 模块，`research.md` §2 已核实本机 Qt 安装头文件存在，**零新增依赖**）。
- Windows `user32`（`GetForegroundWindow`，既有 `PasteInjector_win.cpp`/`FocusContext.cpp` 已链接）、macOS `ApplicationServices`（`CGWindowListCopyWindowInfo`，既有 `FocusContext.cpp` 已 `#include`）、Linux `libX11`（`XGetInputFocus`，既有 `FocusContext.cpp` 已链接）——均为既有依赖的既有 API 复用，不新增链接项。
- 单元测试框架延续 GoogleTest，不新增/更换测试框架。

**Storage**: 不引入任何新的持久化存储；`m_hotkeyFailureNoticeShown` 为纯运行时内存状态，每次应用启动固定重新初始化为 `false`。

**Testing**: 延续 GoogleTest + `gtest_discover_tests`；涉及以下既有测试文件的更新：
- `tests/session/SessionControllerTest.cpp`：新增测试替身 `PasteInjector` 子类（override `isTargetFocusStillValid()` 返回 `false`），验证 `confirm()` 跳过 `attemptPaste()` 并展示手动粘贴提示（`contracts/target-focus-validity-contract.md` 测试契约）；新增默认 `isTargetFocusStillValid()` 返回 `true` 时既有成功路径无回归的验证。
- `tests/inject/PasteInjector*Test.cpp`：若需要为各平台真实实现的 `isTargetFocusStillValid()` 补充测试，视 `/speckit.tasks` 拆分粒度决定（平台相关真实窗口焦点状态难以在 CI 中确定性构造，预期以"不崩溃、返回合法布尔值"为主的弱断言为主）。

**Target Platform**: 延续 001～005 既有桌面三平台 Windows/macOS/Linux 支持范围，未扩大/缩小目标平台集合。

**Project Type**: desktop-app（延续既有单一常驻 Qt6 Quick 进程；本特性新增一个常驻 `QSystemTrayIcon`，但不改变"无持久可见主 QML 窗口"的既有定位——托盘图标是 Widgets 层面的系统原生 UI 元素，不是 QML 窗口）。

**Performance Goals**（引用 spec Success Criteria，非本阶段新定义指标）:
- SC-001/SC-002：目标窗口失效/有效两种场景下用户均能获得与实际结果一致的明确反馈，有效场景下行为与 001 完全一致、无回归。
- SC-003/SC-004：热键注册失败后用户能在合理时间内感知到通知，且不会被重复骚扰。

**Constraints**:
- `PasteInjector` 既有调用点（`confirm()` 中 `attemptPaste()` 调用）与既有测试替身 MUST 保持源码兼容（新增方法默认实现 `{ return true; }`，`research.md` §1）。
- MUST NOT 改变 001 已定义的"目标窗口在确认时仍然有效"场景下的既有粘贴流程（FR-003）。
- MUST NOT 新增 `InjectionOutcome` 枚举取值（`data-model.md` 三.1，复用既有"非 Succeeded 统一降级"分支）。
- MUST NOT 将应用改造为完整"常驻托盘应用"（无右键菜单、无退出入口，`contracts/hotkey-notice-contract.md`）。

**Scale/Scope**: 本特性修改范围限定于 `babel/src/inject/PasteInjector.h`、`PasteInjector_{win,mac,x11}.*`、`babel/src/session/SessionController.{h,cpp}` 及其对应测试文件，不新增新的 QML 文件，不新增第三方依赖。

本计划在设计过程中**新发现 1 项**命中宪章判定程序 (a)（通知机制"常驻图标 vs 真零图标"的权衡，直接影响外部可观察的 UI 存在性）的架构选择点；**用户已在本轮 `/speckit.plan` 对话中就该项作出显式决策**：采纳常驻图标方案（详见 `research.md` §2、`spec.md` Clarifications "`/speckit.plan` 阶段修订"）。该决策已回填至 `spec.md` FR-005/Assumptions/Out of Scope 对应小节，避免 spec 与 plan 产生矛盾。除该项外，本阶段核实到的其余技术事实（三平台粘贴注入的"系统焦点"投递语义、`Widgets` 模块已链接、项目当前无图标资源）均已在 `research.md` 给出明确 Decision，不遗留歧义。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Language (宪章《语言与文档规范》)：本 plan 及同特性 research/data-model/quickstart/contracts 的说明性文字 MUST 使用简体中文；API/路径/库名等标识符可保留英文。*

**原则 I–VI 逐条核对**：

- **原则 I（显式需求是唯一授权范围）**：本计划全部设计内容均可追溯至 `spec.md` FR-001～FR-009、Key Entities、Clarifications 原文，或本阶段新发现、已交由用户显式决策且未由 Agent 擅自拍板的通知机制权衡（见上）。
- **原则 II（不臆测未声明的需求/不预先设计未决策事项）**：`isTargetFocusStillValid()` 默认实现 `{ return true; }`、三平台"重新捕获+比较"均严格限定在 FR-001/FR-002 字面授权范围内；`InjectionOutcome` 枚举未扩展（避免过度设计超出 FR-003 边界）。
- **原则 III（Recommendation 非最终决策）**：通知机制权衡的两个选项（常驻图标 / 真零图标）均已明确标注为候选方案，直至用户显式批复"常驻图标"后才视为最终决策，已回填 `spec.md`。
- **原则 IV（内部实现细节可由 Agent 自主决定）**：托盘图标占位图标选用 Qt 内置标准图标（视觉美术非产品决策分歧点）、`m_hotkeyFailureNoticeShown` 守卫字段命名等内部实现细节，已按原则 IV 自主决定并记录于 `research.md`，不升级为 `[NEEDS CLARIFICATION]`。
- **原则 V（测试先行/契约先行）**：`contracts/target-focus-validity-contract.md`/`contracts/hotkey-notice-contract.md` 已先于代码实现定义行为契约与测试契约。
- **原则 VI（跨特性一致性）**：复用 001 `contracts/injection-outcome-contract.md`"非 Succeeded 统一降级"既有分支、004/005 已确立的"生产专用私有/production-only 方法测试边界延期至 quickstart.md 人工验证"先例，未引入与既有特性相悖的新模式。

**判定程序门禁**：对本 plan 中每一项用户未显式指定的事项，已逐条执行宪章《判定程序》(a)–(e)。**1 项**命中 (a) 的事项（通知机制常驻图标 vs 真零图标）此前已标记候选方案、未由 Agent 代为决策，现已取得用户显式决策并回填；其余事项判定结果全部为"否"，已按原则 IV 自主决定并记录。本计划**无遗留** `[NEEDS CLARIFICATION]`。

**Gate 结论**：**PASS**——此前 1 项待用户显式决策的架构分歧点已由用户显式决策（采纳常驻图标方案）并回填至 `spec.md`/`research.md`/`data-model.md`/`contracts/`，无条件通过。

**Post-Design Re-check**（Phase 1 设计产物完成后复查）：`data-model.md`/`contracts/` 均已据用户决策写入最终值，未留存候选方案占位；`contracts/target-focus-validity-contract.md` 明确"向后兼容"约束（默认实现不破坏既有测试替身），`contracts/hotkey-notice-contract.md` 明确"MUST NOT 改造为完整常驻托盘应用"的范围边界，均不构成新的 `[NEEDS CLARIFICATION]`。**本次复查结论：PASS（无条件通过，无新增违规，无遗留事项）**，可直接进入 `/speckit.tasks` 的最终排期与 `/speckit.implement` 的实现阶段。

## Project Structure

### Documentation (this feature)

```text
specs/006-paste-fallback-hotkey-feedback/
├── plan.md                                    # 本文件（/speckit.plan 命令输出）
├── research.md                                # Phase 0 输出（含 §2 用户决策，无遗留 NEEDS CLARIFICATION）
├── data-model.md                              # Phase 1 输出
├── quickstart.md                              # Phase 1 输出（5 个验证场景）
├── contracts/
│   ├── target-focus-validity-contract.md      # FR-001/FR-002 行为契约
│   └── hotkey-notice-contract.md              # FR-005/FR-006/FR-007/FR-008 行为契约
└── tasks.md                                    # Phase 2 输出（/speckit.tasks 命令，本命令不创建）
```

### Source Code (repository root)

本特性严格限定于以下既有文件的增量修改，**不新增新的源文件/头文件/QML 文件**：

```text
babel/src/inject/PasteInjector.h          # 新增 isTargetFocusStillValid() 虚方法
babel/src/inject/PasteInjector_win.cpp    # 实现 Windows 真实检测
babel/src/inject/PasteInjector_mac.mm     # 实现 macOS 真实检测
babel/src/inject/PasteInjector_x11.cpp    # 实现 Linux X11 真实检测
babel/src/session/SessionController.h     # 新增 m_trayIcon/m_hotkeyFailureNoticeShown 成员
babel/src/session/SessionController.cpp   # confirm() 前置判断 + bootstrapPlatformDefaults() 托盘图标/通知

tests/session/SessionControllerTest.cpp   # 新增 FR-001 测试替身验证用例
```

**Structure Decision**：延续 001～005 既有"单一桌面应用项目，特性产物以 `specs/<NNN>/` 目录隔离、不建立独立 feature branch"的既定结构；本特性不引入任何新的顶层目录或新的构建目标。

## Complexity Tracking

*无 Constitution Check 违规，本节不适用。*
