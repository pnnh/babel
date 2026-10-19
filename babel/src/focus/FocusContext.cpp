#include "babel/src/focus/FocusContext.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#else
#include <X11/Xlib.h>
#endif

namespace babel::focus {

TargetFocusContext captureCurrentFocus() {
  TargetFocusContext ctx;
  ctx.capturedAt = std::chrono::system_clock::now();

#if defined(_WIN32)
  if (const HWND fg = GetForegroundWindow()) {
    ctx.platformWindowRef = static_cast<BabelPlatformWindowRef>(fg);
    ctx.valid = true;
  }

#elif defined(__APPLE__)
  // 纯 C CoreGraphics API（无需 Objective-C 运行时，故可保留 .cpp 后缀，
  // 不同于 GlobalHotkey_mac.mm/PasteInjector_mac.mm 需要的 Carbon/CGEventPost）：
  // 枚举屏幕前到后排序的窗口列表，取第一个"正常层级"（layer == 0）窗口的所属
  // 进程 pid 作为捕获时刻的诊断标识（CGEventPost 实际投递到系统当前焦点，
  // 该引用在目标窗口切换/关闭后如何处理属刻意留白，见 data-model.md §4）。
  CFArrayRef windowList = CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
  if (windowList) {
    const CFIndex count = CFArrayGetCount(windowList);
    for (CFIndex i = 0; i < count; ++i) {
      const CFDictionaryRef info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowList, i));
      const CFNumberRef layerRef = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowLayer));
      int layer = -1;
      if (layerRef) {
        CFNumberGetValue(layerRef, kCFNumberIntType, &layer);
      }
      if (layer != 0) {
        continue;
      }
      const CFNumberRef pidRef = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
      if (pidRef) {
        int pid = 0;
        CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
        ctx.platformWindowRef = reinterpret_cast<void *>(static_cast<intptr_t>(pid));
        ctx.valid = true;
      }
      break;
    }
    CFRelease(windowList);
  }

#else
  if (Display *display = XOpenDisplay(nullptr)) {
    Window focused = 0;
    int revertTo = 0;
    XGetInputFocus(display, &focused, &revertTo);
    ctx.platformWindowRef = static_cast<unsigned long>(focused);
    ctx.valid = (focused != None);
    XCloseDisplay(display);
  }
#endif

  return ctx;
}

}  // namespace babel::focus
