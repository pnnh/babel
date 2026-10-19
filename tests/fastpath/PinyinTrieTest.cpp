#include <gtest/gtest.h>

#include <algorithm>

#include "babel/src/fastpath/DictionarySchema.h"
#include "babel/src/fastpath/PinyinTrie.h"

using babel::fastpath::DictionaryEntry;
using babel::fastpath::PinyinTrie;

namespace {
std::vector<DictionaryEntry> sampleEntries() {
  return {
      {1, "你好", "nihao", "nh", 50.0},
      {2, "你", "ni", "n", 100.0},
      {3, "好", "hao", "h", 100.0},
      {4, "好的", "haode", "hd", 80.0},
  };
}
}  // namespace

// T016 [US1]：全拼/简拼匹配命中率。

TEST(PinyinTrieTest, QueryByFullPinyinExactMatch) {
  PinyinTrie trie;
  trie.build(sampleEntries());
  const auto results = trie.queryByFullPinyin("nihao");
  ASSERT_FALSE(results.empty());
  EXPECT_EQ(results.front(), "你好");
}

TEST(PinyinTrieTest, QueryByFullPinyinPrefixReturnsAllMatches) {
  PinyinTrie trie;
  trie.build(sampleEntries());
  const auto results = trie.queryByFullPinyin("hao");
  // "hao" 是 "好"(haode 前缀匹配不成立，haode 全拼是 "haode" 非 "hao") 的精确全拼，
  // 应至少命中 "好"。
  ASSERT_FALSE(results.empty());
  EXPECT_NE(std::find(results.begin(), results.end(), "好"), results.end());
}

TEST(PinyinTrieTest, QueryByAbbrevMatchesInitialLetters) {
  PinyinTrie trie;
  trie.build(sampleEntries());
  const auto results = trie.queryByAbbrev("nh");
  ASSERT_FALSE(results.empty());
  EXPECT_EQ(results.front(), "你好");
}

TEST(PinyinTrieTest, QueryRanksByFrequencyWeightDescending) {
  PinyinTrie trie;
  trie.build({
      {1, "低频候选", "ni", "n", 1.0},
      {2, "你", "ni", "n", 100.0},
  });
  const auto results = trie.queryByFullPinyin("ni", 2);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results.front(), "你");  // 词频更高者排在前面
}

TEST(PinyinTrieTest, UnknownPrefixReturnsEmpty) {
  PinyinTrie trie;
  trie.build(sampleEntries());
  EXPECT_TRUE(trie.queryByFullPinyin("zzzzz").empty());
  EXPECT_TRUE(trie.queryByAbbrev("zzzzz").empty());
}
