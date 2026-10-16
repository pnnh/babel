# Bug Assessment: 地址栏在工具栏中居中显示且不随窗口宽度自适应

- **Slug**: address-bar-toolbar-layout
- **Created**: 2026-09-04
- **Source**: 用户粘贴的截图 + 文字描述（`speckit-bug-assess` 会话）
- **Verdict**: valid（2026-09-04 复测：已通过实机截图直接复现，不再是推测）
- **Severity**: high

## Report（用户原始描述）

> 从截图中可以看到顶部工具栏中，地址栏至少有几处不符合当前 spec 预期。
> 首先地址栏没有靠左布局挨着最左侧的导航栏而是居中显示了。
> 另外当调整窗口大小时，地址栏并未自适应得变宽或变窄，更像是具有一个最小的宽度，然后会随着内容文字的长短变宽。

用户附带截图显示：主窗口工具栏从左到右为返回/前进/上级导航按钮、居中显示的地址栏（内容"Macos.notelibrary"，右侧带实心黄色收藏星标，与本特性 FR-022/FR-024 的视觉设计一致）、搜索图标、新建文件夹/文件、分隔线、排序、隐藏文件、排除文件、网格/列表切换、设置——完整图标集与 [MainPage.swift](../../../macos/kepler/Kepler/Pages/MainPage.swift) 当前 `.primaryAction` 分组的图标逐一核对一致，确认截图确实来自本特性构建产物（非其他应用），可信。

## Symptom

地址栏（`.principal` ToolbarItem）在工具栏中始终居中显示，且宽度仅随其自身文本内容长短变化，不随主窗口宽度调整而伸缩；预期行为（spec.md FR-004 修订，2026-09-04 第二轮澄清）是：导航按钮组固定左侧、搜索/设置等工具组固定右侧，地址栏铺满两者之间的全部剩余空间并随窗口宽度自适应变宽/变窄。

## Reproduction

1. 构建并启动 Kepler（`xcodebuild ... build` 后运行）。
2. 收藏并选中任意"位置"，使地址栏显示虚拟路径文本。
3. 观察地址栏在工具栏中的位置——**实际**：居中；**预期**：紧邻导航按钮组左对齐。
4. 拖动调整主窗口宽度（变宽/变窄）。
5. 观察地址栏宽度——**实际**：宽度基本不变，仅当文本内容变长/变短时才跟着变化；**预期**：随窗口宽度连续变化，填满导航组与右侧工具组之间的剩余空间。

**2026-09-04 复测已确认第4-5步**：用户开通"辅助功能"权限后，通过 `osascript`/System Events 精确控制 Kepler 主窗口尺寸（先 1400×800pt，再 900×800pt），并用 `screencapture -R` 仅裁剪窗口区域截图（不含屏幕其他内容）。像素测量结果：

| 窗口宽度 | 地址栏（含收藏星标）实测宽度 | 是否居中 |
|---|---|---|
| 1400pt | 约 150pt | 是——两侧留白明显 |
| 900pt | 约 150pt | 是——两侧留白明显 |

两种窗口宽度下地址栏宽度几乎完全一致（≈150pt，与代码中 `FlexibleToolbarItemBridge` 设置的 `minSize.width = 160` 高度吻合），且在两种宽度下均保持居中，未见任何随窗口宽度变化的迹象。**症状 1、2 均已通过直接观察确认，不再是推测。**

## Suspected Code Paths

- [MainPage.swift:557](../../../macos/kepler/Kepler/Pages/MainPage.swift#L557) — `ToolbarItem(placement: .principal) { addressBarView }`：地址栏挂载的 placement，是本次问题的直接根源。
- [MainPage.swift:422](../../../macos/kepler/Kepler/Pages/MainPage.swift#L422) — `addressBarView` 结尾的 `.frame(maxWidth: .infinity, alignment: .leading)`：试图让地址栏铺满剩余空间的手段，但该修饰符作用于 SwiftUI 视图树内部，无法让 `NSToolbarItem` 本身向 AppKit 工具栏布局系统申请更多空间。
- [research.md §9](../../../specs/008-no-sandbox-address-bar/research.md#9-地址栏铺满剩余空间的实现机制fr-004-修订) — 本特性第二轮 Phase 0 研究中记录的技术决策："`.principal` placement 映射为 NSToolbar 的弹性空间区域"——**该假设本身就是本次 bug 的根因**，详见下方。
- `MainPage.swift` 中的 `FlexibleToolbarItemBridge`（`NSViewRepresentable`，`/speckit-bug-fix` 第一轮引入）— **已验证无效**，事后修改 SwiftUI 自动创建的 `NSToolbarItem` 的 `minSize`/`maxSize` 不会产生弹性伸缩效果，只会把 `minSize` 当作固定尺寸。详见下方"第一次修复尝试：已验证失败"。

## Root Cause Hypothesis

**置信度：高（high，2026-09-04 复测后由"中高"上调）**——根因判断最初基于对 SwiftUI/AppKit 工具栏 API 已知行为的分析，现已通过实机截图直接复现确认（见上方 Reproduction 的复测记录），与用户描述的两个症状（居中 + 仅随内容变宽）完全吻合，且能解释"移除旧标题前地址栏原本也是这个 placement"这一事实。

`research.md §9` 在第二轮实现前做出的技术决策是错误的：SwiftUI 的 `ToolbarItemPlacement.principal` 在 macOS 上的语义是"把这个视图放进工具栏**居中**的主内容槽位"，而**不是**"把这个槽位的宽度绑定为 leading 与 trailing 分组之间的全部剩余空间"。`.principal` 项的 `NSToolbarItem` 宽度仍由 AppKit 按其承载视图的 intrinsic/fitting size 决定——这正是移除前的旧标题 `Text(principalTitle)` 表现为"居中的一小段文字"而非"铺满剩余空间"的原因（旧代码注释写的是"centre"即居中，而非"fill"）。第二轮把地址栏塞进同一个 `.principal` 槽位后，继承了同样的"内容驱动宽度、居中放置"行为；视图内部追加的 `.frame(maxWidth: .infinity, alignment: .leading)` 只能在 `NSToolbarItem` 已经分配好的（content-sized）盒子内部生效，而这个盒子本身从未变大过，因此该修饰符实际上是空转的（inert）。

这与 Apple 自家实现形成印证：Finder/Xcode 等系统应用中真正"随窗口宽度伸缩"的搜索框使用的是专用的 `NSSearchToolbarItem`（一个具有可配置 `minSize`/`maxSize` 的原生 AppKit 类），而不是把任意 SwiftUI 视图塞进 `.principal`——如果纯 SwiftUI 的 `.principal` 天然支持弹性宽度，Apple 就没有必要为搜索框单独设计一个专用类型。这进一步印证"纯 SwiftUI `.toolbar` API 不提供开箱即用的弹性宽度工具栏项"这一判断。

## 第一次修复尝试：已验证失败（2026-09-04）

用户已批准 AppKit 桥接（见 Open Questions 历史），`/speckit-bug-fix` 据此实现了 `FlexibleToolbarItemBridge`（`macos/kepler/Kepler/Pages/MainPage.swift`）：一个 `NSViewRepresentable`，在运行时从 `window.toolbar.items` 中找到地址栏对应的（由 SwiftUI 自动创建的）`NSToolbarItem`，事后设置其 `minSize`/`maxSize`，并降低宿主视图的 content-hugging/compression-resistance 优先级。详见 [fix.md](./fix.md)。

**该修复不生效**——2026-09-04 复测截图证实：地址栏宽度在 900pt 与 1400pt 窗口下均恒定在约 150pt（精确对应代码中设的 `minSize.width = 160`），且始终居中。也就是说 `minSize` 被 AppKit 当作了"这项内容的固定尺寸"而非"下限"，`maxSize` 与 hugging/compression-resistance 优先级均未产生任何弹性增长效果。这与该修复自己在运行时触发的系统诊断一致：`NSToolbarItem.minSize and NSToolbarItem.maxSize methods are deprecated... recommended to let the system measure the item automatically using constraints`——**"事后修改一个由 SwiftUI 自动创建的 `NSToolbarItem` 的尺寸属性"这条路径本身在现代统一工具栏（unified toolbar）下已被证实不可行**，不只是"有裁切风险"，而是完全不产生弹性效果。

这排除了 assessment.md 原 Proposed Remediation 中"轻量事后桥接"的做法，将根因判断进一步收窄：问题不在于"要不要用 AppKit"（已经在用了），而在于"必须在 `NSToolbarItem` **创建时**就用正确的方式声明其可伸缩性，而不能在 SwiftUI 已经创建好一个内容驱动尺寸的 `NSToolbarItem` 之后再去修改它"。

## Proposed Remediation（2026-09-04 修订）

**Preferred（推荐）**：完全绕开 SwiftUI 的 `.toolbar { ToolbarItem(placement: .principal) { ... } }` 声明式 API 来创建地址栏这一项，改为自建一个真正的 `NSToolbarDelegate`（可以只接管这一个 identifier，其余项仍可尝试保留在 SwiftUI `.toolbar` 中，或视实现复杂度决定是否需要把全部工具栏项一并转为 delegate 管理——**具体范围留给 `/speckit-bug-fix` 在实现时根据实际约束判断，并在 fix.md 中如实记录**），在 `toolbar(_:itemForItemIdentifier:willBeInsertedIntoToolbar:)` 中构造一个自定义 `NSToolbarItem`：`view` 设为承载 `addressBarView` 内容的 `NSHostingView`，并在**创建时**而非事后设置 `minSize`/`maxSize`（或改用 `NSToolbarItem.isNavigational`/更现代的尺寸协商方式，具体以 `/speckit-bug-fix` 阶段针对当前 macOS 26 SDK 实测确认为准）。这是 Finder/Xcode 原生可伸缩搜索框的真实实现方式，也是唯一被验证过、在现代统一工具栏下能生效的路径。

**Alternatives**：

- **放弃使用系统 `NSToolbar`，改为窗口内自绘的工具栏行**——在 `PSMainPage` 的 body 顶部自建一个 `HStack`（导航按钮 + 地址栏 `.frame(maxWidth: .infinity)` + 右侧按钮组），完全脱离 `.toolbar` API。能 100% 保证获得 SwiftUI 原生 flex 布局能力，代价是失去系统 `NSToolbar` 的原生外观与自动溢出菜单（[007 research.md §9](../../../specs/007-vulcan-index-search/research.md#9-搜索工具栏布局不隐藏其它按钮2026-09-04-增量) 已利用的能力）。**在"完全替换 NSToolbarDelegate"方案也验证失败的情况下，这是保底方案。**
- **接受近似效果，放弃"连续伸缩"这一字面要求**——给地址栏设一个较宽的固定 `idealWidth`（如 400–500pt），不再追求随窗口连续变化。仅在用户认为"自绘工具栏行"或"完整 NSToolbarDelegate"的工程代价不值得时才考虑，需要用户明确放弃 FR-004 修订的字面表述。

**Files likely to change**：

- `macos/kepler/Kepler/Pages/MainPage.swift`
- 大概率需要新增一个独立文件（如 `AddressBarToolbarDelegate.swift`）承载 `NSToolbarDelegate` 实现——事后证明"塞进现有文件的轻量桥接"这条路走不通，真正的 delegate 实现体量更大，独立文件更利于审查

**Tests to add or update**：`macos/kepler` 当前无 XCTest target（[research.md §8](../../../specs/008-no-sandbox-address-bar/research.md#8-现有测试基线)），沿用既有约定；验证方式**不应再仅依赖构建成功或代码走查**——本次已证明"编译通过 + 运行时无崩溃 + 触发了预期的系统 API 调用"均不足以证明修复有效。修复后 MUST 用与本次相同的方法（`osascript`/System Events 精确控制窗口尺寸 + `screencapture -R` 裁剪截图 + 像素测量）实际验证地址栏宽度随窗口宽度连续变化，而非仅凭观感判断。

## Risks & Considerations

- 完整 `NSToolbarDelegate` 实现会引入 `NSHostingView` 生命周期管理、SwiftUI ↔ AppKit 状态双向绑定（`isAddressBarEditing`/`addressBarDraft`/`selectedFolderID` 等现有 `@State`）的额外复杂度，比已验证失败的"事后桥接"方案复杂得多，需要仔细设计避免引入新的状态不一致 bug。
- 若只接管地址栏这一个 identifier、其余项仍留在 SwiftUI `.toolbar` 中，需要验证"部分 SwiftUI 管理 + 部分 delegate 管理"的混合模式在 AppKit 层面是否被允许、是否会互相干扰（`/speckit-bug-fix` 阶段需要先验证这一可行性，若不可行则可能需要把全部工具栏项一并转为 delegate 管理，扩大改动范围——按宪章原则 V，扩大范围前须在 fix.md 中如实记录必要性）。
- 自绘工具栏行方案（保底选项）会影响窗口标题栏融合观感（`toolbarBackground`/`unified` 风格），且需要重新验证 007 特性已实现的搜索框展开/收起、溢出菜单等既有行为是否仍然成立。
- 无论选择哪个方案，都需要重新验证 007 引入的"点击空白处退出搜索"等既有交互（本特性第一轮已确认该交互随旧标题移除而自然失效，属已接受的范围）不会因本次改动产生新的副作用。
- **本次教训**：`/speckit-bug-fix` 此前仅凭"构建成功 + 运行时触发了预期的系统 API 调用（无崩溃）"就在 fix.md 中判定"applied"，但实际未生效——今后同类 UI 布局类 bug 的验证 MUST 以实际像素测量或用户目视确认为准，不能仅凭编译/运行时无异常作为完成依据。

## Open Questions

- ~~是否接受为达成"地址栏随窗口宽度连续伸缩"这一 spec 字面要求，引入 AppKit `NSToolbarItem`/`NSToolbarDelegate` 桥接？~~ **已解决**：用户已明确批准（"允许引入AppKit桥接方案"）。
- [NEEDS CLARIFICATION: 若"完整 NSToolbarDelegate 替换"方案在 `/speckit-bug-fix` 阶段验证后仍不能达成连续弹性伸缩效果，是否接受退回"自绘工具栏行"这一更大的架构改动（会影响 007 特性已实现的搜索框展开/溢出菜单行为，需要一并重新验证），还是接受"较宽固定宽度"的降级近似方案？——此问题暂不阻塞下一轮 `/speckit-bug-fix` 尝试"完整 NSToolbarDelegate 替换"，仅在该方案也失败时才需要用户决策。]
