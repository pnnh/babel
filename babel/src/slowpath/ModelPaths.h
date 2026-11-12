#pragma once

#include <filesystem>

namespace babel::slowpath {

/// 本地 LLM 模型文件约定目录/路径工具（Feature 008 迁移至 ~/.local/models）：
/// 用户需手动下载 Qwen2.5-3B-Instruct GGUF Q4_K_M 模型文件后放置于本约定目录，
/// 或通过环境变量 BABEL_MODEL_PATH 自定义路径。
class ModelPaths {
 public:
  /// 约定本地模型目录：标准用户数据目录（~/.local/models）
  static std::filesystem::path conventionalModelDirectory();

  /// 约定模型文件名/完整路径（~/.local/models/qwen2.5-3b-instruct-q4_k_m.gguf）
  static std::filesystem::path conventionalModelFilePath();

  /// 解析最终生效的模型文件路径，按优先级：
  /// 1. 环境变量 BABEL_MODEL_PATH（若设定且非空）
  /// 2. 约定标准路径 conventionalModelFilePath()（若文件物理存在）
  /// 3. 历史 AppDataLocation 路径（若物理存在则兼容并输出警告提示）
  /// 4. 默认返回 conventionalModelFilePath()
  static std::filesystem::path resolveModelFilePath();
};

}  // namespace babel::slowpath
