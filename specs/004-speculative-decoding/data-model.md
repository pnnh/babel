# Data Model: 投机解码（Speculative Decoding）性能优化

**Spec**: [spec.md](./spec.md) ｜ **Research**: [research.md](./research.md)

## 阅读说明

本文件是 Phase 1 产出，记录本特性对既有实体（`specs/002-lattice-dict-model-switch/data-model.md`、`specs/003-model-tier-perf-tuning/data-model.md`）的**增量变更**。既有实体中未被下表提及的部分（`CandidateEntry`/`PinyinLatticeCell`/`PinyinLattice`/`RimeDictionarySourceFile`/`DictionaryEntry`/`ActiveModelRuntimeState`/`RecommendedModelProfile`（003 数值）/`TierDebounceProfile`）**原样有效，未变更**。

本文件中标注为"**【待决】**"的字段/取值，对应 `research.md` §9 汇总的两项本阶段新发现的事项（架构划分、搭配映射具体取值）；**用户已就这两项作出显式决策**（§3 采纳方案 B、§5.3 采纳方案①，见 `research.md` 对应"用户决策"小节），本文件已据此将原"【待决】"占位回填为最终值。

---

## 一、ModelDescriptor 扩展：草稿模型候选识别（结构不变，新增并列静态分类方法）

`ModelDescriptor` 结构体本身（`ModelCatalog.h`）**不变**，`TierHint`/`Availability` 两个既有枚举**不变**（`research.md` §5.1 核实结论：候选草稿文件名不会与既有四个目标档位关键字冲突，故无需修改 `TierHint` 或 `classifyTierByFileName()` 既有四个分支）。

### 1. DraftCandidate（新增枚举，独立于 TierHint）

```cpp
// ModelDescriptor 内新增的并列枚举（ModelCatalog.h）：
enum class DraftCandidate { None, Size0_5B, Size1_5B };
```

| 枚举值 | 对应文件名关键字（小写子串匹配，与 `classifyTierByFileName()` 同构，research.md §5.1） | 来源 |
|---|---|---|
| `Size0_5B` | `"0.5b"` / `"0-5b"` | FR-004（Qwen2.5-0.5B-Instruct 候选规格） |
| `Size1_5B` | `"1.5b"` / `"1-5b"` | FR-004（Qwen2.5-1.5B-Instruct 候选规格） |
| `None` | 均不匹配 | — |

`ModelCatalog::scan()` 既有行为**不变**：仍返回约定目录下全部 `*.gguf` 文件（含草稿模型文件），`availableModels` 契约内容不收窄（`research.md` §5.1，保持 002 `contracts/qml-cpp-session-interface.md` FR-011 既定契约）。`DraftCandidate` 分类结果**不**写入 `ModelDescriptor` 结构体本身（避免为所有扫描条目都新增一个多数情形下为 `None` 的字段），而是通过新增的独立静态方法按需查询（见下文"五、内部接口变更索引"）。

---

## 二、新增实体：RecommendedDraftModelProfile（推荐草稿模型规格，静态数据）

对应 spec Key Entities"草稿模型（Draft Model）"，与既有 `RecommendedModelProfile`（目标模型三档推荐）并列、结构同构但枚举类型独立（`research.md` §5.2：避免把"目标档位"与"草稿规格"两个维度混用到同一枚举）。

```cpp
struct RecommendedDraftModelProfile {
  ModelDescriptor::DraftCandidate size;
  QString modelName;              // "Qwen2.5-0.5B-Instruct" / "Qwen2.5-1.5B-Instruct"
  QString quantization;           // "GGUF Q4_K_M"（延续既有量化精度约定，FR-004 背景原文）
  QString approxSizeDescription;  // 具体数值留待 /speckit.tasks 阶段按实际 GGUF 文件大小填入，本阶段不编造
};
```

`ModelCatalog::recommendedDraftProfiles()` 固定返回两条静态数据（`Size0_5B`、`Size1_5B` 各一条），供设置页面 User Story 2 的"候选规格"展示需求（FR-004 Acceptance Scenario 1）。

---

## 三、新增实体：SpeculativeDecodingProfile（投机解码配置状态）

对应 spec Key Entities"投机解码配置（Speculative Decoding Profile）"，描述"目标模型档位 → 是否启用投机解码 → 对应草稿模型"的映射关系；spec.md 原文已明确"具体字段结构留待 `/speckit.plan` 阶段确定"，本节据此确定字段结构（取值本身部分**待决**，见下）。

```cpp
struct SpeculativeDecodingProfile {
  ModelDescriptor::TierHint targetTier;         // 仅 Medium / High 两个取值会出现在非空列表中（FR-005）
  bool eligible = false;                        // 该档位是否在"启用范围"内（FR-005：Low 恒为 false）
  ModelDescriptor::DraftCandidate pairedDraft;   // 该档位固定搭配的草稿规格（已决策，research.md §5.3：方案①）
  bool draftModelConfigured = false;            // 运行时判定：约定目录中是否存在与 pairedDraft 匹配的已下载文件
  bool speculativeDecodingActive = false;        // eligible && draftModelConfigured 的运算结果，决定本次会话是否实际走投机解码路径
};
```

**取值来源与计算规则**：

| 字段 | 计算方式 | 对应 FR |
|---|---|---|
| `eligible` | `targetTier == Medium \|\| targetTier == High` | FR-005 |
| `pairedDraft` | 固定查表（用户已决策，`research.md` §5.3 采纳方案①）：`targetTier == Medium → DraftCandidate::Size0_5B`（Qwen2.5-0.5B-Instruct）；`targetTier == High → DraftCandidate::Size1_5B`（Qwen2.5-1.5B-Instruct） | FR-007 |
| `draftModelConfigured` | 对 `ModelCatalog::scan()` 结果逐一调用新增的 `classifyDraftCandidateByFileName()`（见下文"五"），判断是否存在某一文件的分类结果等于 `pairedDraft` | FR-006 |
| `speculativeDecodingActive` | `eligible && draftModelConfigured`；`eligible == false`（低配档位）时恒为 `false`，不触发任何草稿模型加载尝试（FR-005/FR-009） | FR-005/FR-006 |

**生命周期**：与 `ActiveModelRuntimeState` 类似，为纯运行时内存状态，不持久化（延续 002 `research.md` §9"每次应用启动固定重新初始化"的既有实践）；每次 `switchModel()` 完成后（`applyModelSwitchResult()`）与应用启动时（`bootstrapPlatformDefaults()`）各重新计算一次（对应 spec.md Edge Cases"切换目标模型档位后应用 MUST 自动重新匹配新档位对应的默认草稿模型"）。

**具体宿主位置（架构已决策，research.md §3 采纳方案 B）**：该结构体的字段语义由 `LlmEngine` 内部持有/计算并据此决定是否启用投机解码路径，对 `SessionController` 完全透明——`SessionController` 不单独持有该结构体作为成员，仅在 `switchModel()`/`bootstrapPlatformDefaults()` 中查表得到 `pairedDraft` 对应的草稿模型路径后一并传给 `LlmEngine` 构造函数；`LlmEngine` 构造函数据此在内部计算 `draftModelConfigured`/`speculativeDecodingActive` 并决定走投机解码路径还是既有单模型路径（草稿模型缺失/加载失败时内部第二组 `llama_model*`/`llama_context*` 保持 `nullptr`，天然满足 FR-006"静默降级"）。`SessionController` 侧仅需计算 `eligible`/`pairedDraft`（查表，不依赖模型文件是否存在）用于驱动 `speculativeDecodingNoticeText` 的提示文案（见"四"），`draftModelConfigured`/`speculativeDecodingActive` 的最终判定结果保留在 `LlmEngine` 内部，不对 `SessionController` 公开为独立属性。

---

## 四、SessionController 新增只读属性（唯一的对外契约增量，FR-002/FR-006）

本特性预期**不**改变 QML↔C++ 核心契约（FR-002：`generate()` 对外签名不变；`switchModel()`/`availableModels`/`recommendedProfiles`/`currentModelName` 等既有属性/方法签名与语义均保持不变）。**唯一的例外**（用户本轮已预先明确授权的契约增量）：新增一个只读属性，供设置页面展示"未启用加速及原因"提示（FR-006）与"候选草稿规格 + 自动搭配说明"（FR-004/FR-007，User Story 2）。

```cpp
// SessionController.h 新增（具体属性名/粒度留待 /speckit.tasks 阶段最终确定，以下为建议）：
Q_PROPERTY(QString speculativeDecodingNoticeText READ speculativeDecodingNoticeText
           NOTIFY speculativeDecodingNoticeTextChanged)
Q_PROPERTY(QVariantList recommendedDraftProfiles READ recommendedDraftProfilesVariant CONSTANT)
```

| 属性 | 类型 | 取值规则 | 对应 FR |
|---|---|---|---|
| `speculativeDecodingNoticeText` | string | 当前目标档位 `eligible == false`（低配）时为空字符串（不展示任何提示，维持 001/002/003 既有低配体验不变，FR-009）；`eligible == true` 且 `draftModelConfigured == false` 时为非空提示文案（如"未检测到草稿模型，投机解码未启用"，FR-006 原文示例）；`eligible == true` 且 `draftModelConfigured == true` 时为空字符串（已启用，无需提示；或可选地展示"投机解码已启用"类正向提示，具体文案留待 `/speckit.tasks`） | FR-006 |
| `recommendedDraftProfiles` | list（元素含 `size`/`modelName`/`quantization`/`approxSizeDescription`，见"二") | 固定返回两条静态数据，与 `recommendedProfiles` 同构 | FR-004 |

**不新增**的内容（经核实不需要）：不新增"草稿模型切换"相关的 `Q_INVOKABLE` 方法——FR-007 已明确"用户无需在设置页面手动选择搭配关系"，草稿模型的加载/切换完全是 `switchModel(目标模型路径)` 调用的隐式副作用（内部自动查表加载对应草稿模型），不需要 QML 侧发起任何新的调用动作。

---

## 五、内部接口变更索引（非 QML 对外契约，供 `/speckit.tasks` 排期）

| 文件 | 变更类型 | 说明 |
|---|---|---|
| `ModelCatalog.h/.cpp` | 新增 | `DraftCandidate` 枚举、`classifyDraftCandidateByFileName()`、`recommendedDraftProfiles()`（见"一""二"） |
| `ModelCatalog.h/.cpp` | 不变 | `TierHint`、`classifyTierByFileName()`、`recommendedProfiles()`、`tierDebounceWindow()`（003 既有，均不受本特性影响，`research.md` §5.1 核实结论） |
| `LlmEngine.h` | **不变（对外签名）** | `generate(const QString&)` 对外签名不变（FR-002）；构造函数签名按已决策的方案 B（`research.md` §3）扩展，新增一个可选的草稿模型路径参数 |
| `LlmEngine.cpp` | 修改（范围已按已决策的方案 B 确定） | 新增投机解码算法内部实现（`research.md` §2 四步循环）；GBNF 语法约束同时应用于草稿/目标两侧且状态同步（`research.md` §6，已决定）；内部持有第二组 `llama_model*`/`llama_context*`，草稿模型缺失/加载失败时保持 `nullptr` 并静默降级（FR-006） |
| `SessionController.h/.cpp` | 新增 | `speculativeDecodingNoticeText`/`recommendedDraftProfiles` 只读属性（见"四"）；`switchModel()`/`applyModelSwitchResult()`/`bootstrapPlatformDefaults()` 内部新增"按目标档位查表得到草稿模型路径并一并传给 `LlmEngine` 构造函数"调用点（方案 B：`SessionController` 私有状态与公开契约零改动） |

---

## 六、与既有实体的差异对照表

| 实体 | 来源 | 本特性是否变更 |
|---|---|---|
| `ModelDescriptor` | 002 | 结构不变；新增并列 `DraftCandidate` 枚举与独立分类方法（不写入结构体字段） |
| `TierHint`/`classifyTierByFileName()` | 002/003 | 不变（`research.md` §5.1 已核实无冲突） |
| `RecommendedModelProfile`/`recommendedProfiles()` | 002/003 | 不变 |
| `ActiveModelRuntimeState` | 002 | 不变 |
| `TierDebounceProfile`/`tierDebounceWindow()` | 003 | 不变 |
| `RecommendedDraftModelProfile`/`recommendedDraftProfiles()` | **本特性新增** | — |
| `SpeculativeDecodingProfile` | **本特性新增** | 字段结构与 `pairedDraft` 取值均已确定（`research.md` §5.3 用户决策：方案①，Medium→Size0_5B / High→Size1_5B） |
| `SessionController.speculativeDecodingNoticeText`/`recommendedDraftProfiles`（QML 属性） | **本特性新增（唯一对外契约增量）** | — |
