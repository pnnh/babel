# Bug Verification: 地址栏在工具栏中居中显示且不随窗口宽度自适应

- **Slug**: address-bar-toolbar-layout
- **Tested**: 2026-09-04
- **Assessment**: ./assessment.md
- **Fix**: ./fix.md
- **Result**: verified

## Summary

独立于 `/speckit-bug-fix` 阶段已做的验证（不同的窗口尺寸、不同的分步幅度），重新走了一遍真机复现流程：原始 bug 报告的两个症状（地址栏居中显示、拖拽窗口不随宽度自适应）均已确认修复，双向（变宽/变窄）、多组尺寸下表现一致，未见回归。测试过程中额外定位到 fix.md 已记录的"一次性跳变导致工具栏折叠"边界情形的精确触发条件（仅缩窄方向的跳变会触发，增宽方向的跳变不会），细化了该已知限制的描述。

## Checks Performed

| Check | Command / Action | Result | Notes |
|-------|------------------|--------|-------|
| 复现步骤（修复后）—分步缩窄 | `xcodebuild` 全新构建 → `open` 启动 → `osascript`/System Events 分 5 步将窗口从 1400pt 缩到 880pt（`delay 0.25` 间隔，与 fix.md 使用的步长/间隔均不同）→ `screencapture -R` 截取工具栏条带 | pass | 地址栏左对齐紧邻导航按钮组，随每一步收窄；导航组固定左侧；多余图标正确收进原生 "»" 溢出菜单 |
| 复现步骤（修复后）—首次启动即宽窗口 | 应用启动后直接设置窗口为 1500pt（此前 fix.md 未测试过该尺寸） | pass | 首次渲染即正确铺满可用空间，非仅"resize 事件触发后才生效" |
| 新发现的边界情形—方向性验证 | 分别测试"一次性跳变增宽"（880→1400）与"一次性跳变缩窄"（1400→880） | 增宽 pass / 缩窄 fail | **仅缩窄方向的一次性跳变会导致整个工具栏折叠进溢出菜单**，增宽方向的一次性跳变表现正常；细化了 fix.md 中笼统记录的"一次性跳变会导致折叠" |
| 新增/更新的自动化测试 | N/A | not-run | `macos/kepler` 无 XCTest target（research.md §8 既有结论），fix.md 未新增测试，无自动化测试可运行 |
| 回归套件 | N/A | not-run | 同上，无既有自动化测试套件覆盖改动文件 |
| Lint / 类型检查 | `xcodebuild ... build` | pass | Swift 编译器类型检查随构建一起完成，BUILD SUCCEEDED，无新增警告 |

## Output Excerpts

```
** BUILD SUCCEEDED **
```

工具栏条带截图对比（均已裁剪为仅含窗口区域，不含屏幕其他内容）：
- 1500pt 首次启动、880pt 分步缩窄后、1400pt 分步放宽后——地址栏均正确左对齐紧邻导航组、随宽度连续变化。
- 1400→880pt 一次性跳变——整个工具栏（含导航按钮组）折叠为单个 "»" 图标。
- 880→1400pt 一次性跳变——表现正常，不折叠。

## Residual Risks

- **一次性缩窄跳变会导致整个工具栏折叠**（对应真实操作：点击绿色缩放按钮缩小窗口、双击标题栏还原到较小尺寸、macOS 窗口平铺缩窄）。已确认此限制具有方向性（仅缩窄方向触发，增宽方向不受影响），fix.md 中记录的两项补救措施（`validateVisibleItems()`、50ms 延迟二次校正）未能解决。**不在本次原始 bug 报告范围内**（报告场景是拖拽变宽变窄，即典型的分步 resize，已验证修复有效）。
- 未有自动化测试锁定本次修复，未来若 `FlexibleToolbarItemBridge` 或周边工具栏代码被修改，需要重复本报告记录的手动截图验证流程才能确认未回归。
- `FlexibleToolbarItemBridge` 依赖 SwiftUI 为 `.principal` placement 生成的内部 `NSToolbarItem` 结构（非公开 API 行为），未来 macOS/SwiftUI 版本升级后需要重新验证。

## Recommendation

关闭本次 bug——原始报告的两个症状（居中显示、拖拽不自适应）已端到端验证修复，双向、多组尺寸下表现一致。测试中定位到的"一次性缩窄跳变导致折叠"是一个精确刻画过的独立边界情形，不在原始报告范围内；若用户认为有必要修复，建议作为新的 `/speckit-bug-assess` 单独立项（触发条件已经很明确：`osascript`/真实点击绿色缩放按钮，在窗口已处于较宽尺寸时一次性缩窄），而不是重开本次 bug。
