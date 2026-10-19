#pragma once

#include <QString>
#include <chrono>
#include <string>
#include <vector>

namespace babel::fastpath {

/// 单个候选条目（data-model.md §1.1），是 `PinyinLatticeCell` 的组成单元。
struct CandidateEntry {
  /// 候选来源，供诊断日志/后续效果分析使用，不向 QML/用户暴露。
  enum class Source {
    FullPinyinTrie,      ///< 全拼 Trie 命中
    AbbrevTrie,          ///< 简拼 Trie 命中
    EditDistanceFallback,  ///< 编辑距离近似匹配回退
    Passthrough,         ///< 非拼音 token 原样透传（恒为单候选）
  };

  QString surfaceForm;          ///< 候选的目标词形（中文词或英文词）
  double rawWeight = 0.0;       ///< 候选来源的原始权重，未归一化
  double normalizedWeight = 0.0;  ///< 同一格位内截断后归一化的比例值（0.0-1.0）
  Source source = Source::Passthrough;
};

/// 拼音候选格位（data-model.md §1.2）：对应原始输入中以空白分隔的一个 token 位置，
/// 取代 001 `NormalizedCandidateText` 中"每个拼音位置仅承载单一候选字符串"的设计
/// （spec FR-006）。
struct PinyinLatticeCell {
  int tokenIndex = 0;        ///< 格位在原始输入中的序号（从 0 开始）
  QString originalToken;     ///< 用户原始击键产生的该 token 文本
  /// 按 `rawWeight` 降序排列、已截断至 Top-K=8（research.md §5.4）的候选列表；
  /// `originalToken` 非拼音（透传格位）时恒为长度 1 的列表，`source = Passthrough`。
  std::vector<CandidateEntry> candidates;

  /// 派生字段：候选数 >= 2 即视为存在真实歧义（`candidates.size() >= 2`）。
  bool isAmbiguous() const { return candidates.size() >= 2; }
};

/// 拼音候选格序列（data-model.md §1.3）：一次 `submitKeystrokes()` 调用对应的完整
/// 格位序列，是 `FastCorrector` 新增对外产出物，取代 001 `NormalizedCandidateText`
/// 作为快流处理结果的主要载体。
struct PinyinLattice {
  std::vector<PinyinLatticeCell> cells;  ///< 按原始输入 token 顺序排列
  std::chrono::system_clock::time_point builtAt{};  ///< 构建时间

  /// 取每个格位 `candidates.front().surfaceForm` 拼接（沿用 001
  /// `FastCorrector::correct()` 既有的"相邻英文 token 间保留空格"拼接规则），
  /// 对外行为等价于 001 `NormalizedCandidateText.content`（FR-008："MUST NOT 向用
  /// 户呈现候选集合及其权重"）。
  QString toDisplayText() const;

  /// 按 `contracts/llm-prompt-annotation-contract.md` 的
  /// `⟦token:候选1 权重1|候选2 权重2|...⟧` 格式渲染，仅对 `isAmbiguous()==true` 的
  /// 格位输出标注，供 `LlmEngine::generate()` 新输入内容使用（FR-007）。
  QString toAnnotatedPromptText() const;
};

}  // namespace babel::fastpath
