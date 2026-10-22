#include "babel/src/inject/PasteInjector.h"

#include <windows.h>

namespace babel::inject {
namespace {

/// Windows 模拟粘贴注入（research.md §4.2：分平台自研薄封装，`SendInput`）。
/// 尽力而为路径（FR-008/FR-010）：Windows 对模拟输入的可用性因安全软件/
/// UAC 提权边界等因素而不确定，失败时统一判定为 OtherFailure，不单独探测
/// 具体原因（对外 UI 行为见 contracts/injection-outcome-contract.md）。
class PasteInjectorWin : public PasteInjector {
 public:
  InjectionAttemptResult attemptPaste(const babel::focus::TargetFocusContext & /*targetFocus*/) override {
    InjectionAttemptResult result;
    result.attemptedAt = std::chrono::system_clock::now();

    INPUT inputs[4] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    const UINT sent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
    result.outcome = (sent == ARRAYSIZE(inputs)) ? InjectionOutcome::Succeeded : InjectionOutcome::OtherFailure;
    return result;
  }

  /// 006-paste-fallback-hotkey-feedback（FR-001/FR-002，research.md §1 表格第
  /// 一行）：重新调用 GetForegroundWindow()，与 targetFocus.platformWindowRef
  /// 转换回的 HWND 做指针比较；不相等（含现在返回 NULL，如前台窗口已关闭）
  /// 即判定为失效。
  bool isTargetFocusStillValid(const babel::focus::TargetFocusContext &targetFocus) const override {
    const HWND currentForeground = GetForegroundWindow();
    const HWND capturedForeground = static_cast<HWND>(targetFocus.platformWindowRef);
    return currentForeground != nullptr && currentForeground == capturedForeground;
  }
};

}  // namespace

std::unique_ptr<PasteInjector> createPlatformPasteInjector() { return std::make_unique<PasteInjectorWin>(); }

}  // namespace babel::inject
