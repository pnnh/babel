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

#if BABEL_HAVE_LLAMA_CPP
namespace {

/// 004-speculative-decoding（research.md §2/§7）：构造一个仅含单个 token、
/// 显式指定 `pos` 的批次，供投机解码分支在任意一次 `llama_memory_seq_rm()`
/// 回滚之后安全地继续 `llama_decode()`——`llama_batch_get_one()` 的自动位置
/// 追踪在回滚后可能与 KV cache 实际状态脱节，`research.md` §2 已指出 MUST
/// 改用 `llama_memory_seq_pos_min/max()` 重新计算起始 `pos`，本函数据此统一
/// 构造显式 `pos` 的单 token 批次，调用方负责 `llama_batch_free()`。
llama_batch makeSingleTokenBatch(llama_token token, llama_pos pos, llama_seq_id seqId = 0) {
  llama_batch batch = llama_batch_init(/*n_tokens=*/1, /*embd=*/0, /*n_seq_max=*/1);
  batch.n_tokens = 1;
  batch.token[0] = token;
  batch.pos[0] = pos;
  batch.n_seq_id[0] = 1;
  batch.seq_id[0][0] = seqId;
  batch.logits[0] = 1;
  return batch;
}

}  // namespace
#endif

LlmEngine::LlmEngine(std::filesystem::path modelPath, std::filesystem::path draftModelPath)
    : m_modelPath(std::move(modelPath)), m_draftModelPath(std::move(draftModelPath)) {
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

  // 004-speculative-decoding（T007，contracts/llm-engine-speculative-internal-
  // contract.md 二.1/二.4，FR-006）：目标模型加载成功后，若指定了草稿模型路径，
  // 尝试额外加载草稿模型；任一环节失败均仅记录诊断日志、静默降级，MUST NOT
  // 影响已成功加载的目标模型（m_modelAvailable 保持 true）。目标模型本身加载
  // 失败时（已在上方提前 return）不会执行到此处，避免额外开销。
  if (!m_draftModelPath.empty()) {
    std::error_code draftEc;
    if (!std::filesystem::exists(m_draftModelPath, draftEc) || draftEc) {
      spdlog::warn("LlmEngine: 草稿模型文件不存在（{}），投机解码不启用，静默降级（FR-006）",
                   m_draftModelPath.string());
    } else {
      const auto draftSize = std::filesystem::file_size(m_draftModelPath, draftEc);
      if (draftEc || draftSize == 0) {
        spdlog::warn("LlmEngine: 草稿模型文件损坏/为空（{}），投机解码不启用，静默降级（FR-006）",
                     m_draftModelPath.string());
      } else {
        llama_model_params draftMparams = llama_model_default_params();
#if defined(__APPLE__)
        draftMparams.n_gpu_layers = 999;
#endif
        m_draftModel = llama_model_load_from_file(m_draftModelPath.string().c_str(), draftMparams);
        if (!m_draftModel) {
          spdlog::warn("LlmEngine: 草稿模型 llama_model_load_from_file 失败（{}），投机解码不启用，静默降级（FR-006）",
                       m_draftModelPath.string());
        } else {
          llama_context_params draftCparams = llama_context_default_params();
          draftCparams.n_ctx = 2048;
          m_draftCtx = llama_init_from_model(m_draftModel, draftCparams);
          if (!m_draftCtx) {
            spdlog::warn("LlmEngine: 草稿模型 llama_init_from_model 失败，投机解码不启用，静默降级（FR-006）");
            llama_model_free(m_draftModel);
            m_draftModel = nullptr;
          } else {
            m_draftModelAvailable = true;
            spdlog::info("LlmEngine: 草稿模型加载成功（{}），投机解码已启用", m_draftModelPath.string());
          }
        }
      }
    }
  }
#else
  spdlog::warn("LlmEngine: 本次构建未链接 llama-cpp（见 CMakeLists.txt BABEL_HAVE_LLAMA_CPP），modelAvailable=false");
  m_modelAvailable = false;
#endif
}

void LlmEngine::releaseModel() {
#if BABEL_HAVE_LLAMA_CPP
  // 004-speculative-decoding（T008）：草稿模型资源释放，析构顺序与目标模型一致
  // （先 ctx 后 model），置空后不影响目标模型释放逻辑的既定顺序与条件判断。
  if (m_draftCtx) {
    llama_free(m_draftCtx);
    m_draftCtx = nullptr;
  }
  if (m_draftModel) {
    llama_model_free(m_draftModel);
    m_draftModel = nullptr;
  }
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
  std::string result;
  constexpr int kMaxOutputTokens = 256;  // MVP 固定上限，避免单次生成无界耗时

  // 004-speculative-decoding（T009，contracts/llm-engine-speculative-internal-
  // contract.md 四，research.md §3 方案 B）：目标模型提示词 tokenize/首次
  // decode（上方代码）完成后，依据草稿模型是否可用分支；草稿模型侧独立
  // tokenize 同一 prompt + 首次 decode 失败时（极少见边界情形）同样静默降级
  // 回退至既有单模型路径（FR-006），不影响目标模型本身已完成的解码结果。
  bool useSpeculative = m_draftModelAvailable;
  const llama_vocab *draftVocab = nullptr;
  if (useSpeculative) {
    draftVocab = llama_model_get_vocab(m_draftModel);
    llama_memory_clear(llama_get_memory(m_draftCtx), true);
    std::vector<llama_token> draftPromptTokens(prompt.size() + 16);
    const int32_t nDraftPromptTokens =
        llama_tokenize(draftVocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), draftPromptTokens.data(),
                        static_cast<int32_t>(draftPromptTokens.size()), /*add_special=*/true, /*parse_special=*/true);
    if (nDraftPromptTokens < 0) {
      spdlog::warn("LlmEngine: 草稿模型提示词 tokenize 失败，本次 generate() 回退至既有单模型路径（FR-006）");
      useSpeculative = false;
    } else {
      draftPromptTokens.resize(static_cast<std::size_t>(nDraftPromptTokens));
      llama_batch draftPromptBatch =
          llama_batch_get_one(draftPromptTokens.data(), static_cast<int32_t>(draftPromptTokens.size()));
      if (llama_decode(m_draftCtx, draftPromptBatch) != 0) {
        spdlog::warn("LlmEngine: 草稿模型提示词 llama_decode 失败，本次 generate() 回退至既有单模型路径（FR-006）");
        useSpeculative = false;
      }
    }
  }

  if (!useSpeculative) {
    // ===== 既有单模型自回归采样循环（003 既有实现，原样不变）=====
    llama_sampler *grammar = llama_sampler_init_grammar(vocab, kOutputGrammar, kOutputGrammarRoot);
    llama_sampler *temp = llama_sampler_init_temp(0.3f);
    llama_sampler *dist = llama_sampler_init_dist(/*seed=*/0);

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
  }

  // ===== T010：投机解码分支（research.md §2 四步循环 + §6 GBNF 双侧约束同步 +
  // §7 固定窗口大小）=====
  //
  // 目标/草稿两侧各自独立的语法采样器实例（research.md §6：两侧状态需随"最终
  // 采纳的 token 序列"同步前进，而非各自独立前进）。
  llama_sampler *draftGrammar = llama_sampler_init_grammar(draftVocab, kOutputGrammar, kOutputGrammarRoot);
  llama_sampler *draftTemp = llama_sampler_init_temp(0.3f);
  llama_sampler *draftDist = llama_sampler_init_dist(/*seed=*/0);
  llama_sampler *targetGrammar = llama_sampler_init_grammar(vocab, kOutputGrammar, kOutputGrammarRoot);
  llama_sampler *targetTemp = llama_sampler_init_temp(0.3f);
  llama_sampler *targetDist = llama_sampler_init_dist(/*seed=*/0);

  llama_memory_t targetMem = llama_get_memory(m_ctx);
  llama_memory_t draftMem = llama_get_memory(m_draftCtx);

  const int32_t nVocabTarget = llama_vocab_n_tokens(vocab);
  const int32_t nVocabDraft = llama_vocab_n_tokens(draftVocab);

  // research.md §7：K（每轮草稿 token 数量）固定为内部常量，不实现自适应调整，
  // 类比既有 kMaxOutputTokens 的实现风格。
  constexpr int kSpeculativeDraftWindow = 4;

  int producedTokens = 0;
  bool stoppedByEog = false;
  bool stoppedByGrammarStall = false;

  while (producedTokens < kMaxOutputTokens) {
    const int windowSize = std::min(kSpeculativeDraftWindow, kMaxOutputTokens - producedTokens);

    // ---- 草稿阶段：草稿模型自回归生成最多 windowSize 个候选 token ----
    // 记录本轮草稿阶段开始前草稿模型 KV cache 的位置（供验证不通过时回滚使
    // 用）；与目标模型侧的 startPos 分别独立维护，不假设两者数值相等（尽管
    // 同一 Qwen2.5 家族共享同一 BPE 分词器，tokenize 同一 prompt 实践中会得到
    // 一致的 token 序列，但独立维护更稳健、不依赖此隐含假设）。
    const llama_pos draftWindowStartPos = llama_memory_seq_pos_max(draftMem, 0) + 1;
    std::vector<llama_token> draftCandidates;
    draftCandidates.reserve(static_cast<std::size_t>(windowSize));
    for (int step = 0; step < windowSize; ++step) {
      const float *draftLogits = llama_get_logits_ith(m_draftCtx, -1);
      std::vector<llama_token_data> draftCandArr(static_cast<std::size_t>(nVocabDraft));
      for (int32_t t = 0; t < nVocabDraft; ++t) {
        draftCandArr[static_cast<std::size_t>(t)] = llama_token_data{t, draftLogits[t], 0.0f};
      }
      llama_token_data_array draftCurP{draftCandArr.data(), draftCandArr.size(), -1, false};
      llama_sampler_apply(draftGrammar, &draftCurP);
      const bool draftAnyLegal = std::any_of(draftCandArr.begin(), draftCandArr.end(),
                                              [](const llama_token_data &d) { return d.logit > -INFINITY; });
      if (!draftAnyLegal) {
        break;  // 草稿模型自身语法梗阻：本轮候选到此为止，不致命
      }
      llama_sampler_apply(draftTemp, &draftCurP);
      llama_sampler_apply(draftDist, &draftCurP);
      const llama_token draftToken = draftCandArr[static_cast<std::size_t>(draftCurP.selected)].id;
      if (llama_vocab_is_eog(draftVocab, draftToken)) {
        break;  // 草稿模型自身预测到结束：本轮候选到此为止，交由目标模型验证已累积部分
      }
      draftCandidates.push_back(draftToken);
      llama_sampler_accept(draftGrammar, draftToken);
      const llama_pos nextDraftPos = llama_memory_seq_pos_max(draftMem, 0) + 1;
      llama_batch draftNextBatch = makeSingleTokenBatch(draftToken, nextDraftPos);
      const int32_t draftRc = llama_decode(m_draftCtx, draftNextBatch);
      llama_batch_free(draftNextBatch);
      if (draftRc != 0) {
        // 草稿模型自身 KV cache 无法继续推进：本轮候选到此为止（已累积的
        // candidates 仍对目标模型验证有效，草稿模型侧的异常不影响目标模型
        // 独立验证/采样的正确性）。
        break;
      }
    }

    if (draftCandidates.empty()) {
      // 草稿模型本轮未能产出任何候选（语法梗阻/EOG/解码失败）：退回目标模型
      // 自身单 token 采样决定当前位置，不浪费一次批量验证的 decode 调用。
      const float *logits = llama_get_logits_ith(m_ctx, -1);
      std::vector<llama_token_data> candidates(static_cast<std::size_t>(nVocabTarget));
      for (int32_t t = 0; t < nVocabTarget; ++t) {
        candidates[static_cast<std::size_t>(t)] = llama_token_data{t, logits[t], 0.0f};
      }
      llama_token_data_array cur_p{candidates.data(), candidates.size(), -1, false};
      llama_sampler_apply(targetGrammar, &cur_p);
      const bool anyLegal = std::any_of(candidates.begin(), candidates.end(),
                                         [](const llama_token_data &d) { return d.logit > -INFINITY; });
      if (!anyLegal) {
        stoppedByGrammarStall = true;
        break;
      }
      llama_sampler_apply(targetTemp, &cur_p);
      llama_sampler_apply(targetDist, &cur_p);
      const llama_token finalToken = cur_p.data[static_cast<std::size_t>(cur_p.selected)].id;
      if (llama_vocab_is_eog(vocab, finalToken)) {
        stoppedByEog = true;
        break;
      }
      // 双侧语法采样器状态同步（research.md §6）：该 token 最终被采纳，两侧
      // 均需 accept 一次。
      llama_sampler_accept(targetGrammar, finalToken);
      llama_sampler_accept(draftGrammar, finalToken);

      char buf[256] = {0};
      const int32_t written = llama_token_to_piece(vocab, finalToken, buf, sizeof(buf), 0, true);
      if (written > 0) {
        result.append(buf, static_cast<std::size_t>(written));
      }
      ++producedTokens;

      const llama_pos nextTargetPos = llama_memory_seq_pos_max(targetMem, 0) + 1;
      llama_batch targetNextBatch = makeSingleTokenBatch(finalToken, nextTargetPos);
      const int32_t targetRc = llama_decode(m_ctx, targetNextBatch);
      llama_batch_free(targetNextBatch);
      if (targetRc != 0) {
        break;
      }
      const llama_pos nextDraftPos2 = llama_memory_seq_pos_max(draftMem, 0) + 1;
      llama_batch draftFollowBatch = makeSingleTokenBatch(finalToken, nextDraftPos2);
      const int32_t draftFollowRc = llama_decode(m_draftCtx, draftFollowBatch);
      llama_batch_free(draftFollowBatch);
      // 草稿模型侧解码失败不致命：下一轮草稿阶段会重新尝试，失败时自然再次
      // 落入本"无候选"分支，目标模型仍可独立完成生成（不影响 FR-001 正确性）。
      (void)draftFollowRc;
      continue;
    }

    // ---- 批量验证阶段：目标模型一次 decode 验证全部草稿候选 ----
    //
    // 关键正确性说明（research.md §2 步骤 2 的因果注意力性质，实现过程中发现
    // 并修复的索引偏移问题）：`llama_decode()` 对批次内位置 i（0-based，对应
    // 已输入的草稿 token d_i）产出的 logits 预测的是"紧跟在 d_0..d_i 之后"的
    // 下一个 token，而非"d_i 本身是否合理"——用于验证草稿候选 d_i 的 logits
    // 必须来自"仅已处理 d_0..d_{i-1}"时的预测结果：验证 d_0 须使用本轮验证
    // 之前（即上一轮末尾或首次提示词解码）已产出的 logits（批次尚未解码时即
    // 可确定，无需等待本次 decode）；验证 d_i（i>=1）须使用批次内位置 i-1 的
    // 输出 logits。若按"批次位置 i 验证 d_i"（即与本次 decode 共享下标）的
    // 错误映射方式实现，会系统性地把"预测下一个 token"的结果误用于验证"当前
    // 位置"，造成每个位置的验证目标相对真实位置整体错位一格，最终导致生成结
    // 果出现"词汇本身合法但顺序错乱/重复"的现象（已通过真实 14B+1.5B 模型实
    // 测复现并定位，修复后以同一测试复核）。
    const int32_t k = static_cast<int32_t>(draftCandidates.size());
    const llama_pos startPos = llama_memory_seq_pos_max(targetMem, 0) + 1;

    // 验证位置 0 所需的"本轮验证前"logits：此刻批次尚未解码，`-1` 仍指向上一
    // 次 decode（上一轮末尾或首次提示词解码）产出的、预测 startPos 位置的结
    // 果，与 d_0 是否被采纳无关，必须在本轮 verifyBatch decode 之前取得。
    auto sampleTargetChoice = [&](const float *logits, bool *outStall) -> llama_token {
      std::vector<llama_token_data> candidates(static_cast<std::size_t>(nVocabTarget));
      for (int32_t t = 0; t < nVocabTarget; ++t) {
        candidates[static_cast<std::size_t>(t)] = llama_token_data{t, logits[t], 0.0f};
      }
      llama_token_data_array cur_p{candidates.data(), candidates.size(), -1, false};
      llama_sampler_apply(targetGrammar, &cur_p);
      const bool anyLegal = std::any_of(candidates.begin(), candidates.end(),
                                         [](const llama_token_data &d) { return d.logit > -INFINITY; });
      if (!anyLegal) {
        *outStall = true;
        return -1;
      }
      llama_sampler_apply(targetTemp, &cur_p);
      llama_sampler_apply(targetDist, &cur_p);
      const llama_token choice = cur_p.data[static_cast<std::size_t>(cur_p.selected)].id;
      // 无论匹配与否，目标模型在该位置的"真实"选择均 MUST 被 accept 一次
      // （否则下一个验证位置的 apply() 会基于过期堆栈状态误判合法性）。
      llama_sampler_accept(targetGrammar, choice);
      return choice;
    };

    bool stallDuringVerify = false;
    const llama_token choice0 = sampleTargetChoice(llama_get_logits_ith(m_ctx, -1), &stallDuringVerify);

    int j = k;  // 默认：全部 k 个草稿 token 均通过验证（无不同位置）
    llama_token replacementToken = -1;
    bool replacementIsEog = false;

    if (stallDuringVerify) {
      j = 0;
    } else if (choice0 != draftCandidates[0]) {
      j = 0;
      replacementToken = choice0;
      replacementIsEog = llama_vocab_is_eog(vocab, choice0);
    }

    // 仅当位置 0 被采纳时才需要真正解码 verifyBatch 以获得位置 1..k-1 的验证
    // 依据；位置 0 即被拒绝/梗阻时，target 侧 KV cache 仍保持本轮验证前的状
    // 态，无需解码、也无需回滚（避免无谓的批量计算开销）。
    if (!stallDuringVerify && choice0 == draftCandidates[0]) {
      llama_batch verifyBatch = llama_batch_init(k, /*embd=*/0, /*n_seq_max=*/1);
      verifyBatch.n_tokens = k;
      for (int32_t i = 0; i < k; ++i) {
        verifyBatch.token[i] = draftCandidates[static_cast<std::size_t>(i)];
        verifyBatch.pos[i] = startPos + i;
        verifyBatch.n_seq_id[i] = 1;
        verifyBatch.seq_id[i][0] = 0;
        verifyBatch.logits[i] = 1;  // 要求每个位置均输出 logits（research.md §2）
      }
      const int32_t decodeRc = llama_decode(m_ctx, verifyBatch);
      if (decodeRc < 0) {
        // 致命错误（research.md §2：< -1 致命错误，-1 非法输入）：无法继续，
        // 返回已累积的部分内容，不崩溃、不抛异常。
        llama_batch_free(verifyBatch);
        spdlog::error("LlmEngine: 投机解码验证阶段 llama_decode 致命错误（rc={}）", decodeRc);
        llama_sampler_free(draftTemp);
        llama_sampler_free(draftDist);
        llama_sampler_free(draftGrammar);
        llama_sampler_free(targetTemp);
        llama_sampler_free(targetDist);
        llama_sampler_free(targetGrammar);
        return QString::fromStdString(result);
      }
      if (decodeRc > 0) {
        // 正值为警告（如未找到 KV slot），非致命，仍尝试读取 logits
        // （research.md §2 行注释要求的"更细粒度处理"）。
        spdlog::warn("LlmEngine: 投机解码验证阶段 llama_decode 返回非致命警告码 {}", decodeRc);
      }

      // 位置 1..k-1：批次内位置 i-1 的输出 logits 预测位置 startPos+i，用于
      // 验证 d_i（上方关键正确性说明）。
      j = k;
      for (int32_t i = 1; i < k; ++i) {
        bool stall = false;
        const llama_token choice = sampleTargetChoice(llama_get_logits_ith(m_ctx, i - 1), &stall);
        if (stall) {
          stallDuringVerify = true;
          j = static_cast<int>(i);
          break;
        }
        if (choice == draftCandidates[static_cast<std::size_t>(i)]) {
          continue;  // 该位置草稿猜测与目标模型真实选择一致，采纳
        }
        j = static_cast<int>(i);
        replacementToken = choice;
        replacementIsEog = llama_vocab_is_eog(vocab, choice);
        break;
      }
      llama_batch_free(verifyBatch);
    }

    // 位置 [0, j) 的草稿 token 已被验证通过，追加到结果（草稿阶段已对
    // draftGrammar 逐一 accept 过，无需在此重复）。
    for (int i = 0; i < j && i < static_cast<int>(draftCandidates.size()); ++i) {
      const llama_token tok = draftCandidates[static_cast<std::size_t>(i)];
      char buf[256] = {0};
      const int32_t written = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, true);
      if (written > 0) {
        result.append(buf, static_cast<std::size_t>(written));
      }
      ++producedTokens;
    }

    if (stallDuringVerify) {
      // 003 既有"语法梗阻提前终止"契约延续（contracts/llm-engine-speculative-
      // internal-contract.md 二.5）：回滚两侧 KV cache 中已不再需要的 KV 条目
      // 后直接返回已累积内容。
      llama_memory_seq_rm(targetMem, 0, startPos + j, -1);
      llama_memory_seq_rm(draftMem, 0, draftWindowStartPos + j, -1);
      stoppedByGrammarStall = true;
      break;
    }

    if (j < k) {
      // 存在不一致位置：回滚两侧 KV cache 中位置 >= startPos+j 的全部内容
      // （research.md §2 步骤 3：这些位置是基于"错误的草稿猜测"计算的，不再
      // 有效）。
      llama_memory_seq_rm(targetMem, 0, startPos + j, -1);
      llama_memory_seq_rm(draftMem, 0, draftWindowStartPos + j, -1);

      if (replacementIsEog) {
        stoppedByEog = true;
        break;
      }

      // 双侧语法采样器状态同步（research.md §6）：替换 token 此前从未被草稿
      // 模型自身生成过，draftGrammar 需额外 accept 一次。
      llama_sampler_accept(draftGrammar, replacementToken);

      char buf[256] = {0};
      const int32_t written = llama_token_to_piece(vocab, replacementToken, buf, sizeof(buf), 0, true);
      if (written > 0) {
        result.append(buf, static_cast<std::size_t>(written));
      }
      ++producedTokens;

      // 替换 token 此前未被任何一侧 decode 过，分别喂给两侧模型完成本轮
      // decode，为下一轮猜测/验证建立正确的 KV cache 状态。
      const llama_pos nextTargetPos = llama_memory_seq_pos_max(targetMem, 0) + 1;
      llama_batch targetReplBatch = makeSingleTokenBatch(replacementToken, nextTargetPos);
      const int32_t targetReplRc = llama_decode(m_ctx, targetReplBatch);
      llama_batch_free(targetReplBatch);
      if (targetReplRc < 0) {
        break;
      }
      const llama_pos nextDraftPos = llama_memory_seq_pos_max(draftMem, 0) + 1;
      llama_batch draftReplBatch = makeSingleTokenBatch(replacementToken, nextDraftPos);
      const int32_t draftReplRc = llama_decode(m_draftCtx, draftReplBatch);
      llama_batch_free(draftReplBatch);
      // 草稿模型侧解码失败不致命（同上，下一轮草稿阶段会自然体现为"无候选"）。
      (void)draftReplRc;
    }
    // j == k（全部 k 个草稿 token 通过验证，无替换）：两侧 KV cache 均已在各
    // 自循环中正确前进至位置 startPos+k-1，无需任何额外处理，直接进入下一轮
    // 外层循环（这正是投机解码"一次批量验证节省 k-1 次目标模型独立前向计算"
    // 的加速来源，research.md §2）。
  }
  spdlog::debug("LlmEngine: 投机解码生成结束（producedTokens={}, stoppedByEog={}, stoppedByGrammarStall={}）",
                producedTokens, stoppedByEog, stoppedByGrammarStall);

  llama_sampler_free(draftTemp);
  llama_sampler_free(draftDist);
  llama_sampler_free(draftGrammar);
  llama_sampler_free(targetTemp);
  llama_sampler_free(targetDist);
  llama_sampler_free(targetGrammar);
  return QString::fromStdString(result);
#else
  Q_UNUSED(normalizedInput);
  return {};
#endif
}

}  // namespace babel::slowpath
