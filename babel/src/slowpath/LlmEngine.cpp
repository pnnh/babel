#include "babel/src/slowpath/LlmEngine.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <system_error>

#if BABEL_HAVE_LLAMA_CPP
#include <llama.h>
#include <algorithm>
#include <cmath>
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
  //
  // 002-lattice-dict-model-switch（T021，FR-007，contracts/llm-prompt-annotation-
  // contract.md）：输入文本中可能包含 ⟦原始token:候选1 权重1|候选2 权重2|...⟧
  // 形式的结构化标注片段（`PinyinLattice::toAnnotatedPromptText()` 产出，仅对存
  // 在真实歧义的拼音位置出现），用于向模型传递"同一位置的多个候选汉字及其可
  // 信度"供结合上下文消歧；系统提示词补充说明该语法含义，`generate()` 对外签名
  // 仍为单一 QString 不变。
  static constexpr char kSystemPrompt[] =
      "你是一个把中英文混杂的草稿整理为标准、通顺中文表达的助手。"
      "只输出改写后的标准中文句子，不要添加任何解释或前后缀。"
      "输入文本中可能包含形如 \xe2\x9f\xa6原始拼音:候选1 权重1|候选2 权重2|...\xe2\x9f\xa7"
      "（使用 ⟦ ⟧ 包裹、| 分隔）的标注片段，表示同一位置存在多个候选汉字及其可信度数值"
      "（数值越大越可信）；这是因为该拼音存在同音字歧义，你必须结合上下文从候选中选择"
      "语义最合适的一项（不要求必须选择可信度最高者）；最终输出的句子中绝对不能包含"
      "⟦、⟧、| 等标注符号本身，只能是选定后的纯中文文字。";
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

  // GBNF 输出语法约束（FR-005～FR-009，data-model.md 三 Output Grammar Rule，
  // research.md §2/§3 方案 B，contracts/llm-engine-grammar-internal-
  // contract.md）：仅排除 ⟦（U+27E6）/⟧（U+27E7）/|（U+007C）三个 Lattice 内部
  // 标注符号，"最宽边界"——以单一负向字符类表达，不收窄其余任意 Unicode 字符
  // （汉字/中文标点/数字/字母/全半角标点/emoji 均合法）。`root` 定义为"零个或
  // 多个合法字符"（Kleene 星号），使语法状态机在任意生成位置都处于"可接受结
  // 束"状态，不与既有 `llama_vocab_is_eog()` 自然终止逻辑冲突。
  static constexpr char kOutputGrammar[] = "root ::= safe-char*\nsafe-char ::= [^\\u27E6\\u27E7\\x7C]\n";
  static constexpr char kOutputGrammarRoot[] = "root";

  // 放弃现有 llama_sampler_chain_init/llama_sampler_chain_add 一体式链条封装，
  // 改为手动分步调用：语法采样器单独 apply 之后、temp/dist 采样器 apply 之
  // 前，显式检查候选集合是否已全部被语法掩码为 -inf（"语法梗阻"，FR-008），
  // 链式封装无法在这两步之间插入该检查（research.md §3 方案 A 否决理由）。
  llama_sampler *grammar = llama_sampler_init_grammar(vocab, kOutputGrammar, kOutputGrammarRoot);
  llama_sampler *temp = llama_sampler_init_temp(0.3f);
  llama_sampler *dist = llama_sampler_init_dist(/*seed=*/0);

  std::string result;
  constexpr int kMaxOutputTokens = 256;  // MVP 固定上限，避免单次生成无界耗时
  for (int i = 0; i < kMaxOutputTokens; ++i) {
    const float *logits = llama_get_logits_ith(m_ctx, -1);
    const int32_t nVocab = llama_vocab_n_tokens(vocab);
    std::vector<llama_token_data> candidates(static_cast<std::size_t>(nVocab));
    for (int32_t t = 0; t < nVocab; ++t) {
      candidates[static_cast<std::size_t>(t)] = llama_token_data{t, logits[t], 0.0f};
    }
    llama_token_data_array cur_p{candidates.data(), candidates.size(), -1, false};

    llama_sampler_apply(grammar, &cur_p);  // 语法掩码：非法 token 的 logit 被置为 -inf

    const bool anyLegal = std::any_of(candidates.begin(), candidates.end(),
                                       [](const llama_token_data &d) { return d.logit > -INFINITY; });
    if (!anyLegal) {
      // FR-008：语法梗阻（候选集合全部为 -inf），提前终止生成，`result` 中已
      // 累积的部分内容即为本次返回值；不放宽约束续写、不判定失败、不要求重试。
      break;
    }

    llama_sampler_apply(temp, &cur_p);
    llama_sampler_apply(dist, &cur_p);  // 设置 cur_p.selected
    const llama_token nextToken = cur_p.data[static_cast<std::size_t>(cur_p.selected)].id;
    if (llama_vocab_is_eog(vocab, nextToken)) {
      break;  // 既有自然终止逻辑不变，与梗阻提前终止是两条彼此独立的循环退出路径
    }

    // 推进语法采样器内部解析堆栈状态：不可省略，否则下一轮 apply() 会基于过期
    // 堆栈状态误判合法性（research.md §3）。
    llama_sampler_accept(grammar, nextToken);

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
  llama_sampler_free(temp);
  llama_sampler_free(dist);
  llama_sampler_free(grammar);
  return QString::fromStdString(result);
#else
  Q_UNUSED(normalizedInput);
  return {};
#endif
}

}  // namespace babel::slowpath
