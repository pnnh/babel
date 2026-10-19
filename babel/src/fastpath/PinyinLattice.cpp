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
    // 英文时才保留一个分隔空格，贴合中英混排书写习惯。
    if (!firstCell && prevOutputWasLatin && isLatin) {
      result += QLatin1Char(' ');
    }
    result += surface;
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
    if (!cell.isAmbiguous()) {
      result += cell.candidates.front().surfaceForm;
      continue;
    }
    // contracts/llm-prompt-annotation-contract.md：
    // ⟦原始token:候选1 权重1|候选2 权重2|...|候选N 权重N⟧，仅对歧义格位输出。
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
