#pragma once

#include <QString>
#include <chrono>

#include "babel/src/fastpath/PinyinLattice.h"

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
///
/// 002-lattice-dict-model-switch：新增 `lattice` 字段承载多候选格信息
/// （data-model.md §1"与 NormalizedCandidateText 的关系"）；`content` 字段继续
/// 服务既有 QML `rawInputSlotText` 契约，取值来源由"快流直接产出单一字符串"改为
/// "由 `lattice.toDisplayText()` 派生"，对 QML 层不可感知差异。
struct NormalizedCandidateText {
  QString content;
  std::chrono::system_clock::time_point updatedAt{};
  babel::fastpath::PinyinLattice lattice;
};

}  // namespace babel::session
