#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "babel/src/fastpath/PinyinLattice.h"

using babel::fastpath::CandidateEntry;
using babel::fastpath::PinyinLattice;
using babel::fastpath::PinyinLatticeCell;

// T013 [US2]：PinyinLattice 单元测试（data-model.md §1，research.md §5.2/§5.3/§5.4，
// contracts/llm-prompt-annotation-contract.md）。先写测试，确认其在
// toDisplayText()/toAnnotatedPromptText() 实现完成前处于 FAIL 状态。

namespace {

CandidateEntry makeCandidate(const QString &surface, double rawWeight,
                              CandidateEntry::Source source = CandidateEntry::Source::FullPinyinTrie) {
  CandidateEntry c;
  c.surfaceForm = surface;
  c.rawWeight = rawWeight;
  c.source = source;
  return c;
}

}  // namespace

TEST(PinyinLatticeTest, IsAmbiguousReflectsCandidateCount) {
  PinyinLatticeCell single;
  single.candidates = {makeCandidate(QStringLiteral("吃"), 100.0)};
  EXPECT_FALSE(single.isAmbiguous());

  PinyinLatticeCell multi;
  multi.candidates = {makeCandidate(QStringLiteral("吃"), 100.0), makeCandidate(QStringLiteral("持"), 50.0)};
  EXPECT_TRUE(multi.isAmbiguous());
}

TEST(PinyinLatticeTest, PassthroughCellIsAlwaysSingleCandidateWeightOne) {
  PinyinLatticeCell cell;
  cell.tokenIndex = 0;
  cell.originalToken = QStringLiteral("，");
  CandidateEntry c;
  c.surfaceForm = QStringLiteral("，");
  c.rawWeight = 1.0;
  c.normalizedWeight = 1.0;
  c.source = CandidateEntry::Source::Passthrough;
  cell.candidates = {c};

  ASSERT_EQ(cell.candidates.size(), 1u);
  EXPECT_EQ(cell.candidates.front().source, CandidateEntry::Source::Passthrough);
  EXPECT_DOUBLE_EQ(cell.candidates.front().normalizedWeight, 1.0);
  EXPECT_FALSE(cell.isAmbiguous());
}

TEST(PinyinLatticeTest, ToDisplayTextTakesHighestWeightCandidatePerCell) {
  PinyinLattice lattice;

  PinyinLatticeCell cell0;
  cell0.tokenIndex = 0;
  cell0.originalToken = QStringLiteral("wo");
  cell0.candidates = {makeCandidate(QStringLiteral("我"), 100.0)};

  PinyinLatticeCell cell1;
  cell1.tokenIndex = 1;
  cell1.originalToken = QStringLiteral("chi");
  cell1.candidates = {makeCandidate(QStringLiteral("吃"), 0.42 * 100), makeCandidate(QStringLiteral("持"), 0.35 * 100),
                       makeCandidate(QStringLiteral("迟"), 0.15 * 100)};

  PinyinLatticeCell cell2;
  cell2.tokenIndex = 2;
  cell2.originalToken = QStringLiteral("idea");
  cell2.candidates = {makeCandidate(QStringLiteral("idea"), 80.0, CandidateEntry::Source::Passthrough)};

  lattice.cells = {cell0, cell1, cell2};

  // 相邻英文 token 间保留空格（沿用 001 FastCorrector::correct() 既有拼接规则）；
  // 规则仅在"上一个输出 token"与"当前输出 token"均为英文时才插入空格——"吃"
  // （中文）后紧跟 "idea"（英文）不满足该条件，故无空格："我吃idea"。
  EXPECT_EQ(lattice.toDisplayText().toStdString(), std::string("我吃idea"));
}

TEST(PinyinLatticeTest, ToAnnotatedPromptTextOnlyAnnotatesAmbiguousCells) {
  PinyinLattice lattice;

  PinyinLatticeCell cell0;
  cell0.originalToken = QStringLiteral("wo");
  cell0.candidates = {makeCandidate(QStringLiteral("我"), 100.0)};

  PinyinLatticeCell cell1;
  cell1.originalToken = QStringLiteral("chi");
  CandidateEntry chi1 = makeCandidate(QStringLiteral("吃"), 42.0);
  chi1.normalizedWeight = 0.42;
  CandidateEntry chi2 = makeCandidate(QStringLiteral("持"), 35.0);
  chi2.normalizedWeight = 0.35;
  CandidateEntry chi3 = makeCandidate(QStringLiteral("迟"), 15.0);
  chi3.normalizedWeight = 0.15;
  cell1.candidates = {chi1, chi2, chi3};

  PinyinLatticeCell cell2;
  cell2.originalToken = QStringLiteral("le");
  cell2.candidates = {makeCandidate(QStringLiteral("了"), 100.0)};

  lattice.cells = {cell0, cell1, cell2};

  const QString annotated = lattice.toAnnotatedPromptText();
  EXPECT_EQ(annotated.toStdString(), std::string("我⟦chi:吃 0.42|持 0.35|迟 0.15⟧了"));
}

TEST(PinyinLatticeTest, CandidatesTruncatedToTopKEightByRawWeightDescending) {
  // research.md §5.4：每格位最多保留按 rawWeight 降序排列的前 K=8 个候选，
  // normalizedWeight 基于截断后子集重新计算（近似和为 1.00）。
  PinyinLatticeCell cell;
  cell.originalToken = QStringLiteral("yi");
  std::vector<CandidateEntry> raw;
  for (int i = 0; i < 12; ++i) {
    raw.push_back(makeCandidate(QStringLiteral("候选%1").arg(i), 12.0 - i));
  }
  // 模拟 FastCorrector::buildLattice() 的截断+归一化逻辑（此处直接构造截断后状态，
  // 因为 PinyinLattice 本身只是数据结构，截断发生在 FastCorrector 侧，T019）。
  std::sort(raw.begin(), raw.end(), [](const auto &a, const auto &b) { return a.rawWeight > b.rawWeight; });
  raw.resize(8);
  double sum = 0.0;
  for (const auto &c : raw) sum += c.rawWeight;
  for (auto &c : raw) c.normalizedWeight = c.rawWeight / sum;
  cell.candidates = raw;

  EXPECT_EQ(cell.candidates.size(), 8u);
  double normalizedSum = 0.0;
  for (const auto &c : cell.candidates) normalizedSum += c.normalizedWeight;
  EXPECT_NEAR(normalizedSum, 1.00, 0.01);
  // 候选0权重最高（12.0），应排在首位。
  EXPECT_EQ(cell.candidates.front().surfaceForm.toStdString(), std::string("候选0"));
}
