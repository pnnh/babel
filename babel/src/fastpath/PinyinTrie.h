#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "babel/src/fastpath/DictionarySchema.h"

namespace babel::fastpath {

/// 拼音/简拼 Trie（data-model.md §7，research.md §2 自研选型）。
/// 支持全拼与简拼前缀查询，候选按词频权重降序返回。
class PinyinTrie {
 public:
  PinyinTrie();
  ~PinyinTrie();

  /// 由词典条目构建索引（覆盖式重建）。
  void build(const std::vector<DictionaryEntry> &entries);

  /// 全拼前缀匹配，返回 (候选词形, frequencyWeight) 按权重降序排列。
  std::vector<std::pair<std::string, double>> queryByFullPinyin(const std::string &pinyin,
                                                                  std::size_t maxResults = 5) const;

  /// 简拼前缀匹配，返回 (候选词形, frequencyWeight) 按权重降序排列。
  std::vector<std::pair<std::string, double>> queryByAbbrev(const std::string &abbrev,
                                                              std::size_t maxResults = 5) const;

 private:
  struct TrieNode {
    std::unordered_map<char, std::unique_ptr<TrieNode>> children;
    std::vector<std::pair<std::string, double>> candidates;  ///< (surfaceForm, frequencyWeight)
  };

  static void insert(TrieNode *root, const std::string &key, const std::string &surfaceForm, double weight);
  static std::vector<std::pair<std::string, double>> queryPrefix(const TrieNode *root, const std::string &prefix,
                                                                   std::size_t maxResults);
  static void collectCandidates(const TrieNode *node, std::vector<std::pair<std::string, double>> *out);

  std::unique_ptr<TrieNode> m_fullRoot;
  std::unique_ptr<TrieNode> m_abbrevRoot;
};

}  // namespace babel::fastpath
