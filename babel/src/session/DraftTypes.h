#pragma once

#include <QString>
#include <chrono>

namespace babel::session {

/// 用户在浮窗中实时击键产生的中英拼音混杂文本（data-model.md §1，FR-003 输入来源）。
/// 生命周期：随浮窗唤起创建为空文本，隐藏/关闭时丢弃，不跨会话保留。
struct RawDraftText {
  QString content;
  int cursorPosition = 0;
  std::chrono::system_clock::time_point lastKeystrokeAt{};
};

/// 原始输入草稿经快流本地词典/拼音纠错后的展示结果（data-model.md §2，FR-002/FR-003）。
/// 不构成最终上屏内容，仅用于"原始输入槽"区域展示。
struct NormalizedCandidateText {
  QString content;
  std::chrono::system_clock::time_point updatedAt{};
};

}  // namespace babel::session
