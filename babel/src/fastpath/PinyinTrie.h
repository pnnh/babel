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
  /// bugfix: english-word-pinyin-misdetection 候选方向 2 —— 查询结果新增
  /// `exactLengthMatch` 字段：`true` 表示命中词条的完整 key（`pinyinFull`/
  /// `pinyinAbbrev`）长度恰好等于查询串长度（精确匹配）；`false` 表示该候选是
  /// 通过"前缀溢出"（查询串仅是某个更长词条 key 的前缀）命中的——这正是
  /// `want`→"万台"（"wantai" 的前缀 "want"）一类合法英文单词被误判为拼音前缀
  /// 的根因（assessment.md "关键修正"）。调用方（`FastCorrector`）据此标记
  /// `CandidateEntry::latinOriginLengthMismatch`，供 `PinyinLattice` 纵深防御使
  /// 用，不改变候选本身是否被返回（返回与否由 `queryPrefix()` 内部的溢出深度上
  /// 限决定，见 `.cpp`）。
  struct TrieMatch {
    std::string surfaceForm;
    double weight = 0.0;
    bool exactLengthMatch = true;
  };

  PinyinTrie();
  ~PinyinTrie();

  /// 由词典条目构建索引（覆盖式重建）。
  void build(const std::vector<DictionaryEntry> &entries);

  /// 全拼前缀匹配，按权重降序返回。为遏制"查询串恰为某个更长词条全拼前缀"的无
  /// 约束前缀溢出匹配（assessment.md 确认的 `want`→万台 根因），内部对匹配到的
  /// 词条全拼长度相对查询串的溢出量做上限约束（见 .cpp `kMaxFullPinyinPrefixOverreach`）。
  std::vector<TrieMatch> queryByFullPinyin(const std::string &pinyin, std::size_t maxResults = 5) const;

  /// 简拼前缀匹配，按权重降序返回。简拼本身就是"多字符 key 对应短查询串"的压缩
  /// 匹配场景（设计如此，非缺陷），不对前缀溢出量做约束，保持既有行为不变；
  /// `exactLengthMatch` 字段仍如实反映是否精确匹配，供调用方按需使用。
  std::vector<TrieMatch> queryByAbbrev(const std::string &abbrev, std::size_t maxResults = 5) const;

 private:
  struct TrieNode {
    std::unordered_map<char, std::unique_ptr<TrieNode>> children;
    std::vector<std::pair<std::string, double>> candidates;  ///< (surfaceForm, frequencyWeight)
  };

  static void insert(TrieNode *root, const std::string &key, const std::string &surfaceForm, double weight);
  static std::vector<TrieMatch> queryPrefix(const TrieNode *root, const std::string &prefix, std::size_t maxResults,
                                             std::size_t maxOverreach);
  static void collectCandidates(const TrieNode *node, std::size_t depthBelowPrefix, std::size_t maxOverreach,
                                 std::vector<TrieMatch> *out);

  std::unique_ptr<TrieNode> m_fullRoot;
  std::unique_ptr<TrieNode> m_abbrevRoot;
};

}  // namespace babel::fastpath
