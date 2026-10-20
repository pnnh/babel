# Contract: ModelCatalog 草稿模型识别与搭配映射内部契约（非 QML↔C++ 对外契约）

**Spec**: [spec.md](../spec.md) ｜ **Data Model**: [data-model.md](../data-model.md) ｜ **Research**: [research.md](../research.md)

## 适用范围

本文件记录本特性为落实 FR-004/FR-006/FR-007 在 `babel::slowpath::ModelCatalog` 内部新增的方法与行为约束。`ModelCatalog` 本身不是 QML 可见类型，但其静态数据（`recommendedProfiles()`）与扫描结果（`scan()`）经 `SessionController` 转译后通过 `availableModels`/`recommendedProfiles` 等既有 QML 属性对外暴露（002 契约）；本特性新增的 `recommendedDraftProfiles()` 同样经由 `contracts/qml-cpp-session-interface-increment.md` 新增的同名 QML 属性对外暴露，该文件记录两者之间的内部映射关系。

## 一、新增静态方法签名

```cpp
// data-model.md 一：新增枚举（ModelDescriptor 内，与既有 TierHint 并列，互不影响）
enum class DraftCandidate { None, Size0_5B, Size1_5B };

class ModelCatalog {
 public:
  // 既有方法（scan/recommendedProfiles/classifyTierByFileName/tierDebounceWindow）签名与行为均不变。

  // 新增：文件名 → 草稿候选分类（与既有 classifyTierByFileName() 同构的独立分类方法，research.md §5.1）
  static ModelDescriptor::DraftCandidate classifyDraftCandidateByFileName(const QString &fileName);

  // 新增：固定两档草稿模型推荐说明性数据（data-model.md 二，FR-004）
  static std::vector<RecommendedDraftModelProfile> recommendedDraftProfiles();

  // 新增：目标档位 → 固定搭配草稿规格查表（research.md §5.3，用户决策：方案①，具体映射表详见下文"三"）
  static ModelDescriptor::DraftCandidate pairedDraftCandidate(ModelDescriptor::TierHint targetTier);
};
```

## 二、行为契约（可被测试断言的外部可观察效果）

1. `classifyDraftCandidateByFileName()` 对文件名含 `"0.5b"`/`"0-5b"` 子串（不区分大小写）的输入 MUST 返回 `DraftCandidate::Size0_5B`；含 `"1.5b"`/`"1-5b"` 子串 MUST 返回 `DraftCandidate::Size1_5B`；均不匹配 MUST 返回 `DraftCandidate::None`（`research.md` §5.1）。
2. `classifyDraftCandidateByFileName()` 的新增**不得**改变 `classifyTierByFileName()` 既有四个分支（`"3b"`/`"7b"`/`"14b"`/`"32b"`）的任何既有行为——`research.md` §5.1 已核实两者的匹配子串互不冲突，`/speckit.tasks` 阶段的测试 MUST 覆盖"同一文件名分别调用两个方法得到的结果互不干扰"这一回归断言。
3. `recommendedDraftProfiles()` MUST 恒定返回两条数据（`Size0_5B` 一条、`Size1_5B` 一条），与 `recommendedProfiles()` 固定返回三条数据的既有模式一致（静态数据，非扫描结果，不依赖文件系统状态）。
4. `pairedDraftCandidate(TierHint::Low)` MUST 返回 `DraftCandidate::None`（FR-005/FR-009：低配恒不启用投机解码，不存在"低配搭配草稿模型"这一概念）。
5. `pairedDraftCandidate(TierHint::Medium)` MUST 返回 `DraftCandidate::Size0_5B`；`pairedDraftCandidate(TierHint::High)` MUST 返回 `DraftCandidate::Size1_5B`（用户决策：方案①，见下文"三"）。

## 三、档位搭配映射表（用户已决策，方案①，终值）

`research.md` §5.3 已列出候选方案①（Medium→Size0_5B / High→Size1_5B，Recommendation）/②（均→Size1_5B）/③（均→Size0_5B）/④（不区分档位，按已下载文件统一搭配，与 FR-007 措辞存在张力），**用户已就此作出显式决策：采纳方案①**（档位越高，草稿模型也越强）。

`/speckit.tasks`/`/speckit.implement` MUST 按下表实现 `pairedDraftCandidate()`，不得再考虑候选方案②~④：

```text
pairedDraftCandidate(Medium) = DraftCandidate::Size0_5B   # Medium(Qwen2.5-7B-Instruct)  ↔ Qwen2.5-0.5B-Instruct
pairedDraftCandidate(High)   = DraftCandidate::Size1_5B   # High(Qwen2.5-14B-Instruct)   ↔ Qwen2.5-1.5B-Instruct
```

## 四、本契约已确认不变的事项（`research.md` §5.1，事实核实结论，非待决）

- `ModelDescriptor` 结构体字段、`TierHint` 枚举、`Availability` 枚举均**不**因本特性发生任何变化。
- `ModelCatalog::scan()` 的既有行为（枚举约定目录下全部 `*.gguf` 文件、GGUF 魔数校验、按文件名排序）**不**因本特性发生任何变化；草稿模型文件与目标模型文件共享同一约定目录（`ModelPaths::conventionalModelDirectory()`），延续"推荐但需用户手动下载/放置"既有模式（FR-004），不新增独立的草稿模型专用目录。
