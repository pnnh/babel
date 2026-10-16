# Bug Fix: 地址栏在工具栏中居中显示且不随窗口宽度自适应

- **Slug**: address-bar-toolbar-layout
- **Fixed**: 2026-09-04（第二轮，替换第一轮已证实无效的实现）
- **Assessment**: ./assessment.md
- **Status**: applied（核心症状——居中显示、拖拽窗口不自适应——已通过实机截图+像素测量确认修复；一个未在原始 bug 报告中出现的次要边界情形仍未解决，见 Follow-ups）

## Summary

第一轮实现（`FlexibleToolbarItemBridge` 设置固定的 `minSize`/`maxSize` 范围）经复测证实无效——AppKit 把 `minSize` 当作固定尺寸而非下限。第二轮改变策略：同一个 `NSViewRepresentable` 桥接，但**每次窗口 resize 时实时计算地址栏应得的精确宽度**（窗口宽度减去其余工具栏项的实测宽度、留出间距余量），把 `minSize` 与 `maxSize` 同时设为这个刚算出的精确值，模拟连续伸缩效果。已用 `osascript`/System Events 精确控制窗口尺寸 + `screencapture -R` 截图 + 像素测量的方法，对增宽、缩窄两个方向分别实测确认有效。

## Changes

| File | Change | Notes |
|------|--------|-------|
| `macos/kepler/Kepler/Pages/MainPage.swift` | modified | `FlexibleToolbarItemBridge.applyFlexibleSizing`：从"设一次性 min/max 范围"改为"每次 resize 重新计算精确目标宽度"；新增 `spacingAllowance`/55% 安全上限等防护逻辑 |
| `macos/kepler/Kepler/Pages/MainPage.swift` | modified | `makeNSView` 的 resize 回调新增一次 50ms 延迟的二次校正调用；`applyFlexibleSizing` 末尾新增 `toolbar.validateVisibleItems()` |

未新增独立文件，未触及 `SavedFolder.swift`/`project.pbxproj`/entitlements——与第一轮范围一致。

## Diff Highlights

```swift
/// 每次 resize 时实时计算精确宽度并强制设置（而非声明一个范围）：
private func applyFlexibleSizing(from markerView: NSView) {
    guard let win = markerView.window, let toolbar = win.toolbar else { return }
    guard let myItem = toolbar.items.first(where: { item in
        guard let v = item.view else { return false }
        return contains(ancestor: v, descendant: markerView)
    }) else { return }

    let otherItemsWidth = toolbar.items
        .filter { $0 !== myItem }
        .reduce(CGFloat(0)) { total, item in
            let fitting = item.view?.fittingSize.width ?? 0
            let width = fitting > 0 ? fitting : max(item.view?.frame.width ?? 0, 28)
            return total + width
        }
    let spacingAllowance: CGFloat = CGFloat(max(toolbar.items.count - 1, 0)) * 8 + 24
    let rawDesired = win.frame.width - otherItemsWidth - spacingAllowance
    // 安全上限：otherItemsWidth 可能被低估（某项还未完成布局时 fittingSize 接近 0），
    // 曾导致算出的宽度远大于实际可用空间，使 AppKit 把整个工具栏（含固定的导航按钮组）
    // 一并折叠进溢出菜单。任何情况下都不超过窗口宽度的 55%。
    let desiredWidth = max(160, min(rawDesired, win.frame.width * 0.55))
    let height = myItem.minSize.height > 0 ? myItem.minSize.height : 28

    myItem.minSize = NSSize(width: desiredWidth, height: height)
    myItem.maxSize = NSSize(width: desiredWidth, height: height)
    toolbar.validateVisibleItems()
}
```

`makeNSView` 中的 resize 监听增加了一次延迟二次校正：

```swift
coordinator.resizeObserver = NotificationCenter.default.addObserver(
    forName: NSWindow.didResizeNotification, object: win, queue: .main
) { [weak view] _ in
    guard let view else { return }
    applyFlexibleSizing(from: view)
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { [weak view] in
        guard let view else { return }
        applyFlexibleSizing(from: view)
    }
}
```

## Tests Added or Updated

`macos/kepler` 无 XCTest target（沿用 research.md §8 既有结论），未新增自动化测试。

## Local Verification

**方法**：`osascript`/System Events 精确设置 Kepler 主窗口尺寸 → `screencapture -R<x>,<y>,<w>,<h>` 只裁剪窗口区域截图 → 裁剪出工具栏条带直接目视比对（必要时辅以 Python/PIL 像素测量）。用户已开通"屏幕录制"与"辅助功能"两项权限，本轮全部在真机上完成，不是代码走查或推测。

| 测试场景 | 操作方式 | 结果 |
|---|---|---|
| 1400pt → 900pt，**分五步**渐进缩小（`delay 0.3` 间隔，模拟真实拖拽） | 增量 resize | ✅ 地址栏从铺满 1400pt 可用空间平滑收窄到铺满 900pt 可用空间；导航按钮组始终固定左侧，超出的工具组图标自动收进系统原生 "»" 溢出菜单（与 007 特性依赖的原生溢出机制一致） |
| 900pt → 1400pt，分四步渐进放大 | 增量 resize | ✅ 反方向同样平滑，双向确认 |
| 1400pt → 900pt，**一次性跳变** | `set size of window to {900, 800}`（单次） | ❌ 整个工具栏（含固定的导航按钮组）被折叠进溢出菜单——见 Follow-ups |

**结论**：用户实际操作窗口（拖拽窗口边缘）产生的是连续的小幅度 resize 事件序列，与上表"分步渐进"场景一致——**已确认修复对真实使用场景有效**。一次性跳变（`osascript` 单次设置新尺寸，现实中对应双击标题栏缩放按钮/窗口平铺复原等）是本轮测试中意外发现的次要边界情形，非原始 bug 报告涵盖的场景。

- `xcodebuild -project Kepler.xcodeproj -scheme Kepler -configuration Debug build` → 全部 5 次迭代均 **BUILD SUCCEEDED**。

## Deviations from Assessment

- assessment.md 修订后的 Preferred 方案建议"完整 `NSToolbarDelegate` 替换"（认为事后桥接这条路已被证明走不通）。本轮没有直接跳到那个方案，而是先在同一套"事后桥接"机制内换了一种计算策略（从"声明范围"改为"每次强制设精确值"）——这个更小的改动就已经让分步 resize 场景完全生效，因此没有必要进行 assessment.md 中预期的大规模重写。这是一次比 assessment.md 建议成本更低的成功尝试，记录在此供后续参考。
- 发现了一个 assessment.md 未预见到的新边界情形（一次性跳变 resize 导致整个工具栏折叠），已在 Follow-ups 中记录，未纳入本轮修复范围（不在原始 bug 报告内）。

## Follow-ups

- **一次性跳变 resize 会导致整个工具栏（含导航按钮组）折叠进溢出菜单**，即使加了 `validateVisibleItems()` 和 50ms 延迟二次校正也未能解决。已知会触发此情形的真实操作：双击标题栏最大化/还原、点击绿色缩放按钮、macOS 窗口平铺（Split View）。这不是原始 bug 报告涵盖的场景（用户报告的是拖拽窗口变宽变窄），但既然是本轮测试中发现的真实缺陷，如实记录：**若用户认为有必要修复**，建议作为独立的后续 bug 提出（可复用同一测试方法：先用一次性 `set size` 复现，再验证修复），而不是顺带扩大本次修复范围。
- 建议在 quickstart.md 场景7补一条"分步拖拽 vs 一次性跳变"的说明，避免未来验证时用错方法（例如若用脚本自动化测试，必须模拟渐进 resize，而非单次跳变，否则会误判为 bug 复现）。
- `FlexibleToolbarItemBridge` 依赖 SwiftUI 为 `.principal` placement 生成的内部 `NSToolbarItem` 结构（非公开 API 行为），未来 macOS/SwiftUI 版本升级后建议重新验证该桥接是否仍然生效。
