#pragma once

#include <QString>
#include <chrono>
#include <optional>

namespace babel::session {

/// 慢流（本地大语言模型）生成状态迁移（data-model.md §3，对应 FR-004/FR-005 与
/// User Story 3 的"等待状态提示"验收场景）。
///
/// 迁移路径：
///   Idle --(原始输入变化)--> Debouncing --(防抖窗口到期)--> Generating --(推理完成)--> Ready
///   Generating/Ready --(原始输入再次变化)--> Stale --(新一轮防抖完成)--> Generating
///
/// 模型不可用时不进入本枚举的正常迁移路径，而是由 FloatingSessionState::ModelUnavailable
/// 承载（见 SessionController.h），本枚举不新增 Unavailable 取值。
enum class GenerationState {
  Idle,
  Debouncing,
  Generating,
  Ready,
  Stale,
};

/// 慢流本地大语言模型基于规整化候选文本生成的标准中文译写结果（data-model.md §3，FR-004）。
/// 是用户确认后成为最终上屏内容的来源。
struct StandardOutputPreview {
  QString content;
  GenerationState generationState = GenerationState::Idle;
  std::optional<std::chrono::system_clock::time_point> requestedAt;
  std::optional<std::chrono::system_clock::time_point> readyAt;
};

}  // namespace babel::session
