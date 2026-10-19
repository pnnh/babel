#pragma once

#include <QString>
#include <chrono>
#include <functional>

#include "babel/src/session/OutputPreviewTypes.h"

namespace babel::slowpath {

/// 防抖调度器（FR-004/FR-005，目标首批内容 <300ms、整句 <800ms，SC-003）。
/// 驱动 `Idle -> Debouncing -> Generating -> Ready/Stale -> Generating` 迁移
/// （data-model.md §3）。
///
/// 设计为"假时钟可驱动"（不直接依赖 Qt 事件循环/QTimer）：
/// - `onInputChanged()` 由 SessionController 在每次击键后调用；
/// - `tick()` 由调用方周期性调用（生产环境由 QTimer 驱动，单元测试可传入任意
///   模拟时间戳），防抖窗口到期时触发一次 `onTrigger` 回调并迁移到 Generating；
/// - `markGenerationComplete()` 由慢流（LlmEngine）生成完成后调用，驱动
///   Generating -> Ready（若期间无新输入）。
class DebounceScheduler {
 public:
  using GenerateCallback = std::function<void(const QString &inputSnapshot)>;

  DebounceScheduler(std::chrono::milliseconds debounceWindow, GenerateCallback onTrigger);

  void onInputChanged(const QString &currentInput,
                      std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

  void tick(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

  /// 慢流生成完成：若期间无新输入（当前仍处于 Generating）则迁移至 Ready；
  /// 若期间已有新输入（处于 Stale），则保持 Stale，交由后续 tick() 重新触发。
  void markGenerationComplete();

  babel::session::GenerationState state() const { return m_state; }

 private:
  std::chrono::milliseconds m_debounceWindow;
  GenerateCallback m_onTrigger;
  babel::session::GenerationState m_state = babel::session::GenerationState::Idle;
  QString m_pendingInput;
  std::chrono::steady_clock::time_point m_lastChangeAt{};
  bool m_timerArmed = false;
  // llama_context 非线程安全，不可并发 decode：标记上一次触发的生成是否仍在
  // 后台线程执行中（即 markGenerationComplete() 尚未被调用），tick() 据此避免
  // 在同一 LlmEngine 实例上重叠触发第二次 generate()（否则会破坏 ggml 计算图/
  // 调度器内部状态，导致底层断言失败崩溃）。
  bool m_generationInFlight = false;
};

}  // namespace babel::slowpath
