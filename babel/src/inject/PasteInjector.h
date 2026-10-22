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

  /// 006-paste-fallback-hotkey-feedback（FR-001/FR-002，contracts/target-focus-
  /// validity-contract.md，data-model.md 二.1）：判断 targetFocus 捕获时刻记录
  /// 的目标窗口/进程，此刻是否仍是系统当前焦点。默认实现返回 true（既有行为：
  /// 不检测、总是视为仍然有效），保证 001～005 既有调用点与既有测试替身（仅
  /// override attemptPaste() 的匿名测试类）无需任何修改即可继续编译、行为不变；
  /// 三个平台的真实实现 override 为"重新捕获当前系统焦点 + 与捕获时刻记录值
  /// 比较"逻辑（research.md §1）。
  virtual bool isTargetFocusStillValid(const babel::focus::TargetFocusContext & /*targetFocus*/) const {
    return true;
  }
};

/// 创建当前平台对应的 PasteInjector 实现。
std::unique_ptr<PasteInjector> createPlatformPasteInjector();

}  // namespace babel::inject
