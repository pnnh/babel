#include "babel/src/fastpath/FastCorrector.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "babel/src/fastpath/KeyboardProximityMap.h"

namespace babel::fastpath {

namespace {
bool looksLikeLatin(const std::string &token) {
  return !token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isalpha(ch) != 0; });
}

/// 近似匹配回退阈值：与 001 既有实现一致（FR-003 容错范围）。
constexpr double kMaxAcceptableDistance = 1.5;

/// Top-K 候选规模上限（research.md §5.4，用户委派 Agent 采纳 Recommendation，
/// 实测前起点，待 SC-003 实测结果调参）。
constexpr std::size_t kTopKCandidates = 8;
}  // namespace

FastCorrector::FastCorrector(std::vector<DictionaryEntry> entries) : m_entries(std::move(entries)) {
  m_trie.build(m_entries);
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    if (!m_entries[i].pinyinFull.empty()) {
      m_entriesByPinyinLength[m_entries[i].pinyinFull.size()].push_back(i);
    }
  }
  // 按 frequencyWeight 降序排列每个长度桶内的下标：编辑距离回退命中 kMaxDpEvaluations
  // 安全阀截断时，优先消耗评估预算于高频词条，最大化在有限 DP 调用次数内找到
  // 高质量候选的概率（而非任由词库文件加载顺序决定评估优先级）。
  for (auto &[length, indices] : m_entriesByPinyinLength) {
    std::sort(indices.begin(), indices.end(), [this](std::size_t a, std::size_t b) {
      return m_entries[a].frequencyWeight > m_entries[b].frequencyWeight;
    });
  }
}

double FastCorrector::weightedEditDistance(const std::string &a, const std::string &b) const {
  const std::size_t n = a.size();
  const std::size_t m = b.size();
  std::vector<std::vector<double>> dp(n + 1, std::vector<double>(m + 1, 0.0));
  for (std::size_t i = 0; i <= n; ++i) {
    dp[i][0] = static_cast<double>(i);
  }
  for (std::size_t j = 0; j <= m; ++j) {
    dp[0][j] = static_cast<double>(j);
  }
  for (std::size_t i = 1; i <= n; ++i) {
    for (std::size_t j = 1; j <= m; ++j) {
      const double substCost = KeyboardProximityMap::proximityWeight(a[i - 1], b[j - 1]);
      dp[i][j] = std::min({
          dp[i - 1][j] + 1.0,            // 删除
          dp[i][j - 1] + 1.0,            // 插入
          dp[i - 1][j - 1] + substCost,  // 替换（含键位邻近加权）
      });
    }
  }
  return dp[n][m];
}

std::vector<CandidateEntry> FastCorrector::editDistanceFallbackCandidates(const std::string &token) const {
  std::vector<CandidateEntry> result;
  const std::size_t tokenLen = token.size();
  if (tokenLen == 0) {
    return result;
  }
  // 长度差超过可接受阈值时，编辑距离下界（至少需要该数量次插入/删除）必然超过
  // kMaxAcceptableDistance，可安全跳过而不改变最终结果（纯性能优化）。
  const std::size_t maxLenDelta = static_cast<std::size_t>(kMaxAcceptableDistance);
  const std::size_t lowLen = (tokenLen > maxLenDelta) ? (tokenLen - maxLenDelta) : 0;
  const std::size_t highLen = tokenLen + maxLenDelta;
  const char tokenFirstChar = token.front();
  // 真实词库规模下（research.md §1，数十万条），单纯按长度分桶仍可能在某些
  // 长度桶留下数以万计的候选，对每一个都执行完整 DP 不可接受（曾实测单次
  // buildLattice() 调用触及该病态情形耗时达数秒，远超 FR-003/SC-002 的
  // <15ms 目标）。补充一条低成本启发式预筛：仅对"首字符相同或 QWERTY 键位
  // 邻近"的候选词条才执行完整编辑距离 DP——这不是对 kMaxAcceptableDistance
  // 语义的严格数学下界推导（存在插入/删除导致对齐错位的极端反例），而是一
  // 个在"近似键入纠错"场景下合理的性能/召回折衷（内部实现细节，宪章原则
  // IV），不改变 FR-003 对外承诺的延迟目标优先级。
  // 另设硬性 DP 调用次数上限（kMaxDpEvaluations）作为最后安全阀：无论长度分
  // 桶+首字符预筛后剩余候选规模如何，单次 buildLattice() 调用的最坏情况耗时
  // 均有确定上界，避免极端病态输入（如长串随机字母）阻塞 UI 线程。
  constexpr int kMaxDpEvaluations = 300;
  int dpEvaluations = 0;
  for (std::size_t len = lowLen; len <= highLen && dpEvaluations < kMaxDpEvaluations; ++len) {
    const auto it = m_entriesByPinyinLength.find(len);
    if (it == m_entriesByPinyinLength.end()) {
      continue;
    }
    for (const std::size_t idx : it->second) {
      if (dpEvaluations >= kMaxDpEvaluations) {
        break;
      }
      const auto &entry = m_entries[idx];
      if (entry.pinyinFull.empty()) {
        continue;
      }
      if (KeyboardProximityMap::proximityWeight(tokenFirstChar, entry.pinyinFull.front()) > 0.3) {
        continue;
      }
      ++dpEvaluations;
      const double distance = weightedEditDistance(token, entry.pinyinFull);
      if (distance > kMaxAcceptableDistance) {
        continue;
      }
      CandidateEntry c;
      c.surfaceForm = QString::fromStdString(entry.surfaceForm);
      // 距离越小、词频越高越可信；距离为 0 时等价于原始词频权重。
      c.rawWeight = entry.frequencyWeight / (1.0 + distance);
      c.source = CandidateEntry::Source::EditDistanceFallback;
      result.push_back(std::move(c));
    }
  }
  return result;
}

PinyinLatticeCell FastCorrector::buildLatticeCell(int tokenIndex, const std::string &token) const {
  PinyinLatticeCell cell;
  cell.tokenIndex = tokenIndex;
  cell.originalToken = QString::fromStdString(token);

  if (!looksLikeLatin(token)) {
    // 非拼音 token（已是中文或含标点）：透传格位，恒为单候选，权重 1.0
    // （FR-008 隐含预期：不对这类格位引入歧义）。
    CandidateEntry passthrough;
    passthrough.surfaceForm = QString::fromStdString(token);
    passthrough.rawWeight = 1.0;
    passthrough.normalizedWeight = 1.0;
    passthrough.source = CandidateEntry::Source::Passthrough;
    cell.candidates = {std::move(passthrough)};
    return cell;
  }

  std::vector<CandidateEntry> merged;
  std::unordered_set<std::string> seen;

  // 1) 全拼 Trie 命中
  for (const auto &[surfaceForm, weight] : m_trie.queryByFullPinyin(token, kTopKCandidates)) {
    if (!seen.insert(surfaceForm).second) {
      continue;
    }
    CandidateEntry c;
    c.surfaceForm = QString::fromStdString(surfaceForm);
    c.rawWeight = weight;
    c.source = CandidateEntry::Source::FullPinyinTrie;
    merged.push_back(std::move(c));
  }

  // 2) 简拼命中（token 较短，典型为首字母串，如 "zg" -> "这个"）
  if (token.size() <= 4) {
    for (const auto &[surfaceForm, weight] : m_trie.queryByAbbrev(token, kTopKCandidates)) {
      if (!seen.insert(surfaceForm).second) {
        continue;
      }
      CandidateEntry c;
      c.surfaceForm = QString::fromStdString(surfaceForm);
      c.rawWeight = weight;
      c.source = CandidateEntry::Source::AbbrevTrie;
      merged.push_back(std::move(c));
    }
  }

  // 3) 编辑距离近似匹配回退：仅当 Trie 全拼/简拼命中不足 Top-K 时才补充触发
  //    （合并而非仅取单一最优解，取代 001 correctToken() "任一来源命中即返回
  //    单值" 的提前截断逻辑）。真实词库规模下（research.md §1，数十万词条）
  //    若对每个 token 无条件触发编辑距离搜索：① 性能不可接受（即便已按长度
  //    分桶剪枝）；② 会把大量与原拼音实际无关、仅因候选词本身极高频而在
  //    "距离惩罚后权重"上意外反超真实 Trie 精确命中的噪声词注入候选集合，
  //    显著劣化 Lattice 候选质量（SC-001 语义正确率）。故严格延续 001
  //    "优先信任 Trie 精确/前缀命中，仅在其命中不足时才回退近似匹配"的既定
  //    优先级语义，仅将"回退单值"扩展为"回退补足多候选"。
  if (merged.size() < kTopKCandidates) {
    for (auto &c : editDistanceFallbackCandidates(token)) {
      const std::string key = c.surfaceForm.toStdString();
      if (!seen.insert(key).second) {
        continue;
      }
      merged.push_back(std::move(c));
    }
  }

  if (merged.empty()) {
    // 无任何命中：原样保留用户输入，避免丢失用户输入（延续 001 既有行为）。
    CandidateEntry c;
    c.surfaceForm = QString::fromStdString(token);
    c.rawWeight = 1.0;
    c.source = CandidateEntry::Source::EditDistanceFallback;
    merged.push_back(std::move(c));
  }

  // 按 rawWeight 降序排序后截断 Top-K（research.md §5.4），归一化基数为截断后
  // 的候选子集。
  std::sort(merged.begin(), merged.end(),
            [](const CandidateEntry &a, const CandidateEntry &b) { return a.rawWeight > b.rawWeight; });
  if (merged.size() > kTopKCandidates) {
    merged.resize(kTopKCandidates);
  }
  double sum = 0.0;
  for (const auto &c : merged) {
    sum += c.rawWeight;
  }
  if (sum <= 0.0) {
    sum = 1.0;
  }
  for (auto &c : merged) {
    c.normalizedWeight = c.rawWeight / sum;
  }

  cell.candidates = std::move(merged);
  return cell;
}

PinyinLattice FastCorrector::buildLattice(const QString &rawInput) const {
  PinyinLattice lattice;
  lattice.builtAt = std::chrono::system_clock::now();

  const std::string input = rawInput.toStdString();
  std::istringstream stream(input);
  std::string token;
  int tokenIndex = 0;
  while (stream >> token) {
    lattice.cells.push_back(buildLatticeCell(tokenIndex, token));
    ++tokenIndex;
  }
  return lattice;
}

QString FastCorrector::correct(const QString &rawInput) const { return buildLattice(rawInput).toDisplayText(); }

}  // namespace babel::fastpath
