#include "babel/src/slowpath/DebounceScheduler.h"

namespace babel::slowpath {

using babel::session::GenerationState;

DebounceScheduler::DebounceScheduler(std::chrono::milliseconds debounceWindow, GenerateCallback onTrigger)
    : m_debounceWindow(debounceWindow), m_onTrigger(std::move(onTrigger)) {}

void DebounceScheduler::onInputChanged(const QString &currentInput, std::chrono::steady_clock::time_point now) {
  m_pendingInput = currentInput;
  m_lastChangeAt = now;
  m_timerArmed = true;
  switch (m_state) {
    case GenerationState::Idle:
    case GenerationState::Debouncing:
      m_state = GenerationState::Debouncing;
      break;
    case GenerationState::Generating:
    case GenerationState::Ready:
    case GenerationState::Stale:
      // 生成中/已就绪时再次变化：标记为 Stale，待新一轮防抖完成后重新触发生成
      // （data-model.md §3："Generating/Ready --(输入再次变化)--> Stale"）。
      m_state = GenerationState::Stale;
      break;
  }
}

void DebounceScheduler::tick(std::chrono::steady_clock::time_point now) {
  if (!m_timerArmed) {
    return;
  }
  if ((m_state != GenerationState::Debouncing && m_state != GenerationState::Stale)) {
    return;
  }
  if (now - m_lastChangeAt < m_debounceWindow) {
    return;
  }
  m_timerArmed = false;
  m_state = GenerationState::Generating;
  if (m_onTrigger) {
    m_onTrigger(m_pendingInput);
  }
}

void DebounceScheduler::markGenerationComplete() {
  if (m_state == GenerationState::Generating) {
    m_state = GenerationState::Ready;
  }
  // 若当前为 Stale（生成期间已有新输入），保持 Stale，不回退到 Ready，
  // 等待后续 tick() 在防抖窗口到期后重新触发生成。
}

}  // namespace babel::slowpath
