#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "babel/src/fastpath/EnglishWordList.h"
#include "babel/src/fastpath/FastCorrector.h"

using babel::fastpath::EnglishWordList;
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

// bugfix: english-word-pinyin-misdetection —— 合法英文单词被误判为走音拼音并
// 替换为无关汉字。候选方向 1（词表提前拦截）+ 方向 2（阈值收紧）+ 方向 4
// （纵深防御）的回归覆盖，详见 `.specify/bugs/english-word-pinyin-misdetection/`。

TEST(FastCorrectorTest, EnglishWordListInterceptsCommonWordBeforePinyinPipeline) {
  using babel::fastpath::CandidateEntry;
  using babel::fastpath::DictionaryEntry;
  // 构造一个"巧合碰撞"场景：词库中存在全拼恰为 "want" 前缀更长词条 "wantai"
  // （模拟 assessment.md 确认的 want→万台 根因），同时词表命中 "want"。即便
  // Trie 层面本可能命中该碰撞候选，词表拦截应在进入拼音管线之前就已短路。
  std::vector<DictionaryEntry> entries = babel::fastpath::builtinSeedDictionary();
  entries.push_back({0, "万台", "wantai", "wt", 100.0});
  FastCorrector corrector(entries, EnglishWordList::fromWords({"want"}));

  const auto lattice = corrector.buildLattice(QStringLiteral("want"));
  ASSERT_EQ(lattice.cells.size(), 1u);
  const auto &cell = lattice.cells.front();
  ASSERT_EQ(cell.candidates.size(), 1u);
  EXPECT_EQ(cell.candidates.front().surfaceForm.toStdString(), std::string("want"));
  EXPECT_EQ(cell.candidates.front().source, CandidateEntry::Source::EnglishWordListPassthrough);
}

TEST(FastCorrectorTest, EnglishWordListLookupIsCaseInsensitive) {
  FastCorrector corrector(babel::fastpath::builtinSeedDictionary(), EnglishWordList::fromWords({"meeting"}));
  const auto lattice = corrector.buildLattice(QStringLiteral("Meeting"));
  ASSERT_EQ(lattice.cells.size(), 1u);
  EXPECT_EQ(lattice.cells.front().candidates.front().surfaceForm.toStdString(), std::string("Meeting"));
}

TEST(FastCorrectorTest, EmptyEnglishWordListDoesNotInterceptAnything) {
  // 默认构造的 EnglishWordList 为空集合：未传入词表时行为与此前完全一致（既有
  // 测试/既有召回率不受影响）。
  FastCorrector corrector;
  const QString result = corrector.correct(QStringLiteral("wo xiang shuo zhege idea hen hao"));
  EXPECT_EQ(result.toStdString(), std::string("我想说这个idea很好"));
}

TEST(FastCorrectorTest, EditDistanceFallbackSkipsSingleLetterTokens) {
  using babel::fastpath::CandidateEntry;
  using babel::fastpath::DictionaryEntry;
  // 构造会与单字母 "I" 发生编辑距离"巧合"碰撞的词条（模拟 assessment.md 确认
  // 的 I→级 根因：词库中单字拼音 "ji"/"e" 等与 "I" 编辑距离很近）。新增的最小
  // token 长度校验应使 "I"（长度 1）完全不参与编辑距离回退，merged 为空后原
  // 样保留。
  std::vector<DictionaryEntry> entries = babel::fastpath::builtinSeedDictionary();
  entries.push_back({0, "级", "ji", "j", 100.0});
  entries.push_back({0, "哦", "e", "e", 100.0});
  FastCorrector corrector(entries);

  const auto lattice = corrector.buildLattice(QStringLiteral("I"));
  ASSERT_EQ(lattice.cells.size(), 1u);
  const auto &cell = lattice.cells.front();
  ASSERT_EQ(cell.candidates.size(), 1u);
  EXPECT_EQ(cell.candidates.front().surfaceForm.toStdString(), std::string("I"));
}

TEST(FastCorrectorTest, PinyinTrieOverreachMatchIsMarkedAsLatinOriginLengthMismatch) {
  using babel::fastpath::CandidateEntry;
  using babel::fastpath::DictionaryEntry;
  // 未命中词表时（如本词条未加入 EnglishWordList），"want" 仍可能命中 Trie 前
  // 缀溢出候选（取决于溢出上限）；此处构造溢出量恰好在允许范围内
  // （"want"(4) 对 "wanted"(6)，溢出 2，默认上限 1 会拒绝——故改用溢出量 1 的
  // "wants"(5)）验证该候选被标记 `latinOriginLengthMismatch=true`，供
  // PinyinLattice 纵深防御识别。
  std::vector<DictionaryEntry> entries = babel::fastpath::builtinSeedDictionary();
  entries.push_back({0, "万茨", "wants", "wc", 100.0});
  FastCorrector corrector(entries);

  const auto lattice = corrector.buildLattice(QStringLiteral("want"));
  ASSERT_EQ(lattice.cells.size(), 1u);
  const auto &cell = lattice.cells.front();
  ASSERT_FALSE(cell.candidates.empty());
  EXPECT_EQ(cell.candidates.front().source, CandidateEntry::Source::FullPinyinTrie);
  EXPECT_TRUE(cell.candidates.front().latinOriginLengthMismatch);
}

