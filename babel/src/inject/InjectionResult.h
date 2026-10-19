#pragma once

#include <chrono>

namespace babel::inject {

/// 粘贴注入尝试结果（data-model.md §10，contracts/injection-outcome-contract.md）。
///
/// 强约束（直接对应 FR-012，不得在实现阶段放宽）：除 Succeeded 外的任一 outcome，
/// 对外 UI 层 MUST 统一渲染为同一条"已复制到剪贴板，请手动粘贴"提示文案，
/// MUST NOT 依据 outcome 的具体取值向用户展示差异化文案或原因说明。
/// outcome 枚举仅供内部日志/未来诊断使用，不得作为用户可见文案的数据源。
enum class InjectionOutcome {
  Succeeded,              ///< 注入执行成功
  UnsupportedEnvironment,  ///< 平台/合成器不支持注入（典型 Linux Wayland）
  PermissionDenied,        ///< 权限未授予（如 macOS 未授予 Accessibility 权限）
  OtherFailure,            ///< 平台理论上支持但本次执行失败
};

struct InjectionAttemptResult {
  InjectionOutcome outcome = InjectionOutcome::OtherFailure;
  std::chrono::system_clock::time_point attemptedAt{};
};

}  // namespace babel::inject
