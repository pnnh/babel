#pragma once

#include <QString>

namespace babel::inject {

/// 剪贴板写入（FR-007），基于 `Qt6::Gui` `QClipboard`（既有依赖，research.md §4.1）。
/// `write()` 声明为 virtual 以便单元测试用不依赖真实 `QClipboard`/`QGuiApplication`
/// 事件循环的测试替身覆盖，验证"剪贴板写入先于粘贴注入"的调用顺序（T019）。
class ClipboardWriter {
 public:
  virtual ~ClipboardWriter() = default;

  /// 将标准中文文本写入系统剪贴板。MUST 在 PasteInjector::attemptPaste() 之前调用
  /// 完成（contracts/injection-outcome-contract.md 调用顺序，不可颠倒）。
  virtual void write(const QString &text);
};

}  // namespace babel::inject
