#include "babel/src/inject/PasteInjector.h"

#include <ApplicationServices/ApplicationServices.h>

#include <cstdint>

namespace babel::inject {
namespace {

/// macOS 模拟粘贴注入（research.md §4.2：分平台自研薄封装，`CGEventPost`）。
/// 成功路径对应 FR-008/SC-004；Accessibility 权限未授予时返回 PermissionDenied
/// （FR-012、User Story 3 验收场景 3，由 T054 在本文件补充）。
class PasteInjectorMac : public PasteInjector {
 public:
  InjectionAttemptResult attemptPaste(const babel::focus::TargetFocusContext & /*targetFocus*/) override {
    InjectionAttemptResult result;
    result.attemptedAt = std::chrono::system_clock::now();

    // Accessibility 权限检测：未授权时模拟按键事件不会被目标应用正确接收，
    // 统一判定为 PermissionDenied（对外 UI 仍展示统一"请手动粘贴"文案，见
    // contracts/injection-outcome-contract.md 强约束，不在此暴露原因细节）。
    if (!AXIsProcessTrusted()) {
      result.outcome = InjectionOutcome::PermissionDenied;
      return result;
    }

    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!source) {
      result.outcome = InjectionOutcome::OtherFailure;
      return result;
    }

    // 模拟标准粘贴快捷键 Cmd+V。
    constexpr CGKeyCode kVirtualKeyV = 0x09;  // US ANSI 键盘布局下 'v' 的虚拟键码
    CGEventRef keyVDown = CGEventCreateKeyboardEvent(source, kVirtualKeyV, true);
    CGEventRef keyVUp = CGEventCreateKeyboardEvent(source, kVirtualKeyV, false);
    if (!keyVDown || !keyVUp) {
      if (keyVDown) CFRelease(keyVDown);
      if (keyVUp) CFRelease(keyVUp);
      CFRelease(source);
      result.outcome = InjectionOutcome::OtherFailure;
      return result;
    }
    CGEventSetFlags(keyVDown, kCGEventFlagMaskCommand);
    CGEventSetFlags(keyVUp, kCGEventFlagMaskCommand);

    CGEventPost(kCGHIDEventTap, keyVDown);
    CGEventPost(kCGHIDEventTap, keyVUp);

    CFRelease(keyVDown);
    CFRelease(keyVUp);
    CFRelease(source);

    result.outcome = InjectionOutcome::Succeeded;
    return result;
  }

  /// 006-paste-fallback-hotkey-feedback（FR-001/FR-002，research.md §1 表格第
  /// 二行）：重新执行与 FocusContext.cpp::captureCurrentFocus() 同构的"取屏幕
  /// 前到后排序窗口列表中第一个 layer==0 窗口的 kCGWindowOwnerPID"逻辑，与
  /// targetFocus.platformWindowRef 转换回的 pid_t 比较；不相等（含现在找不到
  /// 任何 layer==0 窗口）即判定为失效。
  bool isTargetFocusStillValid(const babel::focus::TargetFocusContext &targetFocus) const override {
    const pid_t capturedPid = static_cast<pid_t>(reinterpret_cast<intptr_t>(targetFocus.platformWindowRef));

    bool found = false;
    pid_t currentPid = 0;
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
          currentPid = static_cast<pid_t>(pid);
          found = true;
        }
        break;
      }
      CFRelease(windowList);
    }

    return found && currentPid == capturedPid;
  }
};

}  // namespace

std::unique_ptr<PasteInjector> createPlatformPasteInjector() { return std::make_unique<PasteInjectorMac>(); }

}  // namespace babel::inject
