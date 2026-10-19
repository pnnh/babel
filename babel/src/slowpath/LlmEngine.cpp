#include "babel/src/slowpath/LlmEngine.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <system_error>

#if BABEL_HAVE_LLAMA_CPP
#include <llama.h>
#include <cstring>
#include <string>
#include <vector>
#endif

namespace babel::slowpath {

LlmEngine::LlmEngine(std::filesystem::path modelPath) : m_modelPath(std::move(modelPath)) {
  tryLoadModel();
}

LlmEngine::~LlmEngine() { releaseModel(); }

void LlmEngine::tryLoadModel() {
  std::error_code ec;
  if (!std::filesystem::exists(m_modelPath, ec) || ec) {
    spdlog::warn("LlmEngine: 模型文件不存在（{}），modelAvailable=false（FR-011）", m_modelPath.string());
    m_modelAvailable = false;
    return;
  }
  const auto size = std::filesystem::file_size(m_modelPath, ec);
  if (ec || size == 0) {
    spdlog::warn("LlmEngine: 模型文件损坏/为空（{}），modelAvailable=false（FR-011）", m_modelPath.string());
    m_modelAvailable = false;
    return;
  }

#if BABEL_HAVE_LLAMA_CPP
  llama_backend_init();
  llama_model_params mparams = llama_model_default_params();
#if defined(__APPLE__)
  mparams.n_gpu_layers = 999;  // macOS 启用 Metal（research.md §1），辅助达成 SC-003 延迟目标
#endif
  m_model = llama_model_load_from_file(m_modelPath.string().c_str(), mparams);
  if (!m_model) {
    spdlog::error("LlmEngine: llama_model_load_from_file 失败（{}），modelAvailable=false（FR-011）",
                  m_modelPath.string());
    m_modelAvailable = false;
    return;
  }
  llama_context_params cparams = llama_context_default_params();
  cparams.n_ctx = 2048;
  m_ctx = llama_init_from_model(m_model, cparams);
  if (!m_ctx) {
    spdlog::error("LlmEngine: llama_init_from_model 失败，modelAvailable=false（FR-011）");
    llama_model_free(m_model);
    m_model = nullptr;
    m_modelAvailable = false;
    return;
  }
  m_modelAvailable = true;
#else
  spdlog::warn("LlmEngine: 本次构建未链接 llama-cpp（见 CMakeLists.txt BABEL_HAVE_LLAMA_CPP），modelAvailable=false");
  m_modelAvailable = false;
#endif
}

void LlmEngine::releaseModel() {
#if BABEL_HAVE_LLAMA_CPP
  if (m_ctx) {
    llama_free(m_ctx);
    m_ctx = nullptr;
  }
  if (m_model) {
    llama_model_free(m_model);
    m_model = nullptr;
  }
#endif
}

QString LlmEngine::generate(const QString &normalizedInput) const {
  if (!m_modelAvailable) {
    return {};
  }
#if BABEL_HAVE_LLAMA_CPP
  // 每次生成前清空 KV cache：SessionController 的 DebounceScheduler 会在同一个
  // LlmEngine 实例上为"每次防抖触发"独立调用一次 generate()（见
  // SessionController::onSlowPathTriggered），各次调用语义上彼此无关、互不延续
  // 对话（每次都是对当前完整草稿的一次性改写）。若不清空，前一次调用残留的
  // KV cache 会与本次新 prompt 在同一序列位置上重叠累积，随着用户持续输入、
  // 调用次数增多，最终导致 KV cache 耗尽（"failed to find a memory slot"）或
  // 位置状态错乱产生 NaN логits，触发 llama_sampler_dist_apply 的 assert(found)
  // 崩溃。
  llama_memory_clear(llama_get_memory(m_ctx), true);

  // Qwen2.5-Instruct 采用 ChatML 对话模板；本 MVP 固定系统提示词，仅要求输出
  // 改写后的标准中文句子，不附加解释（对应 FR-004 的"标准中文译写结果"）。
  static constexpr char kSystemPrompt[] =
      "你是一个把中英文混杂的草稿整理为标准、通顺中文表达的助手。"
      "只输出改写后的标准中文句子，不要添加任何解释或前后缀。";
  const std::string prompt = std::string("<|im_start|>system\n") + kSystemPrompt + "<|im_end|>\n" +
                              "<|im_start|>user\n" + normalizedInput.toStdString() + "<|im_end|>\n" +
                              "<|im_start|>assistant\n";

  const llama_vocab *vocab = llama_model_get_vocab(m_model);
  std::vector<llama_token> tokens(prompt.size() + 16);
  const int32_t nTokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(),
                                          static_cast<int32_t>(tokens.size()), /*add_special=*/true,
                                          /*parse_special=*/true);
  if (nTokens < 0) {
    spdlog::error("LlmEngine: 提示词 tokenize 失败");
    return {};
  }
  tokens.resize(static_cast<std::size_t>(nTokens));

  llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
  if (llama_decode(m_ctx, batch) != 0) {
    spdlog::error("LlmEngine: 提示词 llama_decode 失败");
    return {};
  }

  llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
  llama_sampler *sampler = llama_sampler_chain_init(sparams);
  llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.3f));
  llama_sampler_chain_add(sampler, llama_sampler_init_dist(/*seed=*/0));

  std::string result;
  constexpr int kMaxOutputTokens = 256;  // MVP 固定上限，避免单次生成无界耗时
  for (int i = 0; i < kMaxOutputTokens; ++i) {
    const llama_token nextToken = llama_sampler_sample(sampler, m_ctx, -1);
    if (llama_vocab_is_eog(vocab, nextToken)) {
      break;
    }
    char buf[256] = {0};
    const int32_t written = llama_token_to_piece(vocab, nextToken, buf, sizeof(buf), 0, true);
    if (written > 0) {
      result.append(buf, static_cast<std::size_t>(written));
    }
    llama_token nextTokenArr[1] = {nextToken};
    llama_batch nextBatch = llama_batch_get_one(nextTokenArr, 1);
    if (llama_decode(m_ctx, nextBatch) != 0) {
      break;
    }
  }
  llama_sampler_free(sampler);
  return QString::fromStdString(result);
#else
  Q_UNUSED(normalizedInput);
  return {};
#endif
}

}  // namespace babel::slowpath
