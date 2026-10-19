# Data Model: 拼音多候选格架构、真实词库与可切换本地模型

**Spec**: [spec.md](./spec.md) ｜ **Plan**: [plan.md](./plan.md) ｜ **Research**: [research.md](./research.md)

## 阅读说明

本文件仅记录**本特性新增/修改**的实体，001 `data-model.md` 中未被本特性声明取代的实体（`RawDraftText`、`TargetFocusContext`、`LearningTip`、`FloatingSessionState`）原样沿用，不重复列出。`StandardOutputPreview`/`GenerationState` 不受本特性影响（慢流生成状态迁移语义不变，仅生成**输入内容**的组织形式变化，见 §1）。

涉及 `research.md` §12 原标记 `[NEEDS CLARIFICATION]` 的 7 项，用户已在本轮对话中明确表态"全部委托 Agent 按 research.md 中各自的 Recommendation 执行"（详见 `research.md` §12、`plan.md` Constitution Check"用户委派及 Agent 实际选择"一节），本文件已据此将下述对应字段/小节回填为具体取值/行为（不再使用"行为留待决策"措辞）。

---

## 一、Lattice 多候选格相关实体（取代 001 §2 `NormalizedCandidateText` 的单一候选设计）

### 1. CandidateEntry（候选条目）

单个候选汉字/词语及其权重信息，是 `PinyinLatticeCell` 的组成单元。

| 字段 | 类型 | 说明 |
|------|------|------|
| surfaceForm | string | 候选的目标词形（中文词或英文词），语义同 001 `DictionaryEntry.surfaceForm` |
| rawWeight | number | 候选来源的原始权重（词库 `frequencyWeight` 或编辑距离回退匹配的加权分值），未归一化 |
| normalizedWeight | number (0.0–1.0) | 同一格位内**截断至规模上限后**（见下方 `PinyinLatticeCell.candidates` 规模上限说明），按 `rawWeight / Σ截断后候选 rawWeight` 归一化后的比例值，供 `research.md` §5.3 结构化标注文本使用（避免原始权重量级差异过大导致 prompt 不可比） |
| source | enum: `FullPinyinTrie` \| `AbbrevTrie` \| `EditDistanceFallback` \| `Passthrough` | 候选来源，供 §6 开发诊断日志与后续效果分析使用；不向 QML/用户暴露 |

### 2. PinyinLatticeCell（拼音候选格位）

对应原始输入中以空白分隔的一个 token 位置，取代 001 `NormalizedCandidateText` 中"每个拼音位置仅承载单一候选字符串"的设计（spec FR-006）。

| 字段 | 类型 | 说明 |
|------|------|------|
| tokenIndex | int | 格位在原始输入中的序号（从 0 开始），供诊断日志标注位置 |
| originalToken | string | 用户原始击键产生的该 token 文本（拼音片段，或已是中文/标点的透传文本） |
| candidates | `CandidateEntry[]` | 按 `rawWeight` 降序排列、**已截断**的候选列表；`originalToken` 非拼音（透传格位）时恒为长度 1 的列表，`source = Passthrough`。**候选规模上限（已决策，`research.md` §5.4，用户本轮委派，Agent 采纳 Recommendation 方案 2）**：每格位最多保留 `rawWeight` 降序排列的前 **K = 8** 个候选（实测前起点，待 `/speckit.implement` 阶段结合 SC-003 实测结果调参），截断发生在 `normalizedWeight` 计算**之前**（即归一化基数为截断后的候选子集，而非截断前的全集） |
| isAmbiguous | bool（派生字段，`candidates.length >= 2`） | 供 §6 诊断日志与 `research.md` §5.3 标注文本渲染判断是否需要输出标记 |

### 3. PinyinLattice（拼音候选格序列）

一次 `submitKeystrokes()` 调用对应的完整格位序列，是 `FastCorrector` 新增对外产出物，取代 001 `NormalizedCandidateText` 作为快流处理结果的主要载体。

| 字段 | 类型 | 说明 |
|------|------|------|
| cells | `PinyinLatticeCell[]` | 按原始输入 token 顺序排列 |
| builtAt | timestamp | 构建时间，延续 001 `NormalizedCandidateText.updatedAt` 的"校验 SC-002 延迟"用途 |

**对外渲染方法（非持久化字段，行为契约）**：

- `toDisplayText() -> string`：取每个格位 `candidates.front().surfaceForm` 拼接（与透传 token 的拼接空格规则沿用 001 `FastCorrector::correct()` 既有的"相邻英文 token 间保留空格"逻辑），**对外行为等价于 001 `NormalizedCandidateText.content`**——供 `SessionController::rawInputSlotText` 属性延续展示，不因本特性引入 Lattice 而改变 QML 侧可观察内容（FR-008："MUST NOT 向用户呈现候选集合及其权重"）。
- `toAnnotatedPromptText() -> string`：按 `research.md` §5.3 的 `⟦token:候选 权重|...⟧` 格式渲染，仅对 `isAmbiguous == true` 的格位输出标记，供 `LlmEngine::generate()` 新输入内容使用（FR-007）。

**与 `NormalizedCandidateText` 的关系**（`babel/src/session/DraftTypes.h`，最小必要修改，宪章原则 V）：`NormalizedCandidateText` 结构体**保留** `content`/`updatedAt` 两个既有字段（继续服务 `rawInputSlotText` 既有 QML 契约，不破坏 001 外部行为），新增一个 `lattice: PinyinLattice` 字段承载本特性新增的多候选信息；`content` 字段的取值来源由"快流直接产出单一字符串"改为"由 `lattice.toDisplayText()` 派生"，对 QML 层不可感知差异。

---

## 二、真实词库数据相关实体

### 4. RimeDictionarySourceFile（词库源文件加载记录，内部诊断用途）

扫描约定词库目录（见 `research.md` §3/§7.1 的目录约定设计）时，每个被发现的 `.dict.yaml` 文件对应一条记录，供诊断日志使用；**行为已决策**（用户本轮委派，Agent 采纳 `research.md` §4.1 Recommendation：方案 3）：解析失败时仅跳过该文件、写入诊断日志（`spdlog::warn`），不中断目录下其余文件的加载，不新增用户可见提示 UI。

| 字段 | 类型 | 说明 |
|------|------|------|
| filePath | path | 文件完整路径 |
| frontmatterName | string \| null | frontmatter `name:` 字段值（若存在），仅用于诊断日志标识来源，不参与候选合并逻辑 |
| entryCount | int | 该文件贡献的词条数（解析成功时） |
| loadStatus | enum: `Loaded` \| `Skipped` \| `Aborted` | **已决策**（`research.md` §4.1，方案 3）：`.dict.yaml` 解析成功取 `Loaded`；解析失败（格式不符合规范）取 `Skipped`——该文件被跳过且目录下其余文件继续正常加载，仅写入诊断日志，不展示用户可见提示；`Aborted` 枚举值本特性**不使用**（已排除"中断整个词库加载"的方案 2），保留该枚举值仅为与可能的未来扩展保持结构兼容，当前实现路径中不会产生该取值 |

### 5. DictionaryEntry（快流词典条目，沿用 001，本特性扩展数据来源）

001 `data-model.md` §7 已定义的结构**字段不变**（`entryId`/`surfaceForm`/`pinyinFull`/`pinyinAbbrev`/`frequencyWeight`），持久化方式不变（既有 sqlite3 `dictionary_entry` 表，`DictionarySchema.cpp`）。本特性仅扩展其**数据来源**：从仅 `builtinSeedDictionary()`（14 条）扩展为"`RimeDictLoader` 解析约定词库目录下全部 `.dict.yaml` 文件产出的条目" ∪ `builtinSeedDictionary()`（后者继续作为目录为空/全部解析失败时的最后兜底，内部细节，宪章原则 IV）。

**细胞词库合并时的权重取舍规则（已决策）**：`research.md` §4.2 已由用户本轮委派、Agent 采纳 Recommendation 方案 1——复用 `insertDictionaryEntries()` 既有 `INSERT OR IGNORE` 语义不变，`RimeDictLoader` 按约定目录内文件名字典序依次加载并写入，先加载文件中的词条权重生效，后加载文件中的重复词条（相同 `surface_form`+`pinyin_full`）被忽略。

---

## 三、本地模型切换相关实体

### 6. ModelDescriptor（本地模型目录条目）

对应 spec Key Entities"本地模型目录条目"，`ModelCatalog::scan()`（`research.md` §7.1）产出的列表元素，供设置页面展示。

| 字段 | 类型 | 说明 |
|------|------|------|
| filePath | path | 模型文件完整路径（`ModelPaths::conventionalModelDirectory()` 下的 `*.gguf` 文件） |
| fileName | string | 文件名（展示用） |
| sizeBytes | int64 | 文件体积 |
| tierHint | enum: `Low` \| `Medium` \| `High` \| `Unknown` | `research.md` §7.2 的文件名启发式分类结果，仅供展示分组，不限制可选性 |
| availability | enum: `Unverified` \| `Verified` \| `KnownUnloadable` | **已决策**（`research.md` §7.3，用户本轮委派，Agent 采纳 Recommendation 方案 3）：`ModelCatalog::scan()` 对每个文件读取头部 4 字节并与 GGUF 魔数 `"GGUF"` 比对——不匹配（文件损坏/非 GGUF 格式）即取 `KnownUnloadable`，设置页面对该条目附加"不可用"视觉标记（置灰+提示图标），仍可被选中以触发 FR-013 回退链路；匹配则取 `Unverified`，正常展示；真正尝试加载后，成功更新为 `Verified`，失败（如量化格式不受当前引擎支持等头部校验无法检测的问题）更新为 `KnownUnloadable` 并追加标记 |

### 7. RecommendedModelProfile（推荐档位信息，静态数据，非扫描结果）

对应 FR-012 三档推荐信息展示，内容为**固定的说明性数据**（非用户数据，不持久化）：

**用户选择超出硬件能力档位模型时的限制策略（已决策，`research.md` §7.4，用户本轮委派，Agent 采纳 Recommendation 方案 2）**：设置页面 MUST NOT 对任何档位的模型文件做选择前拦截，不引入跨平台硬件能力（可用内存/显存）检测；`ModelDescriptor`/`RecommendedModelProfile` 均不新增"是否超出当前设备能力"相关字段。用户选择任意档位后，`switchModel()` 一视同仁地尝试加载，若加载失败则完全复用 FR-013 已确定的自动回退+提示链路（`ActiveModelRuntimeState.fallbackHistory`/`lastFallbackNotice`），不追加额外的硬件适配性说明文案。

| 字段 | 类型 | 说明 |
|------|------|------|
| tier | enum: `Low` \| `Medium` \| `High` | — |
| modelName | string | 如 `"Qwen2.5-3B-Instruct"` |
| quantization | string | 固定 `"GGUF Q4_K_M"` |
| approxSizeDescription | string | 如 `"约 9GB"`（中配）/`"约 19.85GB"`（高配），取自 spec FR-012 原文数值 |
| targetHardwareDescription | string | 如 `"Apple M1 Pro + 32GB 统一内存 / Windows + RTX 4090 24GB 显存"`（高配档） |

该三条记录直接来自 spec FR-012 已锁定的文字内容，不涉及任何本阶段新决策。

### 8. ActiveModelRuntimeState（当前模型运行时状态）

承载 FR-013（级联回退）、FR-014（热切换）所需的运行时状态，`SessionController` 持有，`research.md` §8 并发安全设计描述其生命周期管理方式。

| 字段 | 类型 | 说明 |
|------|------|------|
| currentModelPath | path | 当前 `LlmEngine` 实际加载的模型文件路径 |
| isSwitching | bool | 对应 QML 新增 `modelSwitching` 属性，`true` 时慢流功能暂不可用（FR-014），QML 展示"模型加载中，请稍候" |
| fallbackHistory | path[] | 本次应用运行期间按时间顺序记录的"已知成功加载过的模型路径"栈，供 FR-013 级联回退时依次尝试；**仅为进程内存状态**（见下方"持久化范围声明"） |
| lastFallbackNotice | string \| null | 最近一次触发回退时，按 FR-013 原文模板渲染的提示文案（"模型 {} 当前不可用，已自动切换回 {} 继续使用"），供 QML 展示一次性提示 |

**持久化范围声明（已决策，呼应 `research.md` §9，用户本轮委派，Agent 采纳 Recommendation 方案 1）**：本实体的全部字段**仅存在于应用进程运行期间的内存中**，应用退出后不写入任何磁盘配置文件；下次应用启动后 `currentModelPath` 重新初始化为 `ModelPaths::conventionalModelFilePath()`（001 既有低配默认模型，行为与 001 完全一致）。**最终选型：不持久化**——用户的模型选择不跨应用重启记忆，每次启动固定使用低配默认模型，仅覆盖 spec 字面明确要求的"同一次应用运行期间热切换"范围（FR-014），不隐含地扩大该范围；若后续需要"记住用户选择"，需作为新的、需另行显式确认的需求提出，不在本特性范围内。

---

## 四、QML↔C++ 契约层面的新增属性/方法索引

详细签名与前置/后置条件见 `contracts/qml-cpp-session-interface.md`，此处仅索引对应实体：

| QML 可见属性/方法 | 对应实体 |
|---|---|
| `modelSwitching` (bool) | `ActiveModelRuntimeState.isSwitching` |
| `currentModelName` (string) | `ActiveModelRuntimeState.currentModelPath`（展示其文件名） |
| `availableModels` (list) | `ModelDescriptor[]`（`ModelCatalog::scan()` 结果） |
| `recommendedProfiles` (list) | `RecommendedModelProfile[]`（静态数据） |
| `switchModel(path)` (invokable) | 触发 `research.md` §8 描述的热切换流程 |
| `fallbackNoticeText` (string) | `ActiveModelRuntimeState.lastFallbackNotice` |

---

## 五、与 001 既有实体的差异对照表

| 001 实体/字段 | 本特性处理方式 |
|---|---|
| `NormalizedCandidateText.content`/`updatedAt` | 保留，字段语义不变；新增 `lattice` 字段（见 §1） |
| `DictionaryEntry` 全部字段 | 字段不变，仅扩展数据来源（见 §5） |
| `FloatingSessionState` 全部取值 | 不变；模型热切换的"暂不可用"语义由新增 `ActiveModelRuntimeState.isSwitching` 独立承载，不新增/修改 `FloatingSessionState` 枚举值（避免与既有"会话状态机"语义混淆——切换模型并不等同于"浮窗会话"本身状态迁移，二者是正交维度） |
| `ModelPaths.conventionalModelDirectory()`/`conventionalModelFilePath()` | 不变，直接复用（见 `research.md` §7.1） |
