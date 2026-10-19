#include "babel/src/fastpath/PinyinTrie.h"

#include <algorithm>

namespace babel::fastpath {

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

/// 深度优先收集某子树下全部候选词，不限制数量（由调用方截断/排序）。
void PinyinTrie::collectCandidates(const TrieNode *node, std::vector<std::pair<std::string, double>> *out) {
  for (const auto &candidate : node->candidates) {
    out->push_back(candidate);
  }
  for (const auto &[ch, child] : node->children) {
    collectCandidates(child.get(), out);
  }
}

std::vector<std::pair<std::string, double>> PinyinTrie::queryPrefix(const TrieNode *root, const std::string &prefix,
                                                                      std::size_t maxResults) {
  const TrieNode *node = root;
  for (const char ch : prefix) {
    const auto it = node->children.find(ch);
    if (it == node->children.end()) {
      return {};
    }
    node = it->second.get();
  }
  std::vector<std::pair<std::string, double>> gathered;
  collectCandidates(node, &gathered);
  std::sort(gathered.begin(), gathered.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });
  if (gathered.size() > maxResults) {
    gathered.resize(maxResults);
  }
  return gathered;
}

std::vector<std::pair<std::string, double>> PinyinTrie::queryByFullPinyin(const std::string &pinyin,
                                                                           std::size_t maxResults) const {
  return queryPrefix(m_fullRoot.get(), pinyin, maxResults);
}

std::vector<std::pair<std::string, double>> PinyinTrie::queryByAbbrev(const std::string &abbrev,
                                                                       std::size_t maxResults) const {
  return queryPrefix(m_abbrevRoot.get(), abbrev, maxResults);
}

}  // namespace babel::fastpath
