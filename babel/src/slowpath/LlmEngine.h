#pragma once

#include <QString>
#include <filesystem>
#include <vector>

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
///
/// 004-speculative-decoding（research.md §3 用户决策：方案 B，contracts/llm-
/// engine-speculative-internal-contract.md）：内部可选封装第二组"草稿模型"
/// `llama_model*`/`llama_context*`，用于投机解码加速中/高配档位的生成速度，
/// 对 `SessionController`/调用方完全透明——`generate()` 对外签名不变（FR-002）。
class LlmEngine {
 public:
  /// `draftModelPath` 为可选参数（默认空路径，向后兼容既有单参数调用点，FR-002/
  /// FR-009）：非空时构造函数会尝试额外加载草稿模型，供 `generate()` 内部判断
  /// 是否走投机解码分支；路径为空或对应文件不存在/加载失败时静默降级为既有
  /// 单模型生成路径（FR-006），不影响 `modelPath` 本身的加载结果。
  explicit LlmEngine(std::filesystem::path modelPath, std::filesystem::path draftModelPath = {});
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

  // gbnf-grammar-sampling-overhead（fix.md Preferred remediation）：003 阶段用
  // 通用 GBNF 语法引擎（llama_sampler_init_grammar）实现输出语法里的
  // ⟦/⟧/| 三字符静态黑名单，导致每个输出 token 都要对全词表做一次昂贵的
  // "解码+栈状态模拟"。该黑名单结果只取决于模型词表本身、与生成到第几步无
  // 关，因此改为模型加载完成后一次性预计算、按 token id 索引的禁用位图
  // （见 LlmEngine.cpp 匿名命名空间 ForbiddenTokenMask），采样循环内直接
  // O(1) 查表。按 token id 索引，下标越界视为未禁用。
  std::vector<bool> m_forbiddenTokenMask;

  // 004-speculative-decoding（contracts/llm-engine-speculative-internal-
  // contract.md 四）：草稿模型生命周期与目标模型对称管理，析构顺序一致。
  std::filesystem::path m_draftModelPath;
  bool m_draftModelAvailable = false;
  llama_model *m_draftModel = nullptr;
  llama_context *m_draftCtx = nullptr;

  // 草稿模型词表与目标模型词表是两套独立词表（token id 含义不同），禁用位图
  // 不可共用，需单独预计算、单独缓存。
  std::vector<bool> m_draftForbiddenTokenMask;
};

}  // namespace babel::slowpath
