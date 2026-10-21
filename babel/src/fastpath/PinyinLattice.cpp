#include "babel/src/fastpath/PinyinLattice.h"

namespace babel::fastpath {

namespace {

bool looksLikeLatin(const QString &token) {
  if (token.isEmpty()) {
    return false;
  }
  for (const QChar &ch : token) {
    if (!ch.isLetter() || ch.unicode() > 127) {
      return false;
    }
  }
  return true;
}

/// bugfix: english-word-pinyin-misdetection 候选方向 4（纵深防御）：判断某格位
/// 是否属于"无歧义但高风险"的坍缩场景——候选数恰为 1，且该唯一候选标记了
/// `latinOriginLengthMismatch`（即来自拼音匹配管线对全字母 token 的非精确长度
/// 匹配，典型如 `want`→万台、`meeting`→客厅）。此类格位即便技术上"无歧义"，
/// 也不应完全丢失原始 token 的痕迹（assessment.md 风险条目）。
bool isRiskyLatinCollapse(const PinyinLatticeCell &cell) {
  return !cell.isAmbiguous() && cell.candidates.front().latinOriginLengthMismatch;
}

}  // namespace

QString PinyinLattice::toDisplayText() const {
  QString result;
  bool prevOutputWasLatin = false;
  bool firstCell = true;
  for (const auto &cell : cells) {
    if (cell.candidates.empty()) {
      continue;
    }
    const QString &surface = cell.candidates.front().surfaceForm;
    const bool isLatin = looksLikeLatin(surface);
    // 沿用 001 FastCorrector::correct() 既有拼接规则：仅当相邻两个输出 token 均为
    // 英文时才保留一个分隔空格，贴合中英混排书写习惯。空格插入判断仅依据
    // `surface` 本身（不含下方追加的原始 token 痕迹后缀），保持既有拼接行为。
    if (!firstCell && prevOutputWasLatin && isLatin) {
      result += QLatin1Char(' ');
    }
    result += surface;
    if (isRiskyLatinCollapse(cell) && cell.originalToken != surface) {
      // 仅追加原始 token 本身（不含候选集合/权重），不违反 FR-008"MUST NOT 向
      // 用户呈现候选集合及其权重"；`cell.originalToken != surface` 排除"无命中
      // 原样保留"（surfaceForm 恒等于 originalToken，无信息丢失，无需追加）。
      result += QLatin1Char('(');
      result += cell.originalToken;
      result += QLatin1Char(')');
    }
    prevOutputWasLatin = isLatin;
    firstCell = false;
  }
  return result;
}

QString PinyinLattice::toAnnotatedPromptText() const {
  QString result;
  for (const auto &cell : cells) {
    if (cell.candidates.empty()) {
      continue;
    }
    if (!cell.isAmbiguous() && !isRiskyLatinCollapse(cell)) {
      result += cell.candidates.front().surfaceForm;
      continue;
    }
    // contracts/llm-prompt-annotation-contract.md：
    // ⟦原始token:候选1 权重1|候选2 权重2|...|候选N 权重N⟧。
    // 原本仅对歧义格位（candidates.size()>=2）输出；bugfix:
    // english-word-pinyin-misdetection 候选方向 4 额外对"无歧义但高风险坍缩"
    // 的单候选格位（`isRiskyLatinCollapse()`）同样输出该标注（候选列表长度为
    // 1），确保慢流 LLM 不会完全看不到原始击键内容。
    result += QChar(0x27E6);  // ⟦
    result += cell.originalToken;
    result += QLatin1Char(':');
    for (std::size_t i = 0; i < cell.candidates.size(); ++i) {
      if (i > 0) {
        result += QLatin1Char('|');
      }
      const auto &candidate = cell.candidates[i];
      result += candidate.surfaceForm;
      result += QLatin1Char(' ');
      result += QString::asprintf("%.2f", candidate.normalizedWeight);
    }
    result += QChar(0x27E7);  // ⟧
  }
  return result;
}

}  // namespace babel::fastpath
