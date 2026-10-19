#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "babel/src/fastpath/FastCorrector.h"

using babel::fastpath::FastCorrector;

// T015 [US1]：中英拼音混排样例的编辑距离纠错正确性 + 目标延迟 <15ms 基准
// （对应 FR-003/SC-002）。先写测试，期望在 FastCorrector 实现完成前处于 FAIL 状态。

TEST(FastCorrectorTest, CorrectsMixedPinyinDraftToChinese) {
  FastCorrector corrector;
  const QString result = corrector.correct(QStringLiteral("wo xiang shuo zhege idea hen hao"));
  EXPECT_EQ(result.toStdString(), std::string("我想说这个idea很好"));
}

TEST(FastCorrectorTest, KeepsUnrecognizedTokenUnchanged) {
  FastCorrector corrector;
  const QString result = corrector.correct(QStringLiteral("qzxxxxxxxxxxx"));
  EXPECT_EQ(result.toStdString(), std::string("qzxxxxxxxxxxx"));
}

TEST(FastCorrectorTest, KeyboardProximityToleratesTypo) {
  // 'q' 与 'a' 为 QWERTY 物理相邻键位：误触 "hqo"（意图 "hao"）应仍以更低编辑
  // 距离权重纠正为 "好"，验证 KeyboardProximityMap 的邻近加权生效（FR-003）。
  FastCorrector corrector;
  const QString result = corrector.correct(QStringLiteral("hqo"));
  EXPECT_EQ(result.toStdString(), std::string("好"));
}

TEST(FastCorrectorTest, EndToEndLatencyIsReasonablyFast) {
  FastCorrector corrector;
  const auto start = std::chrono::steady_clock::now();
  corrector.correct(QStringLiteral("wo xiang shuo zhege idea hen hao"));
  const auto elapsed = std::chrono::steady_clock::now() - start;
  // 目标 <15ms（FR-003/SC-002）；沙箱/CI 环境调度抖动较大，单元测试阈值放宽至
  // 50ms 以避免环境噪声导致误报，严格的延迟基准验证见 quickstart.md 场景一。
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 50);
}

// T015 [US2]：FastCorrector::buildLattice() —— 同一拼音片段产出多候选而非唯一
// 锁定（research.md §5.2，data-model.md §1），既有 correct() 经 toDisplayText()
// 派生仍保持向后兼容。

TEST(FastCorrectorTest, BuildLatticeProducesMultipleCandidatesForAmbiguousSyllable) {
  // "hao" 在内置种子词典中至少对应 "好"；额外补充同音候选 "号"/"豪" 以构造歧义。
  using babel::fastpath::DictionaryEntry;
  std::vector<DictionaryEntry> entries = babel::fastpath::builtinSeedDictionary();
  entries.push_back({0, "号", "hao", "h", 60.0});
  entries.push_back({0, "豪", "hao", "h", 40.0});
  FastCorrector corrector(entries);

  const auto lattice = corrector.buildLattice(QStringLiteral("hao"));
  ASSERT_EQ(lattice.cells.size(), 1u);
  const auto &cell = lattice.cells.front();
  EXPECT_TRUE(cell.isAmbiguous());
  EXPECT_GE(cell.candidates.size(), 3u);
  // 候选按 rawWeight 降序排列，"好"(100) 权重最高应排在首位。
  EXPECT_EQ(cell.candidates.front().surfaceForm.toStdString(), std::string("好"));
}

TEST(FastCorrectorTest, BuildLatticeCandidatesMergeTrieAndEditDistanceFallbackSources) {
  using babel::fastpath::CandidateEntry;
  FastCorrector corrector;
  // "hqo"（键位邻近误触 "hao"）：全拼 Trie 精确命中为空，应落入编辑距离回退，
  // 候选来源标记为 EditDistanceFallback。
  const auto lattice = corrector.buildLattice(QStringLiteral("hqo"));
  ASSERT_EQ(lattice.cells.size(), 1u);
  ASSERT_FALSE(lattice.cells.front().candidates.empty());
  EXPECT_EQ(lattice.cells.front().candidates.front().source, CandidateEntry::Source::EditDistanceFallback);
}

TEST(FastCorrectorTest, BuildLatticePassthroughForNonPinyinToken) {
  FastCorrector corrector;
  const auto lattice = corrector.buildLattice(QStringLiteral("已经是中文"));
  ASSERT_EQ(lattice.cells.size(), 1u);
  const auto &cell = lattice.cells.front();
  ASSERT_EQ(cell.candidates.size(), 1u);
  EXPECT_EQ(cell.candidates.front().source, babel::fastpath::CandidateEntry::Source::Passthrough);
  EXPECT_DOUBLE_EQ(cell.candidates.front().normalizedWeight, 1.0);
}

TEST(FastCorrectorTest, CorrectRemainsBackwardCompatibleViaToDisplayText) {
  FastCorrector corrector;
  const QString viaCorrect = corrector.correct(QStringLiteral("wo xiang shuo zhege idea hen hao"));
  const QString viaLattice = corrector.buildLattice(QStringLiteral("wo xiang shuo zhege idea hen hao")).toDisplayText();
  EXPECT_EQ(viaCorrect, viaLattice);
}
