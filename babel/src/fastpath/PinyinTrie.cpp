#include "babel/src/fastpath/PinyinTrie.h"

#include <algorithm>
#include <limits>

namespace babel::fastpath {

namespace {
/// bugfix: english-word-pinyin-misdetection 候选方向 2 —— 全拼前缀查询允许的最大
/// "溢出深度"（命中词条全拼长度 - 查询串长度）。assessment.md 确认 `want`
/// （4 字母）前缀命中 "万台"（全拼 "wantai"，6 字母，溢出 2）是报告现象的实际成
/// 因；设为 1 可过滤掉该类"查询串恰为更长词条前缀"的碰撞，同时仍保留"查询串
/// 与词条全拼仅差 1 个尾随字符"这一相对宽松场景的完成式建议（如用户习惯性少打
/// 最后一个音节字母的场景），具体数值已用 `FastCorrectorTest.cpp`/
/// `FastCorrectorRealDictionaryPerformanceTest.cpp` 正例回归验证不影响既有召回。
constexpr std::size_t kMaxFullPinyinPrefixOverreach = 1;
/// 简拼匹配设计上就是"短查询串对应长 key"的压缩匹配，不做溢出约束（保持既有行
/// 为），用一个足够大的哨兵值表示"实际不限制"。
constexpr std::size_t kUnlimitedOverreach = std::numeric_limits<std::size_t>::max();
}  // namespace

PinyinTrie::PinyinTrie() : m_fullRoot(std::make_unique<TrieNode>()), m_abbrevRoot(std::make_unique<TrieNode>()) {}

PinyinTrie::~PinyinTrie() = default;

void PinyinTrie::insert(TrieNode *root, const std::string &key, const std::string &surfaceForm, double weight) {
  if (key.empty()) {
    return;
  }
  TrieNode *node = root;
  for (const char ch : key) {
    auto &child = node->children[ch];
    if (!child) {
      child = std::make_unique<TrieNode>();
    }
    node = child.get();
  }
  node->candidates.emplace_back(surfaceForm, weight);
}

void PinyinTrie::build(const std::vector<DictionaryEntry> &entries) {
  m_fullRoot = std::make_unique<TrieNode>();
  m_abbrevRoot = std::make_unique<TrieNode>();
  for (const auto &entry : entries) {
    if (!entry.pinyinFull.empty()) {
      insert(m_fullRoot.get(), entry.pinyinFull, entry.surfaceForm, entry.frequencyWeight);
    }
    if (!entry.pinyinAbbrev.empty()) {
      insert(m_abbrevRoot.get(), entry.pinyinAbbrev, entry.surfaceForm, entry.frequencyWeight);
    }
  }
}

/// 深度优先收集某子树下的候选词，`depthBelowPrefix` 为当前节点相对"查询串匹配
/// 完成时所在节点"的下探深度（0 表示恰好在该节点上精确命中，即
/// `exactLengthMatch=true`）。`maxOverreach` 为允许的最大下探深度：超出后既不
/// 收录该节点候选，也不再继续下探子树（剪枝，避免真实词库规模下对深层无关子
/// 树做无意义遍历）。
void PinyinTrie::collectCandidates(const TrieNode *node, std::size_t depthBelowPrefix, std::size_t maxOverreach,
                                    std::vector<TrieMatch> *out) {
  if (depthBelowPrefix <= maxOverreach) {
    for (const auto &[surfaceForm, weight] : node->candidates) {
      out->push_back(TrieMatch{surfaceForm, weight, depthBelowPrefix == 0});
    }
  }
  if (depthBelowPrefix >= maxOverreach) {
    return;
  }
  for (const auto &[ch, child] : node->children) {
    collectCandidates(child.get(), depthBelowPrefix + 1, maxOverreach, out);
  }
}

std::vector<PinyinTrie::TrieMatch> PinyinTrie::queryPrefix(const TrieNode *root, const std::string &prefix,
                                                             std::size_t maxResults, std::size_t maxOverreach) {
  const TrieNode *node = root;
  for (const char ch : prefix) {
    const auto it = node->children.find(ch);
    if (it == node->children.end()) {
      return {};
    }
    node = it->second.get();
  }
  std::vector<TrieMatch> gathered;
  collectCandidates(node, 0, maxOverreach, &gathered);
  std::sort(gathered.begin(), gathered.end(),
            [](const TrieMatch &a, const TrieMatch &b) { return a.weight > b.weight; });
  if (gathered.size() > maxResults) {
    gathered.resize(maxResults);
  }
  return gathered;
}

std::vector<PinyinTrie::TrieMatch> PinyinTrie::queryByFullPinyin(const std::string &pinyin,
                                                                  std::size_t maxResults) const {
  return queryPrefix(m_fullRoot.get(), pinyin, maxResults, kMaxFullPinyinPrefixOverreach);
}

std::vector<PinyinTrie::TrieMatch> PinyinTrie::queryByAbbrev(const std::string &abbrev,
                                                              std::size_t maxResults) const {
  return queryPrefix(m_abbrevRoot.get(), abbrev, maxResults, kUnlimitedOverreach);
}

}  // namespace babel::fastpath
