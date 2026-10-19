#include "babel/src/inject/PasteInjector.h"

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <cstdlib>
#include <cstring>

namespace babel::inject {
namespace {

bool isLikelyWaylandSession() {
  const char *sessionType = std::getenv("XDG_SESSION_TYPE");
  if (sessionType && std::strcmp(sessionType, "wayland") == 0) {
    return true;
  }
  return std::getenv("WAYLAND_DISPLAY") != nullptr;
}

/// Linux X11 模拟粘贴注入（research.md §4.2：分平台自研薄封装，`XTest` 扩展）。
/// 成功路径对应 FR-008/SC-004；Wayland/合成器不支持注入时返回
/// UnsupportedEnvironment（FR-009，T044）；平台本应支持但本次执行失败时返回
/// OtherFailure（User Story 3 场景 3，T055）。
class PasteInjectorX11 : public PasteInjector {
 public:
  InjectionAttemptResult attemptPaste(const babel::focus::TargetFocusContext & /*targetFocus*/) override {
    InjectionAttemptResult result;
    result.attemptedAt = std::chrono::system_clock::now();

    if (isLikelyWaylandSession()) {
      result.outcome = InjectionOutcome::UnsupportedEnvironment;
      return result;
    }

    Display *display = XOpenDisplay(nullptr);
    if (!display) {
      // 无 X11 显示连接（典型纯 Wayland、无 XWayland 回退）：不支持注入。
      result.outcome = InjectionOutcome::UnsupportedEnvironment;
      return result;
    }

    int eventBase = 0, errorBase = 0, majorVersion = 0, minorVersion = 0;
    if (!XTestQueryExtension(display, &eventBase, &errorBase, &majorVersion, &minorVersion)) {
      // X11 可用但合成器/服务器未提供 XTest 扩展：仍判定为不支持注入（FR-009）。
      XCloseDisplay(display);
      result.outcome = InjectionOutcome::UnsupportedEnvironment;
      return result;
    }

    const KeyCode ctrlKeyCode = XKeysymToKeycode(display, XK_Control_L);
    const KeyCode vKeyCode = XKeysymToKeycode(display, XK_v);
    if (ctrlKeyCode == 0 || vKeyCode == 0) {
      // 平台理论上支持注入，但本次无法解析键位（如键盘布局异常）：本应支持但本次执行失败。
      XCloseDisplay(display);
      result.outcome = InjectionOutcome::OtherFailure;
      return result;
    }

    XTestFakeKeyEvent(display, ctrlKeyCode, True, 0);
    XTestFakeKeyEvent(display, vKeyCode, True, 0);
    XTestFakeKeyEvent(display, vKeyCode, False, 0);
    XTestFakeKeyEvent(display, ctrlKeyCode, False, 0);
    XSync(display, False);
    XCloseDisplay(display);

    result.outcome = InjectionOutcome::Succeeded;
    return result;
  }
};

}  // namespace

std::unique_ptr<PasteInjector> createPlatformPasteInjector() { return std::make_unique<PasteInjectorX11>(); }

}  // namespace babel::inject
