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

/// gbnf-grammar-sampling-overhead（bug fix，见 .specify/bugs/
/// gbnf-grammar-sampling-overhead/assessment.md Preferred remediation）：
///
/// 003 阶段的输出语法 `root ::= safe-char*` / `safe-char ::= [^\u27E6\u27E7\x7C]`
/// 仅排除 ⟦（U+27E6）/⟧（U+27E7）/|（U+007C）三个码点，不存在递归规则间的相
/// 互依赖。但旧实现把这一规则接入了 llama.cpp 通用 GBNF 栈式语法引擎
/// （`llama_sampler_init_grammar` + 每个输出 token 一次 `llama_sampler_apply`/
/// `llama_sampler_accept`），导致每个输出 token 都对全词表（3B 模型 151936
/// 个候选）做一次"解码为 UTF-8 + 栈状态转移模拟"，实测把 3B 模型单次
/// generate() 耗时从 002 基线 628ms 推高到约 2000ms（纯文本 prompt，无
/// Lattice 标注）。
///
/// `ForbiddenTokenMask` 把这一步降级为"模型加载完成后一次性预扫描 + 采样循环
/// 内 O(1) 位图查询"：给定 vocab，仅在构造/加载阶段遍历全部 token id，复刻
/// llama.cpp `decode_utf8()`/`llama_grammar_match_partial_char()`
/// （llama-grammar.cpp）对"单 token 从空状态独立解析"情形下的判定逻辑，结果
/// 缓存为按 token id 索引的 `std::vector<bool>`。采样循环不再调用
/// `llama_sampler_apply(grammar, …)`，只需 `isForbidden(id)` 查表即可获得与
/// 原语法引擎完全等价的判定——`anyLegal` 梗阻检测（FR-008）与自然终止逻辑均
/// 不受影响。
///
/// 实现说明（fix.md "Deviations from Assessment" 记录了完整的踩坑过程）：
/// 最初按"token 解码后的字符串是否直接包含 ⟦/⟧/|"这一朴素标准实现，构建后
/// 发现生成质量明显劣化（偶发输出垃圾字节、无法正常以 EOG 终止）。逐行比对
/// llama-grammar.cpp 后定位根因：原语法引擎按**字节**而非按 token 扫描，对
/// 于 BPE 分词产生的"单字节/不完整多字节 UTF-8 前缀"token（常见于稀有字节
/// 回退编码），即使其解码出的字符串不直接包含被禁字符，只要
/// ①该前缀本身就是无效 UTF-8 lead byte（含 0xC0/0xC1 过长编码特判），或
/// ②该前缀未消费完、但其所有可能的补全区间恰好覆盖 ⟦/⟧ 两个码点，
/// 原语法引擎也会保守地判定该 token 非法（防止模型靠跨 token 拼接字节的方
/// 式绕过禁用符号）。本类的 `isForbidden()` 判定逻辑据此扩展，已用独立诊断
/// 探针对 3B/0.5B 两套词表共 151936×2 个 token 做过逐一穷举比对，与
/// `llama_sampler_apply(grammar, …)` 的判定结果 100% 一致（0 处不一致）。
///
/// 草稿模型与目标模型（004 投机解码）是两套独立词表，`build()` 需对两者分别
/// 调用一次，结果不可共用（同一 token id 在两个词表中可能对应完全不同的字
/// 符串）。
///
/// 若未来某个新特性需要真正有状态的语法约束（例如强制输出合法 JSON），静态
/// 黑名单方案不适用——届时应在调用点旁另行引入 `llama_sampler_init_grammar`
/// 等通用引擎，不影响这里的查询接口，本类本身不需要为此预留可扩展性。
class ForbiddenTokenMask {
 public:
  /// 一次性扫描 `vocab` 的全部 token，返回按 token id 索引的禁用位图。调用方
  /// MUST 仅在模型加载完成后调用一次并长期缓存结果（见 `LlmEngine::
  /// m_forbiddenTokenMask`/`m_draftForbiddenTokenMask`），不得放入 `generate()`
  /// 的采样循环或每次 `generate()` 调用路径——否则会重新引入本 bug 试图消除
  /// 的"每 token/每次调用全词表扫描"开销。
  static std::vector<bool> build(const llama_vocab *vocab) {
    const int32_t nVocab = llama_vocab_n_tokens(vocab);
    std::vector<bool> mask(static_cast<std::size_t>(std::max(nVocab, 0)), false);
    char buf[256];
    for (int32_t t = 0; t < nVocab; ++t) {
      // 与原语法引擎一致：EOG token 永远合法（kOutputGrammar 的 `root` 定义
      // 允许零个字符，对应 `allow_eog` 在该语法下恒为 true），不受后续字节
      // 级判定影响。
      if (llama_vocab_is_eog(vocab, t)) {
        continue;
      }
      const int32_t written = llama_token_to_piece(vocab, t, buf, sizeof(buf), /*lstrip=*/0, /*special=*/true);
      // 空字符串/以 NUL 开头：与 llama_grammar_apply_impl 的
      // `piece.empty() || piece[0] == 0` 分支一致，直接判非法。
      if (written <= 0 || buf[0] == '\0') {
        mask[static_cast<std::size_t>(t)] = true;
        continue;
      }
      const std::string piece(buf, static_cast<std::size_t>(written));
      if (isForbiddenByUtf8Structure(piece)) {
        mask[static_cast<std::size_t>(t)] = true;
      }
    }
    return mask;
  }

  /// O(1) 查询：token id 是否命中禁用黑名单。下标越界（理论上不应发生，因为
  /// `mask` 与对应 vocab 的 `llama_vocab_n_tokens()` 等长）保守判定为"未禁
  /// 用"，调用方仍需通过 logits 数组本身的合法范围来保证不会越界访问。
  static bool isForbidden(const std::vector<bool> &mask, llama_token id) {
    const auto idx = static_cast<std::size_t>(id);
    return idx < mask.size() && mask[idx];
  }

 private:
  /// 复刻 llama-grammar.cpp `decode_utf8(src, partial_start={0,0})` 从干净状
  /// 态逐字节消费 + `llama_grammar_match_partial_char()` 的保守判定：
  /// - 完整解码出 ⟦（U+27E6）/⟧（U+27E7）/|（U+007C）中任一码点 → 非法；
  /// - 出现无效 lead byte（含 0xC0/0xC1 过长编码特判）→ 非法；
  /// - 结尾处残留未消费完的多字节前缀，其所有可能补全区间覆盖 ⟦/⟧ 任一码点
  ///   → 非法（原语法引擎为防止跨 token 拼接禁用符号而采取的保守策略）；
  /// - 其余情况 → 合法。
  static bool isForbiddenByUtf8Structure(const std::string &piece) {
    static constexpr int kLookup[16] = {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 2, 2, 3, 4};
    std::size_t i = 0;
    const std::size_t n = piece.size();
    while (i < n) {
      const unsigned char first = static_cast<unsigned char>(piece[i]);
      const int highbits = first >> 4;
      int nRemain = kLookup[highbits] - 1;
      if (nRemain < 0) {
        return true;  // 裸露延续字节（0x80-0xBF）等无效 lead byte
      }
      const unsigned int mask = (1u << (7 - nRemain)) - 1u;
      uint32_t value = first & mask;
      ++i;
      while (i < n && nRemain > 0) {
        value = (value << 6) + (static_cast<unsigned char>(piece[i]) & 0x3Fu);
        ++i;
        --nRemain;
      }
      if (nRemain == 0) {
        if (value == kCodepointLeftBracket || value == kCodepointRightBracket || value == kCodepointPipe) {
          return true;  // 完整解码出被禁码点
        }
        continue;  // 合法完整字符，继续解析下一个
      }
      // 结尾处尚有未消费完的多字节前缀：复刻 match_partial_char 的判定。
      if (nRemain == 1 && value < 2) {
        return true;  // 0xC0/0xC1 式过长编码 lead byte
      }
      uint32_t low = value << (nRemain * 6);
      const uint32_t high = low | ((1u << (nRemain * 6)) - 1u);
      if (low == 0) {
        if (nRemain == 2) {
          low = 1u << 11;
        } else if (nRemain == 3) {
          low = 1u << 16;
        }
      }
      if ((low <= kCodepointLeftBracket && kCodepointLeftBracket <= high) ||
          (low <= kCodepointRightBracket && kCodepointRightBracket <= high)) {
        return true;  // 未完成前缀仍可能补全为 ⟦/⟧，保守判为禁用
      }
      break;  // 未命中禁用范围，结尾不完整序列按"仍可能合法"处理
    }
    return false;
  }

  static constexpr uint32_t kCodepointLeftBracket = 0x27E6;   // ⟦
  static constexpr uint32_t kCodepointRightBracket = 0x27E7;  // ⟧
  static constexpr uint32_t kCodepointPipe = 0x7C;            // |
};

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

  // gbnf-grammar-sampling-overhead 修复：模型加载完成、词表确定后立即一次性
  // 预计算目标模型的禁用 token 位图（而非每次 generate() 或每个输出 token 内
  // 重新计算），供后续所有 generate() 调用的采样循环 O(1) 查表复用。
  m_forbiddenTokenMask = ForbiddenTokenMask::build(llama_model_get_vocab(m_model));

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
            // 草稿模型词表与目标模型词表是两套独立词表（004 投机解码），禁用
            // 位图不可共用，需单独预计算、单独缓存（见 LlmEngine.h
            // m_draftForbiddenTokenMask 注释）。
            m_draftForbiddenTokenMask = ForbiddenTokenMask::build(llama_model_get_vocab(m_draftModel));
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

  // 输出语法约束（FR-005～FR-009，data-model.md 三 Output Grammar Rule）：仅
  // 排除 ⟦（U+27E6）/⟧（U+27E7）/|（U+007C）三个 Lattice 内部标注符号，"最宽
  // 边界"——不收窄其余任意 Unicode 字符（汉字/中文标点/数字/字母/全半角标
  // 点/emoji 均合法）。
  //
  // gbnf-grammar-sampling-overhead 修复：原实现用一条等价于
  // `root ::= safe-char*` / `safe-char ::= [^\u27E6\u27E7\x7C]` 的 GBNF 语法
  // 字符串接入 `llama_sampler_init_grammar` 通用语法引擎，但该规则本身无递
  // 归、无跨步骤状态依赖，用通用引擎实现属于性能反模式（实测 3B 模型单次
  // generate() 从 002 基线 628ms 推高到约 2000ms，见 assessment.md）。现改为
  // 直接查询 `m_forbiddenTokenMask`/`m_draftForbiddenTokenMask`（模型加载时
  // 由 `ForbiddenTokenMask::build()` 一次性预计算，见上方匿名命名空间），采
  // 样循环内 O(1) 查表即可获得等价的屏蔽效果，不再需要 GBNF 语法字符串/
  // grammar 采样器。若未来需要真正有状态的语法约束，可在此处另行引入
  // `llama_sampler_init_grammar`，不影响其余采样流程。

  // 放弃现有 llama_sampler_chain_init/llama_sampler_chain_add 一体式链条封装，
  // 改为手动分步调用：黑名单掩码应用之后、temp/dist 采样器 apply 之前，显式
  // 检查候选集合是否已全部被掩码为 -inf（"语法梗阻"，FR-008）。
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
    // ===== 既有单模型自回归采样循环 =====
    llama_sampler *temp = llama_sampler_init_temp(0.3f);
    llama_sampler *dist = llama_sampler_init_dist(/*seed=*/0);

    for (int i = 0; i < kMaxOutputTokens; ++i) {
      const float *logits = llama_get_logits_ith(m_ctx, -1);
      const int32_t nVocab = llama_vocab_n_tokens(vocab);
      std::vector<llama_token_data> candidates(static_cast<std::size_t>(nVocab));
      for (int32_t t = 0; t < nVocab; ++t) {
        // 静态禁用黑名单查表（O(1)），替代原 llama_sampler_apply(grammar, …)：
        // 命中 ⟦/⟧/| 的 token 直接置 -inf，其余 token 的 logit 原样保留。
        const float logit = ForbiddenTokenMask::isForbidden(m_forbiddenTokenMask, t) ? -INFINITY : logits[t];
        candidates[static_cast<std::size_t>(t)] = llama_token_data{t, logit, 0.0f};
      }
      llama_token_data_array cur_p{candidates.data(), candidates.size(), -1, false};

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
    return QString::fromStdString(result);
  }

  // ===== T010：投机解码分支（research.md §2 四步循环 + §7 固定窗口大小）=====
  //
  // 目标/草稿两侧分别使用各自预计算好的禁用位图（m_forbiddenTokenMask /
  // m_draftForbiddenTokenMask，两套独立词表不可共用同一份黑名单），不再需要
  // 为每一侧单独维护语法采样器实例/状态。
  llama_sampler *draftTemp = llama_sampler_init_temp(0.3f);
  llama_sampler *draftDist = llama_sampler_init_dist(/*seed=*/0);
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
        // 草稿模型侧使用其自身词表预计算的禁用位图（两套词表不可共用）。
        const float logit = ForbiddenTokenMask::isForbidden(m_draftForbiddenTokenMask, t) ? -INFINITY : draftLogits[t];
        draftCandArr[static_cast<std::size_t>(t)] = llama_token_data{t, logit, 0.0f};
      }
      llama_token_data_array draftCurP{draftCandArr.data(), draftCandArr.size(), -1, false};
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
        const float logit = ForbiddenTokenMask::isForbidden(m_forbiddenTokenMask, t) ? -INFINITY : logits[t];
        candidates[static_cast<std::size_t>(t)] = llama_token_data{t, logit, 0.0f};
      }
      llama_token_data_array cur_p{candidates.data(), candidates.size(), -1, false};
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
        const float logit = ForbiddenTokenMask::isForbidden(m_forbiddenTokenMask, t) ? -INFINITY : logits[t];
        candidates[static_cast<std::size_t>(t)] = llama_token_data{t, logit, 0.0f};
      }
      llama_token_data_array cur_p{candidates.data(), candidates.size(), -1, false};
      const bool anyLegal = std::any_of(candidates.begin(), candidates.end(),
                                         [](const llama_token_data &d) { return d.logit > -INFINITY; });
      if (!anyLegal) {
        *outStall = true;
        return -1;
      }
      llama_sampler_apply(targetTemp, &cur_p);
      llama_sampler_apply(targetDist, &cur_p);
      const llama_token choice = cur_p.data[static_cast<std::size_t>(cur_p.selected)].id;
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
        llama_sampler_free(targetTemp);
        llama_sampler_free(targetDist);
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

    // 位置 [0, j) 的草稿 token 已被验证通过，追加到结果（黑名单为无状态静态
    // 查表，无需对已采纳 token 做任何"accept 推进"操作）。
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

      // 替换 token（choice0/choice 的直接采样结果）已经是从目标模型"应用禁用
      // 位图后的候选集合"中选出的合法 token，静态黑名单无状态，无需任何
      // accept 推进操作。

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
  llama_sampler_free(targetTemp);
  llama_sampler_free(targetDist);
  return QString::fromStdString(result);
#else
  Q_UNUSED(normalizedInput);
  return {};
#endif
}

}  // namespace babel::slowpath
