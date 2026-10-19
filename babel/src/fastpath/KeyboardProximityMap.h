#pragma once

#include <array>
#include <cctype>
#include <string>

namespace babel::fastpath {

/// QWERTY 物理键位邻近编辑距离权重表（data-model.md §8）。
/// 编译期常量，非用户数据，不持久化到 sqlite3。
///
/// `proximityWeight(a, b)` 返回替换 a→b 的编辑距离权重：
///   - 相同字符：0.0（无代价）
///   - 物理相邻键位：0.3（低代价，优先于普通替换）
///   - 其他：1.0（标准编辑距离代价）
class KeyboardProximityMap {
 public:
  static double proximityWeight(char a, char b);

 private:
  struct NeighborEntry {
    char key;
    std::string_view neighbors;
  };

  static const std::array<NeighborEntry, 26> &qwertyNeighbors();
};

}  // namespace babel::fastpath
