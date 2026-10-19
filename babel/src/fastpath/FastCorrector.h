#pragma once

#include <QString>
#include <string>
#include <vector>

#include "babel/src/fastpath/DictionarySchema.h"
#include "babel/src/fastpath/PinyinTrie.h"

namespace babel::fastpath {

/// 快流统一入口（research.md §2 自研选型）：击键 → 规整化候选文本。
/// 对外行为：编辑距离前缀索引思路 + 拼音/简拼 Trie 查询 + QWERTY 键位邻近加权，
/// 目标端到端延迟 <15ms（FR-003/SC-002）。全程纯内存计算，不依赖网络。
///
/// 实现说明（内部细节，供维护参考）：MVP 阶段词典规模较小，近似匹配回退采用
/// 直接对词典全量计算键位邻近加权编辑距离（而非预构建 SymSpell 删除前缀索引），
/// 在该规模下仍可满足 <15ms 目标；若未来词典规模显著增长，可在不改变本类对外
/// 接口的前提下替换为真正的 SymSpell 删除前缀索引以降低时间复杂度。
class FastCorrector {
 public:
  explicit FastCorrector(std::vector<DictionaryEntry> entries = builtinSeedDictionary());

  /// 将原始击键文本（中英拼音混排）规整化为候选展示文本。
  QString correct(const QString &rawInput) const;

 private:
  std::string correctToken(const std::string &token) const;
  double weightedEditDistance(const std::string &a, const std::string &b) const;

  PinyinTrie m_trie;
  std::vector<DictionaryEntry> m_entries;
};

}  // namespace babel::fastpath
