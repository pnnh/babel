#pragma once

#include <QString>
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
};

}  // namespace babel::slowpath
