#include "babel/src/fastpath/KeyboardProximityMap.h"

namespace babel::fastpath {

const std::array<KeyboardProximityMap::NeighborEntry, 26> &KeyboardProximityMap::qwertyNeighbors() {
  // QWERTY 物理键位邻近表（仅覆盖字母键，数字/符号键不参与中英拼音混排纠错）。
  static const std::array<NeighborEntry, 26> kTable = {{
      {'q', "wa"},       {'w', "qeas"},     {'e', "wrsd"},     {'r', "edft"},
      {'t', "rfgy"},     {'y', "tghu"},     {'u', "yhji"},     {'i', "ujko"},
      {'o', "iklp"},     {'p', "ol"},       {'a', "qwsz"},     {'s', "qweadzx"},
      {'d', "wersfcx"},  {'f', "ertdgvc"},  {'g', "rtfhbv"},   {'h', "tygjnb"},
      {'j', "yuhkmn"},   {'k', "uiojlm"},   {'l', "iokp"},     {'z', "asx"},
      {'x', "zsdc"},     {'c', "xdfv"},     {'v', "cfgb"},     {'b', "vghn"},
      {'n', "bhjm"},     {'m', "njk"},
  }};
  return kTable;
}

double KeyboardProximityMap::proximityWeight(char a, char b) {
  const char la = static_cast<char>(std::tolower(static_cast<unsigned char>(a)));
  const char lb = static_cast<char>(std::tolower(static_cast<unsigned char>(b)));
  if (la == lb) {
    return 0.0;
  }
  for (const auto &entry : qwertyNeighbors()) {
    if (entry.key == la) {
      if (entry.neighbors.find(lb) != std::string_view::npos) {
        return 0.3;
      }
      break;
    }
  }
  return 1.0;
}

}  // namespace babel::fastpath
