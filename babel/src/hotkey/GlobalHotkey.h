#pragma once

#include <functional>
#include <memory>

#include "babel/src/hotkey/HotkeyBinding.h"

namespace babel::hotkey {

/// 全局热键跨平台抽象接口（contracts/global-hotkey-contract.md）。
/// 分平台自研薄封装实现（research.md §3）：Windows `RegisterHotKey` /
/// macOS Carbon `RegisterEventHotKey` / Linux `libX11` 的 `XGrabKey`。
class GlobalHotkey {
 public:
  using ToggleCallback = std::function<void()>;

  virtual ~GlobalHotkey() = default;

  /// 注册全局热键；成功后每次用户按下该组合键均调用 onToggle（`ToggleFloatingWindow`
  /// 语义：由调用方——SessionController——根据当前会话状态决定唤起或取消）。
  virtual bool registerHotkey(const HotkeyBinding &binding, ToggleCallback onToggle) = 0;

  /// 注销全局热键（应用退出或重新绑定前调用）。
  virtual void unregisterHotkey() = 0;
};

/// 创建当前平台对应的 GlobalHotkey 实现（Windows/_WIN32、macOS/__APPLE__、
/// Linux X11 条件编译分支，见 GlobalHotkey_{win,mac,x11}.*）。
std::unique_ptr<GlobalHotkey> createPlatformGlobalHotkey();

}  // namespace babel::hotkey
