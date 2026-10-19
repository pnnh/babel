# Quickstart: AI 辅助双语浮窗转换器（MVP）验证指南

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md) ｜ **Data Model**: [data-model.md](./data-model.md) ｜ **Contracts**: [contracts/](./contracts/)

本指南用于在 `/speckit.implement` 完成本特性对应的 `tasks.md` 任务后，手工验证端到端行为是否满足 `spec.md` 的验收场景与成功标准。本文件**不包含**实现代码，具体模块实现见 `data-model.md`/`contracts/`，任务拆分见 `tasks.md`（由 `/speckit.tasks` 生成）。

## 前置条件

- 5 项原 `[NEEDS CLARIFICATION]` 实现选型已由用户显式委派、Agent 采纳 `research.md` 对应 Recommendation 全部确定：快流编辑距离/拼音 Trie——仓库内自研（§2）；全局热键绑定——分平台自研薄封装（§3）；模拟粘贴注入——分平台自研薄封装（§4.2）；C++ 单元测试框架——GoogleTest（§6）；本地 LLM 模型文件供给方式——用户手动下载后放置于约定本地目录（§7）。`/speckit.tasks`/`/speckit.implement` 需按此 5 项结果排期与落地，无需再次确认选型。
- 本地已按 `research.md` §7 的委派结果，由用户手动下载一份 Qwen2.5-3B-Instruct GGUF Q4_K_M 模型文件，并放置于应用约定的本地目录（应用不随安装包分发该文件，亦不提供应用内自动下载入口；具体路径常量留待 `/speckit.tasks`/`/speckit.implement` 阶段按内部细节自主确定）。
- 已安装对应平台的 Qt6（版本与路径见 `CMakePresets.json` 的 `linux`/`macos`/`windows` preset）与 vcpkg（`vcpkg.json` 需在 `/speckit.tasks`/`/speckit.implement` 阶段补充 `llama-cpp`、`gtest` 依赖引用）。

## 构建

按既有 CMake Presets 工作流构建（均为仓库既有配置，未因本特性改变构建入口）：

```bash
# Linux
cmake --preset linux
cmake --build --preset linux

# macOS
cmake --preset macos
cmake --build --preset macos

# Windows（PowerShell）
cmake --preset windows
cmake --build --preset windows
```

## 验证场景一：核心闭环——混杂草稿一键转换为标准中文并自动上屏（User Story 1，P1）

**环境要求**：macOS 或 Linux X11（已知支持自动粘贴注入环境，见 FR-010）。

1. 打开任意文本输入框（如系统备忘录/文本编辑器）并将光标聚焦其中。
2. 按下默认全局热键 `Ctrl+Alt+Space`（macOS: `Control+Option+Space`）。
   - **预期**：浮窗在前景弹出且处于可输入状态（对应 Acceptance Scenario 1；契约见 `contracts/global-hotkey-contract.md`）。
3. 在浮窗中输入一段中英拼音混杂文本（如 `wo xiang shuo zhege idea hen hao`）。
   - **预期**：原始输入槽实时展示规整化文本，主观无卡顿（SC-001/SC-002；契约见 `contracts/qml-cpp-session-interface.md` 的 `rawInputSlotText`）。
4. 停止输入，等待中文输出预览区域展示生成结果。
   - **预期**：首批内容出现等待 <300ms、整句完成 <800ms（SC-003，普通消费级 CPU 环境下测量；通过 `outputPreviewState` 的 `Debouncing`→`Generating`→`Ready` 迁移观察）。
5. 对预览结果满意后按下回车确认。
   - **预期**：浮窗关闭；标准中文文本已写入系统剪贴板；该内容无需任何额外手动操作即出现在步骤 1 的目标输入框内（SC-004；契约见 `contracts/injection-outcome-contract.md` 的 `Succeeded` 分支）。
6. 继续编辑原始输入文本（重新唤起浮窗或在确认前修改）。
   - **预期**：中文输出预览基于最新原始输入重新生成并更新（Acceptance Scenario 5）。
7. 唤起浮窗后按 Esc 或再次触发热键，不输入任何内容或改变主意取消。
   - **预期**：浮窗隐藏，不产生任何剪贴板写入或粘贴动作（Acceptance Scenario 6）。

**粗略准确率验证（SC-007）**：使用多组真实中英混杂草稿样本重复步骤 3–5，人工判断生成结果是否"可直接使用或仅需极少修改"；该比例 MUST 不低于 60%。

## 验证场景二：不支持自动粘贴注入环境下的手动粘贴降级（User Story 2，P2）

**环境要求**：已知不支持自动粘贴注入的环境（典型为 Linux Wayland 桌面）。

1. 重复验证场景一的步骤 1–5。
2. 在步骤 5（按回车确认）后：
   - **预期**：标准中文结果已写入系统剪贴板，界面明确展示"已复制到剪贴板，请手动粘贴"提示（SC-005；对应 `InjectionAttemptResult.outcome = UnsupportedEnvironment`，见 `contracts/injection-outcome-contract.md`），且无报错或无响应现象。
3. 切换到目标应用并手动执行粘贴（如 `Ctrl+V`）。
   - **预期**：剪贴板内容被正确粘贴，与浮窗确认前展示的输出预览内容一致（Acceptance Scenario 2）。

## 验证场景三：管道/注入异常时的友好提示而非崩溃（User Story 3，P3）

1. **模型文件缺失场景**：临时移除/重命名本地 LLM 模型文件后启动应用，唤起浮窗并尝试输入。
   - **预期**：系统提示"标准中文生成功能不可用"及可能原因，浮窗不崩溃、不无响应（SC-006；对应 `data-model.md` §6 的 `FloatingSessionState.ModelUnavailable`、`modelAvailable = false`）。
2. **慢流耗时异常场景**：人为制造高负载（或使用更大上下文）使慢流生成显著超出预期等待时间。
   - **预期**：界面给出可感知的等待状态提示，而非看起来冻结（User Story 3 验收场景 2；对应 `outputPreviewState = Generating` 的持续展示）。
3. **注入失败场景（本应支持的平台）**：在 macOS 上撤销本应用的 Accessibility 权限后，重复验证场景一步骤 1–5。
   - **预期**：标准中文内容已写入剪贴板，并提示用户注入未成功（文案与场景二步骤 2 完全相同，不展示具体失败原因，见 FR-012）。

## 已知不在本指南验证范围内的事项

以下事项因在 `plan.md`/`research.md` 中标记为 `[NEEDS CLARIFICATION]` 或已明确排除，不在本验证指南覆盖：

- 自定义热键组合、热键冲突提示（spec 未要求可配置性；冲突探测为 Edge Case，未转化为 FR）。
- 系统级输入法框架接入、模型微调、个性化学习记忆、学习提示气泡动画效果、Wayland 无感注入（均属 spec Out of Scope）。
- 单元测试层面的自动化验证（本指南聚焦手工端到端验收场景；单元测试框架已选型为 GoogleTest，见 `research.md` §6，具体测试用例由 `tasks.md` 排期、不在本指南覆盖范围）。
