#include "babel/src/hotkey/GlobalHotkey.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <atomic>
#include <thread>

namespace babel::hotkey {
namespace {

unsigned int modifiersToX11Mask(const HotkeyBinding &binding) {
  unsigned int mask = 0;
  for (const auto modifier : binding.modifiers) {
    switch (modifier) {
      case Modifier::Control:
        mask |= ControlMask;
        break;
      case Modifier::Alt:
      case Modifier::Option:
        mask |= Mod1Mask;  // X11 下 Alt 通常映射为 Mod1
        break;
    }
  }
  return mask;
}

KeySym keyToKeySym(Key key) {
  switch (key) {
    case Key::Space:
    default:
      return XK_space;
  }
}

/// Linux X11 全局热键实现（research.md §3：分平台自研薄封装，`libX11` 的
/// `XGrabKey`）。Wayland 会话下 XGrabKey 预期不可用/无效果，对应 FR-009 的
/// 统一降级提示路径由 PasteInjector/SessionController 侧处理，本模块不单独
/// 探测合成器类型。
class GlobalHotkeyX11 : public GlobalHotkey {
 public:
  ~GlobalHotkeyX11() override { unregisterHotkey(); }

  bool registerHotkey(const HotkeyBinding &binding, ToggleCallback onToggle) override {
    unregisterHotkey();
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
      return false;  // 典型 Wayland-only 会话下无 X11 显示连接
    }
    m_callback = std::move(onToggle);
    const KeySym keysym = keyToKeySym(binding.key);
    m_keycode = XKeysymToKeycode(m_display, keysym);
    m_modifiers = modifiersToX11Mask(binding);
    m_root = DefaultRootWindow(m_display);

    // 同时忽略 NumLock/CapsLock 状态位，确保这些锁定键开启时热键仍可触发。
    const unsigned int ignoredCombos[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    for (const unsigned int ignored : ignoredCombos) {
      XGrabKey(m_display, m_keycode, m_modifiers | ignored, m_root, True, GrabModeAsync, GrabModeAsync);
    }
    XSelectInput(m_display, m_root, KeyPressMask);

    m_running = true;
    m_thread = std::thread([this]() { eventLoop(); });
    return true;
  }

  void unregisterHotkey() override {
    if (!m_running) {
      return;
    }
    m_running = false;
    if (m_display) {
      // 发送一个无害事件以唤醒阻塞在 XNextEvent 的事件循环线程。
      XClientMessageEvent dummy{};
      dummy.type = ClientMessage;
      dummy.window = m_root;
      dummy.format = 32;
      XSendEvent(m_display, m_root, False, NoEventMask, reinterpret_cast<XEvent *>(&dummy));
      XFlush(m_display);
    }
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_display) {
      XUngrabKey(m_display, m_keycode, m_modifiers, m_root);
      XCloseDisplay(m_display);
      m_display = nullptr;
    }
    m_callback = nullptr;
  }

 private:
  void eventLoop() {
    XEvent event;
    while (m_running) {
      XNextEvent(m_display, &event);
      if (!m_running) {
        break;
      }
      if (event.type == KeyPress && event.xkey.keycode == m_keycode && m_callback) {
        m_callback();
      }
    }
  }

  ToggleCallback m_callback;
  Display *m_display = nullptr;
  Window m_root = 0;
  unsigned int m_keycode = 0;
  unsigned int m_modifiers = 0;
  std::thread m_thread;
  std::atomic<bool> m_running{false};
};

}  // namespace

std::unique_ptr<GlobalHotkey> createPlatformGlobalHotkey() { return std::make_unique<GlobalHotkeyX11>(); }

}  // namespace babel::hotkey
