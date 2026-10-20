# Data Model: 模型档位调整、GBNF 语法约束解码与防抖节奏优化

**Spec**: [spec.md](./spec.md) ｜ **Research**: [research.md](./research.md)

## 阅读说明

本文件是 Phase 1 产出，记录本特性对 002 既有实体（`specs/002-lattice-dict-model-switch/data-model.md`）的**增量变更**。002 原文件中未被下表提及的实体（`CandidateEntry`/`PinyinLatticeCell`/`PinyinLattice`/`RimeDictionarySourceFile`/`DictionaryEntry`/`ModelDescriptor`/`ActiveModelRuntimeState`）**原样有效，未变更**，仅 `RecommendedModelProfile` 的静态数据取值与 `ModelDescriptor.tierHint` 的分类规则（非结构本身）有变更，见下文。

---

## 一、模型档位相关实体（变更）

### 1. RecommendedModelProfile（结构不变，静态数据取值变更）

结构定义（`ModelCatalog.h`）不变，仍为：

```cpp
struct RecommendedModelProfile {
  enum class Tier { Low, Medium, High };
  Tier tier;
  QString modelName;
  QString quantization;
  QString approxSizeDescription;
  QString targetHardwareDescription;
};
```

`ModelCatalog::recommendedProfiles()` 返回的三条静态数据取值变更（`research.md` §6，FR-001/FR-002 原文数值）：

| 字段 | Low（不变） | Medium（变更） | High（变更） |
|---|---|---|---|
| `modelName` | `Qwen2.5-3B-Instruct` | `Qwen2.5-14B-Instruct` → **`Qwen2.5-7B-Instruct`** | `Qwen2.5-32B-Instruct` → **`Qwen2.5-14B-Instruct`** |
| `quantization` | `GGUF Q4_K_M` | `GGUF Q4_K_M`（不变） | `GGUF Q4_K_M`（不变） |
| `approxSizeDescription` | `约 2GB` | `约 9GB` → **`约 4.6GB`** | `约 19.85GB` → **`约 9GB`** |
| `targetHardwareDescription` | 通用低配设备 | 不变 | 不变 |

### 2. ModelDescriptor.tierHint 分类规则（`TierHint` 枚举本身不变，分类依据的关键字表变更）

`ModelDescriptor` 结构（`ModelCatalog.h`）与 `TierHint`/`Availability` 两个枚举定义本身**不变**。仅 `ModelCatalog.cpp` 内部 `classifyTier()`（本特性提升为公开静态方法，见下文"五、内部接口变更"）所依据的文件名关键字—档位对应表变更（`research.md` §5）：

| 文件名关键字（小写子串匹配） | 原对应档位（002） | 新对应档位（本特性） |
|---|---|---|
| `3b` / `3-b` | `Low` | `Low`（不变） |
| `7b` / `7-b` | （未覆盖，落入 `Unknown`） | **`Medium`（新增）** |
| `14b` | `Medium` | **`High`（变更）** |
| `32b` | `High` | `High`（不变） |
| 均不匹配 | `Unknown` | `Unknown`（不变） |

---

## 二、新增实体：TierDebounceProfile（模型档位防抖参数）

对应 spec Key Entities"模型档位防抖参数（Tier Debounce Profile）"，描述"模型档位 → 防抖窗口时长"的映射关系（FR-010～FR-012）。

**结构形式（Decision，内部实现细节，按宪章原则 IV 自主决定）**：不新增独立的 C++ `struct`/类，采用一个固定的查表函数（静态数据 + 纯函数），避免为一张三行的映射表引入不必要的新类型：

```cpp
// 建议落位：babel/src/slowpath/DebounceScheduler.h 或 ModelCatalog.h（内部细节，
// /speckit.tasks 阶段按就近原则确定，不新增独立头文件）。
std::chrono::milliseconds tierDebounceWindow(ModelDescriptor::TierHint tier);
```

| `TierHint` | 防抖窗口时长 | 对应 FR | 来源 |
|---|---|---|---|
| `Low` | 200ms | FR-011 | spec 已澄清数值，不变 |
| `Medium` | 500ms | FR-012 | spec 已澄清数值（2026-10-19 澄清） |
| `High` | 800ms | FR-012 | spec 已澄清数值（2026-10-19 澄清） |
| `Unknown` | **200ms**（复用 `Low` 档位数值） | — | 用户决策：Unknown 档位复用低配 200ms（`research.md` §5，方案①/Recommendation 已采纳） |

**字段/取值来源说明**：三个已澄清档位的数值均可逐字追溯至 `spec.md` Clarifications 小节原文；`Unknown` 一行的 200ms 数值来自用户对 `research.md` §5 候选方案的显式决策（采纳方案①，复用低配 200ms），`/speckit.tasks` 排期时 MUST 将该叶子行为列为明确的子任务并直接采纳该数值作为验收标准，不再标注为待定事项。

---

## 三、新增实体：Output Grammar Rule（GBNF 输出语法规则）

对应 spec Key Entities"GBNF 输出语法规则（Output Grammar Rule）"，描述约束 `LlmEngine` 生成采样的语法定义（FR-005～FR-009）。

**结构形式（Decision，内部实现细节）**：以 `LlmEngine.cpp` 内一个编译期字符串常量表示，不引入独立语法文件（`.gbnf` 磁盘文件），理由：语法内容固定不变、不随用户配置变化，无需外部化为可配置资源，内嵌常量更利于与 `LlmEngine::generate()` 的现有代码风格（系统提示词 `kSystemPrompt` 同样以内嵌字符串常量形式存在）保持一致。

| 字段 | 取值 | 对应 FR |
|---|---|---|
| 语法根规则名（`grammar_root` 参数） | `"root"` | — |
| 语法产生式字符串（`grammar_str` 参数） | `"root ::= safe-char*\nsafe-char ::= [^\\u27E6\\u27E7\\x7C]\n"` | FR-007（`research.md` §2 设计方案） |
| 排除码点 1 | U+27E6（`⟦`） | FR-005/FR-007 |
| 排除码点 2 | U+27E7（`⟧`） | FR-005/FR-007 |
| 排除码点 3 | U+007C（`\|`） | FR-005/FR-007 |
| 合法字符集边界 | "最宽边界"：除上述三个码点外，一切 Unicode 码点（含汉字、中文标点、数字、英文字母、全/半角标点、emoji）均合法 | FR-007（2026-10-19 澄清） |
| 梗阻（找不到合法延续 token）时的行为 | 提前终止生成，返回已生成的部分内容 | FR-008（2026-10-19 澄清，`research.md` §3 实现方案） |

**与 `generate()` 对外契约的关系**：本实体**完全是 `LlmEngine` 内部状态**，不出现在任何 QML 可见属性/信号/方法参数中，`generate(const QString &normalizedInput) const` 的输入输出类型、调用约定均不变（FR-006）。

---

## 四、本特性未新增/未变更的既有实体（显式列出，避免误解为遗漏）

- `DebounceScheduler` 的状态机字段（`m_state`/`m_pendingInput`/`m_lastChangeAt`/`m_timerArmed`/`m_generationInFlight`）与 `GenerationState` 枚举（`Idle`/`Debouncing`/`Generating`/`Ready`/`Stale`）结构与迁移逻辑：**不变**（FR-013）。
- `ActiveModelRuntimeState`（002 已定义）：**不变**，本特性不新增字段，当前生效模型的档位通过"实时对 `currentModelPath` 文件名重新分类"获得（`research.md` §5 方案 1），不缓存/持久化档位信息到该结构中（避免档位与实际加载模型脱节的一致性风险）。
- `PinyinLattice`/`CandidateEntry`/结构化标注文本格式（`⟦...|...⟧`）：**不变**，GBNF 语法约束的是 `generate()` **输出** token 流，与**输入** prompt 中允许携带的标注符号语法是两个独立方向，互不影响（输入侧标注符号的生成/解析逻辑在 `PinyinLattice::toAnnotatedPromptText()`，不在本特性改动范围）。

---

## 五、内部接口变更索引（非 QML↔C++ 对外契约，详见 `contracts/`）

| 变更 | 文件 | 对应 FR |
|---|---|---|
| `classifyTier()` 由 `ModelCatalog.cpp` 匿名命名空间私有函数提升为 `ModelCatalog::classifyTierByFileName()` 公开静态方法，并更新关键字表（见上文"一、2"） | `ModelCatalog.h`/`.cpp` | FR-010（间接，供 SessionController 调用） |
| 新增 `tierDebounceWindow(TierHint)` 查表函数 | `DebounceScheduler.h`/`.cpp` 或 `ModelCatalog.h`/`.cpp`（内部细节） | FR-010～FR-012 |
| 新增 `DebounceScheduler::setDebounceWindow(std::chrono::milliseconds)` 方法 | `DebounceScheduler.h`/`.cpp` | FR-010/FR-013/FR-014 |
| `recommendedProfiles()` 静态数据取值更新 | `ModelCatalog.cpp` | FR-001/FR-002 |
| `generate()` 内部新增 GBNF 语法采样器 + 梗阻检测循环 | `LlmEngine.cpp` | FR-005～FR-009 |
| `SessionController::bootstrapPlatformDefaults()`/`applyModelSwitchResult()` 新增"按当前模型档位调用 `setDebounceWindow()`"的调用点 | `SessionController.cpp` | FR-010/FR-014 |

详见 `contracts/llm-engine-grammar-internal-contract.md` 与 `contracts/debounce-tier-window-internal-contract.md`。
