#pragma once

#include <filesystem>

namespace babel::slowpath {

/// 本地 LLM 模型文件约定目录/路径工具（research.md §7 委派结果）：
/// 用户需手动下载 Qwen2.5-3B-Instruct GGUF Q4_K_M 模型文件后，放置于本约定目录，
/// 应用不随安装包分发该文件，亦不提供应用内自动下载入口。
class ModelPaths {
 public:
  /// 约定本地模型目录：平台标准用户数据目录（Qt QStandardPaths::AppDataLocation）
  /// 下的 `models/` 子目录（内部实现细节，按宪章原则 IV 自主确定，不改变
  /// research.md §7 已确定的外部行为——用户需手动获取并放置模型文件）。
  static std::filesystem::path conventionalModelDirectory();

  /// 约定模型文件名/完整路径（Qwen2.5-3B-Instruct GGUF Q4_K_M，对应 FR-013）。
  static std::filesystem::path conventionalModelFilePath();
};

}  // namespace babel::slowpath
