#include "babel/src/fastpath/FastCorrector.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <vector>

#include "babel/src/fastpath/KeyboardProximityMap.h"

namespace babel::fastpath {

namespace {
bool looksLikeLatin(const std::string &token) {
  return std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isalpha(ch) != 0; });
}
}  // namespace

FastCorrector::FastCorrector(std::vector<DictionaryEntry> entries) : m_entries(std::move(entries)) {
  m_trie.build(m_entries);
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

std::string FastCorrector::correctToken(const std::string &token) const {
  if (token.empty()) {
    return token;
  }
  // 非纯字母 token（已是中文或含标点）原样保留，不参与拼音纠错。
  if (!looksLikeLatin(token)) {
    return token;
  }
  // 1) 全拼精确/前缀 Trie 命中
  if (auto hits = m_trie.queryByFullPinyin(token, 1); !hits.empty()) {
    return hits.front();
  }
  // 2) 简拼命中（token 较短，典型为首字母串，如 "zg" -> "这个"）
  if (token.size() <= 4) {
    if (auto hits = m_trie.queryByAbbrev(token, 1); !hits.empty()) {
      return hits.front();
    }
  }
  // 3) 近似匹配回退：键位邻近加权编辑距离，阈值内取最小距离、同距离取词频更高者
  constexpr double kMaxAcceptableDistance = 1.5;
  double bestDistance = std::numeric_limits<double>::max();
  double bestWeight = -1.0;
  std::string best = token;  // 无命中时原样保留，避免丢失用户输入
  for (const auto &entry : m_entries) {
    if (entry.pinyinFull.empty()) {
      continue;
    }
    const double distance = weightedEditDistance(token, entry.pinyinFull);
    if (distance > kMaxAcceptableDistance) {
      continue;
    }
    if (distance < bestDistance || (distance == bestDistance && entry.frequencyWeight > bestWeight)) {
      bestDistance = distance;
      bestWeight = entry.frequencyWeight;
      best = entry.surfaceForm;
    }
  }
  return best;
}

QString FastCorrector::correct(const QString &rawInput) const {
  const std::string input = rawInput.toStdString();
  std::istringstream stream(input);
  std::string token;
  std::string result;
  bool prevOutputWasLatin = false;
  bool firstToken = true;
  while (stream >> token) {
    const std::string corrected = correctToken(token);
    const bool correctedIsLatin = looksLikeLatin(corrected);
    // 仅当"上一个输出 token"与"当前输出 token"均为英文（词典未命中/本身是英文单词）
    // 时才保留一个分隔空格，贴合中英混排书写习惯；中文纠正结果之间不插入空格。
    if (!firstToken && prevOutputWasLatin && correctedIsLatin) {
      result += ' ';
    }
    result += corrected;
    prevOutputWasLatin = correctedIsLatin;
    firstToken = false;
  }
  return QString::fromStdString(result);
}

}  // namespace babel::fastpath
