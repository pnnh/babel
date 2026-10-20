# Quickstart: 模型档位调整、GBNF 语法约束解码与防抖节奏优化 验证指南

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md) ｜ **Data Model**: [data-model.md](./data-model.md) ｜ **Contracts**: [contracts/](./contracts/)

本指南用于在 `/speckit.implement` 完成本特性对应的 `tasks.md` 任务后，手工验证端到端行为是否满足 `spec.md` 的验收场景与成功标准。本文件延续 001/002 `quickstart.md` 的既有验证场景（核心闭环、手动粘贴降级、异常提示、Lattice 多候选消歧、模型热切换）作为回归基线，本文件**仅新增**本特性引入的验证场景，**不重复**001/002 已覆盖的内容。

## 前置条件

- `research.md` 中记录的全部 Decision 已被 `/speckit.implement` 落地：`ModelCatalog::recommendedProfiles()` 返回新档位数值、`classifyTierByFileName()` 关键字表已更新、`LlmEngine::generate()` 已接入 GBNF 语法采样、`DebounceScheduler::setDebounceWindow()` 已被 `SessionController` 在启动与模型切换完成后调用。
- `research.md` §5 记录的 `Unknown` 档位防抖窗口数值已由用户决策确定为复用低配 200ms（采纳方案①/Recommendation），场景二新增 2.6 校验该分支。
- 已按 `research.md` §6 的新数值，由用户手动下载 Qwen2.5-7B-Instruct（GGUF Q4_K_M）与 Qwen2.5-14B-Instruct（GGUF Q4_K_M）两个模型文件，放置于约定本地模型目录（`ModelPaths::conventionalModelDirectory()`），文件名建议包含 `7b`/`14b` 关键字（如 `qwen2.5-7b-instruct-q4_k_m.gguf`/`qwen2.5-14b-instruct-q4_k_m.gguf`），以便被 `ModelCatalog::scan()` 正确分类为 `Medium`/`High`。
- 构建方式延续既有 CMake Presets 工作流（未因本特性改变构建入口）：

```bash
# macOS
cmake --preset macos
cmake --build --preset macos
```

- 单元测试运行方式延续既有约定（`tests/CMakeLists.txt` 已配置 POST_BUILD 自动 ad-hoc codesign + `DISCOVERY_MODE PRE_TEST`，macOS 沙箱环境下可直接运行）：

```bash
ctest --preset macos --output-on-failure
```

---

## 验证场景一：设置页面展示调整后的推荐档位数值（User Story 1 的 Acceptance Scenario 1）

1. 唤起浮窗（默认全局热键），点击右上角设置按钮进入设置视图。
2. 查看"中配"推荐条目。
   - **预期**：展示型号为 `Qwen2.5-7B-Instruct`（GGUF Q4_K_M），而非 002 原有的 `Qwen2.5-14B-Instruct`（FR-001）。
3. 查看"高配"推荐条目。
   - **预期**：展示型号为 `Qwen2.5-14B-Instruct`（GGUF Q4_K_M），而非 002 原有的 `Qwen2.5-32B-Instruct`（FR-002）。
4. 若此前已按 002 旧版推荐下载过 `Qwen2.5-32B-Instruct` 文件并放置于约定目录，查看模型列表。
   - **预期**：该文件仍出现在可选模型列表中，可被选中切换，仅不再标记为官方推荐项（FR-004）。

---

## 验证场景二：切换到中/高配模型后防抖窗口变化的体感验证（User Story 1 的 Acceptance Scenario 3/4，对应 SC-003）

**目的**：验证 FR-010～FR-014——防抖窗口随当前选定模型档位动态变化，且状态机迁移逻辑本身不受影响。

### 2.1 低配默认档位（基线，200ms）

1. 应用启动后（未做任何手动模型切换），在浮窗中连续击键，每次击键间隔约 150ms（略短于 200ms 窗口），持续输入一段较长文本（如逐字敲完"wo jin tian qu chao shi mai le hen duo dong xi"）。
   - **预期**：由于每次击键间隔小于 200ms 窗口，防抖被持续重置，期间不触发生成（`outputPreviewState` 保持 `Debouncing`），直至停止击键满 200ms 后才触发一次生成（`outputPreviewState` 变为 `Generating`）。

### 2.2 切换至中配档位（500ms）

2. 进入设置页面，选择"中配"推荐模型条目（或文件名含 `7b` 关键字的已下载模型），点击切换。
   - **预期**：`modelSwitching` 短暂变为 `true` 后恢复 `false`，`currentModelName` 更新为所选模型文件名（契约见 `contracts/qml-cpp-session-interface.md`，002 既有行为不变）。
3. 返回主视图，重复步骤 1 的"间隔约 150ms 持续击键"输入节奏。
   - **预期**：相较 2.1，防抖触发所需的"停止击键后的静默时长"变为约 500ms；若在步骤 1 使用的"间隔 150ms"节奏下持续击键超过 350ms（如从 200ms 延长到 400ms 的停顿），此时低配窗口（200ms）下本应已触发生成，而中配窗口（500ms）下应仍处于 `Debouncing`、尚未触发——以此体感差异验证窗口确已拉长（FR-012 中配 500ms）。
4. 完全停止击键并等待超过 500ms。
   - **预期**：触发一次生成，`outputPreviewState` 变为 `Generating`。

### 2.3 切换至高配档位（800ms）

5. 进入设置页面，选择"高配"推荐模型条目（或文件名含 `14b` 关键字的已下载模型），点击切换。
6. 重复步骤 3 的间隔击键验证，这次以"停顿 600ms"（超过中配 500ms 但未达高配 800ms）作为中间检查点。
   - **预期**：停顿 600ms 时仍处于 `Debouncing`（尚未达到 800ms），需等到停顿满 800ms 后才触发生成（FR-012 高配 800ms）。

### 2.4 切回低配默认档位（验证 FR-011"不受本特性调整影响"）

7. 切换回低配默认模型（文件名含 `3b`）。
8. 重复步骤 1 的验证。
   - **预期**：防抖窗口恢复为 200ms，与本特性交付前的既有体验一致（FR-011/SC-005）。

### 2.5 并发保护逻辑未被破坏的回归检查（FR-013）

9. 在中配或高配档位下，于一次生成仍在进行中（`outputPreviewState` 为 `Generating`）时继续输入。
   - **预期**：状态迁移为 `Stale`，且在该次生成真正完成（`markGenerationComplete()`）之前，即便新的防抖窗口已到期也 MUST NOT 触发第二次生成（与 002 既有行为完全一致，仅等待时长数值不同）。

### 2.6 `Unknown` 档位（自定义命名模型）复用低配 200ms 的验证（用户决策，`research.md` §5）

10. 将一个已下载的模型文件重命名为不含 `"3b"`/`"7b"`/`"14b"`/`"32b"` 任何关键字的自定义名称（如 `my-custom-model.gguf`），放置于约定本地模型目录后重新扫描模型列表并切换至该文件。
    - **预期**：`classifyTierByFileName()` 判定为 `Unknown`，`currentModelName` 正常更新为该自定义文件名（档位识别失败不影响模型本身的加载与使用，FR-004 既有行为延续）。
11. 重复步骤 1 的"间隔约 150ms 持续击键"输入节奏。
    - **预期**：防抖窗口表现与 2.1 低配基线一致（约 200ms 静默后触发生成），与 2.2/2.3 中配/高配观察到的 500ms/800ms 延长效果形成对比，验证 `Unknown` 档位复用低配 200ms 数值（用户决策，`research.md` §5）。

---

## 验证场景三：构造易触发标注符号泄漏的歧义输入，验证 GBNF 约束后最终文本不含标注符号（User Story 2，对应 SC-004）

**目的**：验证 FR-005～FR-008——GBNF 语法约束从结构上强制排除 `⟦`、`⟧`、`|` 三个 Lattice 内部标注符号出现在最终生成文本中，即便输入 prompt 本身包含大量这类标注片段（易诱导模型"模仿"输出这些符号）。

### 3.1 构造高歧义密度测试语料

1. 准备一组包含多个同音字歧义位置的拼音混输文本，确保 `FastCorrector::buildLattice()` 会为多个 token 产出"存在真实歧义"的候选格位（即 `PinyinLattice::toAnnotatedPromptText()` 产出的标注文本中包含较多个 `⟦候选1 权重1|候选2 权重2|...⟧` 片段）。可复用/扩展 002 既有测试样例，例如包含 `chi`（吃/持/迟/池/尺/齿）、`shi`（是/事/时/市/使/式等）等高歧义拼音的连续长句。
2. 在浮窗中逐字输入该测试语料，等待慢流生成完成。

### 3.2 验证最终上屏文本不含标注符号

3. 对生成的中文输出预览文本逐字符检查。
   - **预期**：输出文本中**不出现** `⟦`（U+27E6）、`⟧`（U+27E7）、`|`（U+007C）任意一个字符（FR-005/FR-007）。
4. 按回车确认该输出，验证剪贴板写入内容与模拟粘贴到目标应用后的最终文本。
   - **预期**：目标应用中实际落地的文本同样不含上述三个标注符号（Acceptance Scenario 2，对应 002 既有"剪贴板写入+模拟粘贴"链路，未因本特性改变）。
5. 针对合法但"看起来特殊"的内容（如翻译结果中包含英文品牌名"iPhone"、技术缩写"GPT"、emoji）构造额外测试样例。
   - **预期**：这些合法字符**正常出现**在输出中，不被语法约束误伐（FR-007"最宽边界"要求，对应 `contracts/llm-engine-grammar-internal-contract.md` 第三节）。

### 3.3 语法梗阻场景的行为验证（FR-008，较难稳定复现，可选/尽力而为）

6. 若在步骤 2 的重复测试中观察到某次生成结果明显短于预期（疑似提前终止），检查该次生成是否仍产出了非空的部分内容、且应用未崩溃/未报错、未向用户展示任何"生成失败，请重试"的提示。
   - **预期**：即便发生语法梗阻提前终止，用户体验上仅表现为"本次生成内容比预期短"，不出现错误提示或要求重试（FR-008）；`outputPreviewState` 正常迁移至 `Ready`（而非任何错误态）。

---

## 覆盖对照表

| Spec 条款 | 本文件对应场景 |
|---|---|
| FR-001/FR-002/FR-004 | 场景一 |
| FR-010～FR-014 | 场景二 |
| FR-005～FR-009 | 场景三 |
| SC-001/SC-002（延迟降低，需配合 `/speckit.implement` 后的实测数据，非本文手工场景可直接验证，留待实现阶段记录） | 不在本文件范围，见 `research.md` 结论汇总 |
| SC-003 | 场景二 |
| SC-004 | 场景三 |
| SC-005 | 场景二 2.4 |
