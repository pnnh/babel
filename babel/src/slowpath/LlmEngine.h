#pragma once

#include <QString>
#include <filesystem>

// 前置声明 llama.cpp 不透明类型，避免未链接 llama-cpp 时也强制要求包含 <llama.h>
// （见 babel/src/CMakeLists.txt 的 BABEL_HAVE_LLAMA_CPP 编译期开关）。
struct llama_model;
struct llama_context;

namespace babel::slowpath {

/// 慢流本地 LLM 推理引擎（research.md §1 已选型：llama.cpp 加载
/// Qwen2.5-3B-Instruct GGUF Q4_K_M，对应 FR-013）。
///
/// 模型文件缺失/损坏/无法加载（FR-011）时 `modelAvailable()` 返回 false，
/// 此时 `generate()` 不应被调用（由 SessionController 据 modelAvailable 判定）。
class LlmEngine {
 public:
  explicit LlmEngine(std::filesystem::path modelPath);
  ~LlmEngine();

  LlmEngine(const LlmEngine &) = delete;
  LlmEngine &operator=(const LlmEngine &) = delete;

  bool modelAvailable() const noexcept { return m_modelAvailable; }

  /// 基于规整化候选文本同步生成标准中文译写结果。
  /// 调用方（DebounceScheduler/SessionController）负责在非 UI 线程调用，避免阻塞界面。
  /// 前置条件：modelAvailable() == true；否则返回空字符串。
  QString generate(const QString &normalizedInput) const;

 private:
  void tryLoadModel();
  void releaseModel();

  std::filesystem::path m_modelPath;
  bool m_modelAvailable = false;
  llama_model *m_model = nullptr;
  llama_context *m_ctx = nullptr;
};

}  // namespace babel::slowpath
