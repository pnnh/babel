#include "babel/src/hotkey/GlobalHotkey.h"

#include <Carbon/Carbon.h>

namespace babel::hotkey {
namespace {

/// macOS 全局热键实现（research.md §3：分平台自研薄封装，Carbon `RegisterEventHotKey`）。
/// Carbon HIToolbox 的热键注册 API 在现代 macOS（含 Apple Silicon）上仍受支持，
/// 是 Cocoa 应用注册系统级全局热键的标准做法，不依赖已弃用的 Carbon 应用框架本身。
class GlobalHotkeyMac : public GlobalHotkey {
 public:
  ~GlobalHotkeyMac() override { unregisterHotkey(); }

  bool registerHotkey(const HotkeyBinding &binding, ToggleCallback onToggle) override {
    unregisterHotkey();
    m_callback = std::move(onToggle);

    UInt32 carbonModifiers = 0;
    for (const auto modifier : binding.modifiers) {
      switch (modifier) {
        case Modifier::Control:
          carbonModifiers |= controlKey;
          break;
        case Modifier::Alt:
        case Modifier::Option:
          carbonModifiers |= optionKey;
          break;
      }
    }
    const UInt32 virtualKeyCode = keyToVirtualKeyCode(binding.key);

    const EventTypeSpec eventType{kEventClassKeyboard, kEventHotKeyPressed};
    InstallApplicationEventHandler(&GlobalHotkeyMac::handleHotkeyEvent, 1, &eventType, this, &m_handlerRef);

    const EventHotKeyID hotKeyId{'bbhk', 1};
    const OSStatus status =
        RegisterEventHotKey(virtualKeyCode, carbonModifiers, hotKeyId, GetApplicationEventTarget(), 0, &m_hotKeyRef);
    return status == noErr;
  }

  void unregisterHotkey() override {
    if (m_hotKeyRef) {
      UnregisterEventHotKey(m_hotKeyRef);
      m_hotKeyRef = nullptr;
    }
    if (m_handlerRef) {
      RemoveEventHandler(m_handlerRef);
      m_handlerRef = nullptr;
    }
    m_callback = nullptr;
  }

 private:
  static OSStatus handleHotkeyEvent(EventHandlerCallRef /*nextHandler*/, EventRef /*event*/, void *userData) {
    auto *self = static_cast<GlobalHotkeyMac *>(userData);
    if (self && self->m_callback) {
      self->m_callback();
    }
    return noErr;
  }

  static UInt32 keyToVirtualKeyCode(Key key) {
    switch (key) {
      case Key::Space:
      default:
        return kVK_Space;
    }
  }

  ToggleCallback m_callback;
  EventHotKeyRef m_hotKeyRef = nullptr;
  EventHandlerRef m_handlerRef = nullptr;
};

}  // namespace

std::unique_ptr<GlobalHotkey> createPlatformGlobalHotkey() { return std::make_unique<GlobalHotkeyMac>(); }

}  // namespace babel::hotkey
