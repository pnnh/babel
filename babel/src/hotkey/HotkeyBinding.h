#pragma once

#include <vector>

namespace babel::hotkey {

/// 修饰键（data-model.md §9）。MVP 不提供自定义热键配置入口，
/// 默认组合键为固定值，不在此新增"自定义绑定"能力。
enum class Modifier {
  Control,
  Alt,    ///< Windows/Linux 术语 Alt；macOS 对应 Option，见 HotkeyBinding::defaultBinding()
  Option,  ///< macOS 专用别名，与 Alt 为同一物理键位的不同平台命名
};

enum class Key {
  Space,
};

/// 全局热键绑定配置（contracts/global-hotkey-contract.md）。
struct HotkeyBinding {
  std::vector<Modifier> modifiers;
  Key key = Key::Space;

  /// 返回当前平台的默认组合键：
  ///   Windows / Linux: Ctrl+Alt+Space
  ///   macOS: Control+Option+Space
  static HotkeyBinding defaultBinding() {
#if defined(__APPLE__)
    return HotkeyBinding{{Modifier::Control, Modifier::Option}, Key::Space};
#else
    return HotkeyBinding{{Modifier::Control, Modifier::Alt}, Key::Space};
#endif
  }
};

}  // namespace babel::hotkey
