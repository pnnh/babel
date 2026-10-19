#pragma once

#include <memory>

#include "babel/src/focus/FocusContext.h"
#include "babel/src/inject/InjectionResult.h"

namespace babel::inject {

/// 模拟粘贴注入跨平台抽象接口（contracts/injection-outcome-contract.md）。
/// 分平台自研薄封装实现（research.md §4.2）：Windows `SendInput` / macOS
/// `CGEventPost` / Linux X11 `XTest`。
class PasteInjector {
 public:
  virtual ~PasteInjector() = default;

  /// 尝试向 targetFocus 对应窗口模拟标准粘贴操作（Windows/Linux Ctrl+V，macOS Cmd+V）。
  /// 前置条件：调用方 MUST 已先完成 ClipboardWriter::write()（不可颠倒）。
  virtual InjectionAttemptResult attemptPaste(const babel::focus::TargetFocusContext &targetFocus) = 0;
};

/// 创建当前平台对应的 PasteInjector 实现。
std::unique_ptr<PasteInjector> createPlatformPasteInjector();

}  // namespace babel::inject
