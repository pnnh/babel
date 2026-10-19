# Contract: QML ↔ C++ 浮窗会话接口（002 变更增量）

**Spec**: [spec.md](../spec.md) ｜ **Data Model**: [data-model.md](../data-model.md) ｜ **基线**: [001 契约](../../001-floating-ime-converter/contracts/qml-cpp-session-interface.md)

## 适用范围与阅读方式

本文件**不是**对 001 `contracts/qml-cpp-session-interface.md` 的整体重写，而是记录本特性对该契约的**增量变更**（新增属性/方法/信号，以及对既有条目语义的必要澄清）。001 原文件中未被下表提及的属性/方法/信号（`rawInputSlotText`、`outputPreviewText`、`outputPreviewState`、`learningTipText`、`learningTipVisible`、`manualPasteHintVisible`、`submitKeystrokes()`、`confirm()`、`cancel()`、`sessionInvoked()`、`sessionClosed()`）**原样有效，未变更**。

---

## 一、既有属性的语义澄清（签名不变）

| 属性 | 变更前（001） | 变更后（本特性） |
|------|----------------|-------------------|
| `modelAvailable` | 本地 LLM 模型文件缺失/损坏/无法加载时为 `false` | 语义不变；新增触发场景——`switchModel()` 失败且已回退至所有已知模型仍不可用时同样为 `false`（FR-013 终止条件） |
| `rawInputSlotText` | 展示 `NormalizedCandidateText.content`（单一候选字符串） | **取值来源变更，展示内容的外部可观察效果不变**：由 `data-model.md` §1 `PinyinLattice.toDisplayText()` 派生，等价于"各格位最高权重候选拼接"；QML 侧无需、也不应感知候选集合存在（FR-008） |

## 二、新增属性

| 属性 | 类型 | 对应 spec 条款 | 说明 |
|------|------|----------------|------|
| `currentModelName` | string | FR-009、FR-014 | 当前慢流实际加载模型的文件名（不含路径），供设置页面与（可选）主视图展示 |
| `modelSwitching` | bool | FR-014 | `true` 时表示推理引擎正在卸载旧模型/加载新模型；此状态下慢流生成请求 MUST NOT 被触发（`SessionController` 内部据此暂缓 `onSlowPathTriggered`），QML 侧 SHOULD 展示"模型加载中，请稍候" |
| `availableModels` | list（元素含 `filePath`/`fileName`/`sizeBytes`/`tierHint`/`availability`，见 `data-model.md` §6） | FR-011 | 约定本地模型目录下扫描到的全部模型文件条目 |
| `recommendedProfiles` | list（元素含 `tier`/`modelName`/`quantization`/`approxSizeDescription`/`targetHardwareDescription`，见 `data-model.md` §7） | FR-012 | 固定三档推荐模型说明性数据，供设置页面展示 |
| `fallbackNoticeText` | string | FR-013 | 最近一次触发自动回退时的提示文案；为空字符串时 QML 不展示该提示 |
| `settingsVisible` | bool | FR-010 | 控制 `research.md` §11 所述"同一 Window 内视图切换"的当前视图（`true` = 展示设置视图，`false` = 展示主视图） |

## 三、新增可调用方法（Invokable Methods）

| 方法 | 参数 | 对应 spec 条款 | 说明 |
|------|------|----------------|------|
| `openSettings()` | 无 | FR-010 | 浮窗右上角设置按钮触发；置 `settingsVisible = true` |
| `closeSettings()` | 无 | FR-010（隐含的"返回"操作，未改变 spec 对外行为契约，仅为 `research.md` §11 所选"同一 Window 内视图切换"UI 方案所必需的配套操作） | 置 `settingsVisible = false`，回到主视图；MUST NOT 影响 `rawInputSlotText`/`outputPreviewText` 等既有会话数据（FR-014："既有会话状态…均 MUST 保持不受影响"） |
| `switchModel(filePath: string)` | 目标模型文件完整路径（取自 `availableModels` 某一元素的 `filePath`） | FR-011、FR-014 | 触发 `research.md` §8 描述的热切换流程；调用后立即置 `modelSwitching = true`，流程结束后置回 `false` 并更新 `currentModelName`/`modelAvailable`；若失败则按 FR-013 执行级联回退并更新 `fallbackNoticeText` |

## 四、新增信号（Signals）

| 信号 | 参数 | 对应 spec 条款 | 说明 |
|------|------|----------------|------|
| `modelSwitchCompleted(success: bool, modelName: string)` | 是否成功、最终生效的模型名称 | FR-014 | `switchModel()` 流程结束时发出，供 QML 侧播放"加载完成"提示（如有）或仅用于测试断言，不强制要求 QML 必须监听 |
| `modelFallbackTriggered(noticeText: string)` | 提示文案 | FR-013 | 自动回退发生时发出，`noticeText` 即为 FR-013 原文模板渲染结果 |

## 五、前置/后置条件补充

- `switchModel()` 的前置条件：`modelSwitching == false`（若已在切换中再次调用，MUST 被忽略或排队，具体选择属内部实现细节，不改变外部契约——"同一时刻只会有一次切换在进行"这一约束本身是外部可观察的，MUST 遵守）。
- `switchModel()` 的后置条件：无论成功或经级联回退后成功，`modelAvailable` 的最终取值 MUST 准确反映"当前 `currentModelName` 对应的模型此刻是否真的可用于生成"；仅当回退链路中所有候选（含低配默认模型）均不可用时，`modelAvailable` 才为 `false`（与 001 `ModelUnavailable` 会话态触发条件一致）。
- `closeSettings()`/`openSettings()` 均 MUST NOT 产生任何剪贴板写入、粘贴注入或模型切换的副作用（与设置页面导航无关的既有行为边界保持隔离）。

## 六、本契约已回填的事项（`research.md` §12 用户委派）

- `availableModels` 中每个条目的 `availability` 字段何时标记为 `KnownUnloadable`（`research.md` §7.3，已决策）：`ModelCatalog::scan()` 对每个文件读取头部 4 字节并校验是否匹配 GGUF 魔数 `"GGUF"`；不匹配即扫描阶段直接标记为 `KnownUnloadable`；匹配则为 `Unverified`，待实际加载后更新为 `Verified` 或 `KnownUnloadable`。标记为 `KnownUnloadable` **不影响**其在 `availableModels` 列表中的可点选性——设置页面附加"不可用"视觉标记（置灰+提示图标）但仍允许选中，选中后触发既有 `switchModel()` 流程并按 FR-013 回退链路处理。
- `switchModel()` 是否需要在调用前对目标设备硬件能力做检测性校验（`research.md` §7.4，已决策）：**不需要**。`switchModel()` 对任意 `availableModels` 条目一视同仁地尝试加载，不新增任何跨平台硬件能力（可用内存/显存）检测逻辑；加载失败时完全复用既有 FR-013 自动回退+提示链路（`modelFallbackTriggered` 信号），不追加额外的前置拦截或硬件适配性提示文案。
- 应用重启后 `currentModelName`/`availableModels` 的初始值是否反映"用户上次会话的选择"（`research.md` §9，已决策）：**不反映**。`data-model.md` §8 `ActiveModelRuntimeState` 为纯进程内存状态，不持久化到磁盘；每次应用启动后 `currentModelName` 固定初始化为低配默认模型（`ModelPaths::conventionalModelFilePath()` 对应的文件名），`availableModels` 则每次启动重新扫描约定目录产出，与"记住用户上次选择"无关，行为与 001 既有"固定默认模型启动"完全一致。
