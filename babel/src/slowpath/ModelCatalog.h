#pragma once

#include <QString>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace babel::slowpath {

/// 本地模型目录条目（data-model.md §6），`ModelCatalog::scan()` 产出的列表元素，
/// 供设置页面展示（FR-011）。
struct ModelDescriptor {
  enum class TierHint { Low, Medium, High, Unknown };
  /// `research.md` §7.3：GGUF 文件头魔数校验结果；`KnownUnloadable` 仍可被选中以
  /// 触发 FR-013 回退链路，仅附加"不可用"视觉标记。
  enum class Availability { Unverified, Verified, KnownUnloadable };
  /// 004-speculative-decoding（data-model.md 一，research.md §5.1）：草稿模型
  /// 候选规格识别结果，与 `TierHint` 并列、互不影响，不写入扫描结果本身（见
  /// `ModelCatalog::classifyDraftCandidateByFileName()`）。
  enum class DraftCandidate { None, Size0_5B, Size1_5B };

  std::filesystem::path filePath;
  QString fileName;
  int64_t sizeBytes = 0;
  TierHint tierHint = TierHint::Unknown;
  Availability availability = Availability::Unverified;
};

/// 固定三档推荐模型说明性数据（data-model.md §7，FR-012 原文数值），非扫描结果、
/// 不持久化。
struct RecommendedModelProfile {
  enum class Tier { Low, Medium, High };

  Tier tier = Tier::Low;
  QString modelName;
  QString quantization;
  QString approxSizeDescription;
  QString targetHardwareDescription;
};

/// 004-speculative-decoding（data-model.md 二，FR-004）：固定两档草稿模型推荐
/// 说明性数据，与 `RecommendedModelProfile` 并列、结构同构但枚举类型独立
/// （research.md §5.2：避免把"目标档位"与"草稿规格"两个维度混用到同一枚举）。
struct RecommendedDraftModelProfile {
  ModelDescriptor::DraftCandidate size = ModelDescriptor::DraftCandidate::None;
  QString modelName;
  QString quantization;
  QString approxSizeDescription;
};

/// 当前模型运行时状态（data-model.md §8），承载 FR-013（级联回退）/FR-014（热切
/// 换）所需的运行时状态；全部字段仅存在于进程内存，不持久化到磁盘
/// （research.md §9：每次应用启动固定重新初始化为低配默认模型）。
struct ActiveModelRuntimeState {
  std::filesystem::path currentModelPath;
  bool isSwitching = false;
  std::vector<std::filesystem::path> fallbackHistory;  ///< 本次运行期间已知成功加载过的模型路径栈
  QString lastFallbackNotice;  ///< 为空字符串时 QML 不展示该提示
};

/// 本地模型目录扫描工具（research.md §7.1）：枚举
/// `babel::slowpath::ModelPaths::conventionalModelDirectory()` 下全部 `*.gguf`
/// 文件，构建 `ModelDescriptor` 列表。
class ModelCatalog {
 public:
  /// 扫描约定模型目录，返回全部 `*.gguf` 文件对应的描述条目（`scan()` 实现见
  /// User Story 3 阶段）。
  static std::vector<ModelDescriptor> scan();

  /// 固定三档 `RecommendedModelProfile` 静态数据（FR-012 原文数值）。
  static std::vector<RecommendedModelProfile> recommendedProfiles();

  /// 档位标签启发式分类（原 `.cpp` 匿名命名空间私有 `classifyTier()` 提升为公开
  /// 静态方法，`data-model.md` 一/2、`contracts/debounce-tier-window-internal-
  /// contract.md` 二）：文件名包含 "3b"/"3-b" 归类 `Low`；包含 "7b"/"7-b" 归类
  /// `Medium`；包含 "14b" 归类 `High`（本特性变更：14B 现为高配推荐，`research.md`
  /// §5）；包含 "32b" 归类 `High`；均不匹配归类 `Unknown`。供 `scan()` 内部调用，
  /// 亦供 `SessionController` 在 `bootstrapPlatformDefaults()`/
  /// `applyModelSwitchResult()` 中直接调用以获知当前模型所属档位。
  static ModelDescriptor::TierHint classifyTierByFileName(const QString &fileName);

  /// 模型档位 -> 防抖窗口时长查表（`data-model.md` 二 TierDebounceProfile，
  /// `contracts/debounce-tier-window-internal-contract.md` 三，FR-010～FR-012）：
  /// `Low`/`Unknown` -> 200ms（`Unknown` 复用 `Low` 档位数值，用户已显式决策）、
  /// `Medium` -> 500ms、`High` -> 800ms。
  static std::chrono::milliseconds tierDebounceWindow(ModelDescriptor::TierHint tier);

  /// 004-speculative-decoding（research.md §5.1，contracts/model-catalog-draft-
  /// pairing-internal-contract.md 一/二.1/二.2）：文件名（不区分大小写）含
  /// "0.5b"/"0-5b" 子串 -> `Size0_5B`；含 "1.5b"/"1-5b" 子串 -> `Size1_5B`；
  /// 均不匹配 -> `None`。与 `classifyTierByFileName()` 同构、互不影响（已核实
  /// 两者匹配子串互不冲突），供 `SessionController` 查询"当前目标档位所需草稿
  /// 模型是否已被用户下载"使用。
  static ModelDescriptor::DraftCandidate classifyDraftCandidateByFileName(const QString &fileName);

  /// 004-speculative-decoding（data-model.md 二，FR-004，contracts/model-
  /// catalog-draft-pairing-internal-contract.md 二.3）：固定返回两条静态数据
  /// （`Size0_5B`/`Size1_5B` 各一条），供设置页面 User Story 2 展示候选规格。
  static std::vector<RecommendedDraftModelProfile> recommendedDraftProfiles();

  /// 004-speculative-decoding（research.md §5.3 用户决策：方案①，contracts/
  /// model-catalog-draft-pairing-internal-contract.md 三）：目标档位 -> 固定
  /// 搭配草稿规格查表——`Low`/`Unknown` -> `None`（FR-005/FR-009：低配恒不启用
  /// 投机解码）；`Medium` -> `Size0_5B`（Qwen2.5-7B-Instruct ↔ Qwen2.5-0.5B-
  /// Instruct）；`High` -> `Size1_5B`（Qwen2.5-14B-Instruct ↔ Qwen2.5-1.5B-
  /// Instruct）。MUST NOT 考虑候选方案②～④。
  static ModelDescriptor::DraftCandidate pairedDraftCandidate(ModelDescriptor::TierHint targetTier);
};

}  // namespace babel::slowpath
