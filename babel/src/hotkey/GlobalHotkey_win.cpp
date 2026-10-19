#include "babel/src/hotkey/GlobalHotkey.h"

#include <windows.h>

#include <atomic>
#include <thread>

namespace babel::hotkey {
namespace {

constexpr int kHotkeyId = 1;

UINT modifiersToWinFlags(const HotkeyBinding &binding) {
  UINT flags = 0;
  for (const auto modifier : binding.modifiers) {
    switch (modifier) {
      case Modifier::Control:
        flags |= MOD_CONTROL;
        break;
      case Modifier::Alt:
      case Modifier::Option:
        flags |= MOD_ALT;
        break;
    }
  }
  return flags | MOD_NOREPEAT;
}

UINT keyToVk(Key key) {
  switch (key) {
    case Key::Space:
    default:
      return VK_SPACE;
  }
}

/// Windows 全局热键实现（research.md §3：分平台自研薄封装，`RegisterHotKey`）。
/// 在独立线程上创建消息专用窗口（HWND_MESSAGE）接收 WM_HOTKEY 消息，避免占用/
/// 依赖 Qt 主事件循环的窗口句柄。
class GlobalHotkeyWin : public GlobalHotkey {
 public:
  ~GlobalHotkeyWin() override { unregisterHotkey(); }

  bool registerHotkey(const HotkeyBinding &binding, ToggleCallback onToggle) override {
    unregisterHotkey();
    m_callback = std::move(onToggle);
    m_running = true;
    m_thread = std::thread([this, binding]() { messageLoop(binding); });
    return true;
  }

  void unregisterHotkey() override {
    if (!m_running) {
      return;
    }
    m_running = false;
    if (m_threadId != 0) {
      PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
    }
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

 private:
  void messageLoop(const HotkeyBinding &binding) {
    m_threadId = GetCurrentThreadId();
    const HWND hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    RegisterHotKey(hwnd, kHotkeyId, modifiersToWinFlags(binding), keyToVk(binding.key));

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
      if (msg.message == WM_HOTKEY && msg.wParam == kHotkeyId && m_callback) {
        m_callback();
      }
    }
    UnregisterHotKey(hwnd, kHotkeyId);
    if (hwnd) {
      DestroyWindow(hwnd);
    }
  }

  ToggleCallback m_callback;
  std::thread m_thread;
  std::atomic<bool> m_running{false};
  DWORD m_threadId = 0;
};

}  // namespace

std::unique_ptr<GlobalHotkey> createPlatformGlobalHotkey() { return std::make_unique<GlobalHotkeyWin>(); }

}  // namespace babel::hotkey
