#pragma once

#include <QString>
#include <string>
#include <unordered_map>
#include <vector>

#include "babel/src/fastpath/DictionarySchema.h"
#include "babel/src/fastpath/EnglishWordList.h"
#include "babel/src/fastpath/PinyinLattice.h"
#include "babel/src/fastpath/PinyinTrie.h"

namespace babel::fastpath {

/// 快流统一入口（research.md §2 自研选型）：击键 → 规整化候选文本 / 多候选格
/// （002-lattice-dict-model-switch 新增 `buildLattice()`）。
/// 对外行为：编辑距离前缀索引思路 + 拼音/简拼 Trie 查询 + QWERTY 键位邻近加权，
/// 目标端到端延迟 <15ms（FR-003/SC-002）。全程纯内存计算，不依赖网络。
///
/// 实现说明（内部细节，供维护参考）：近似匹配回退按 `pinyinFull` 长度分桶
/// （`m_entriesByPinyinLength`），结合"长度差超过可接受阈值时编辑距离下界必然
/// 超限"的数学性质做无损剪枝，避免真实词库规模（数十万条）下对全量词条逐一计
/// 算编辑距离动态规划（research.md §1 已核实的体量）。
class FastCorrector {
 public:
  /// `commonEnglishWords` 为 bugfix: english-word-pinyin-misdetection 新增的高
  /// 频英文常用词表（候选方向 1），默认空（不拦截任何词，保持既有行为/既有测试
  /// 不受影响）；生产环境由 `SessionController` 加载随包分发的词表后传入（见
  /// `babel/src/fastpath/EnglishWordList.h`）。
  explicit FastCorrector(std::vector<DictionaryEntry> entries = builtinSeedDictionary(),
                         EnglishWordList commonEnglishWords = EnglishWordList());

  /// 将原始击键文本（中英拼音混排）规整化为候选展示文本。
  /// 002-lattice-dict-model-switch：实现改为 `buildLattice(rawInput).toDisplayText()`
  /// 的薄封装，对外签名/行为保持向后兼容（data-model.md §1）。
  QString correct(const QString &rawInput) const;

  /// 按空白分词粒度逐 token 构建拼音候选格序列（data-model.md §1，FR-006/FR-007）：
  /// 每个拼音 token 合并 Trie 全拼命中 ∪ 简拼命中 ∪ 编辑距离回退候选，按
  /// `rawWeight` 降序排序后截断 Top-K=8（research.md §5.4）再计算 `normalizedWeight`；
  /// 非拼音 token 保留为透传格位（恒为单候选，`source = Passthrough`）。
  PinyinLattice buildLattice(const QString &rawInput) const;

 private:
  PinyinLatticeCell buildLatticeCell(int tokenIndex, const std::string &token) const;
  std::vector<CandidateEntry> editDistanceFallbackCandidates(const std::string &token) const;
  double weightedEditDistance(const std::string &a, const std::string &b) const;

  PinyinTrie m_trie;
  std::vector<DictionaryEntry> m_entries;
  /// `m_entries` 按 `pinyinFull.size()` 分桶的索引（值为 `m_entries` 下标），供编辑
  /// 距离回退时按长度差做无损剪枝（仅性能优化，不改变候选匹配结果）。
  std::unordered_map<std::size_t, std::vector<std::size_t>> m_entriesByPinyinLength;
  /// bugfix: english-word-pinyin-misdetection 候选方向 1 —— 高频英文常用词表，
  /// `looksLikeLatin()` 判定为真之后、进入拼音匹配管线之前优先查询。
  EnglishWordList m_commonEnglishWords;
};

}  // namespace babel::fastpath
