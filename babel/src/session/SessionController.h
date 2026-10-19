#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "babel/src/focus/FocusContext.h"
#include "babel/src/hotkey/HotkeyBinding.h"
#include "babel/src/inject/InjectionResult.h"
#include "babel/src/session/DraftTypes.h"
#include "babel/src/session/OutputPreviewTypes.h"

class QTimer;

namespace babel::fastpath {
class FastCorrector;
}
namespace babel::slowpath {
class DebounceScheduler;
class LlmEngine;
}  // namespace babel::slowpath
namespace babel::hotkey {
class GlobalHotkey;
}
namespace babel::inject {
class ClipboardWriter;
class PasteInjector;
}  // namespace babel::inject

namespace babel::session {

/// 浮窗会话状态机（data-model.md §6），是 RawDraftText/StandardOutputPreview 等
/// 实体的宿主上下文。对应 User Story 1/2/3 的整体交互流程。
enum class FloatingSessionState {
  Hidden,          ///< 初始状态 / 确认或取消后
  Invoked,         ///< 用户按下全局热键（FR-001），捕获 TargetFocusContext，浮窗显示
  Editing,         ///< 浮窗可输入状态下用户持续击键
  Confirmed,       ///< 用户按回车（FR-006）
  Cancelled,       ///< 用户按 Esc 或再次触发热键（FR-006）
  ModelUnavailable,  ///< 本地 LLM 模型文件缺失/损坏/无法加载（FR-011）
};

/// 暴露给 QML 的浮窗会话控制器（contracts/qml-cpp-session-interface.md）。
/// 承载一次浮窗会话（FloatingSessionState）的全部可观察状态。
///
/// 本阶段（Foundational，T011）仅声明只读属性/信号/可调用方法骨架，
/// submitKeystrokes()/confirm()/cancel() 的业务逻辑由 User Story 1 阶段
/// （T036/T037/T038）在 SessionController.cpp 中补全实现。
/// 注册方式（T014）：在 main.cpp 中通过 qmlRegisterSingletonInstance() 手动注册为
/// QML 单例"SessionController"（classic 注册方式，不依赖 QML_ELEMENT/qmltyperegistrar
/// 跨 babel_core 静态库边界的构建期扫描，兼容性更可预期）。
class SessionController : public QObject {
  Q_OBJECT

  Q_PROPERTY(QString rawInputSlotText READ rawInputSlotText NOTIFY rawInputSlotTextChanged)
  Q_PROPERTY(QString outputPreviewText READ outputPreviewText NOTIFY outputPreviewTextChanged)
  Q_PROPERTY(QString outputPreviewState READ outputPreviewState NOTIFY outputPreviewStateChanged)
  Q_PROPERTY(QString learningTipText READ learningTipText NOTIFY learningTipTextChanged)
  Q_PROPERTY(bool learningTipVisible READ learningTipVisible NOTIFY learningTipVisibleChanged)
  Q_PROPERTY(bool modelAvailable READ modelAvailable NOTIFY modelAvailableChanged)
  Q_PROPERTY(bool manualPasteHintVisible READ manualPasteHintVisible NOTIFY manualPasteHintVisibleChanged)

 public:
  /// 浮窗关闭原因（qml-cpp-session-interface.md 的 sessionClosed(reason) 信号参数）。
  enum class CloseReason { Confirmed, Cancelled };
  Q_ENUM(CloseReason)

  explicit SessionController(QObject *parent = nullptr);
  ~SessionController() override;

  /// 装配生产环境的真实依赖（FastCorrector/LlmEngine/DebounceScheduler/
  /// ClipboardWriter/PasteInjector/GlobalHotkey）并启动慢流轮询定时器。
  /// 仅由 main.cpp 在生产环境调用；单元测试 MUST NOT 调用本方法，
  /// 应改用 setXxx() 注入测试替身，避免测试进程意外抢占系统全局热键等副作用。
  void bootstrapPlatformDefaults();

  QString rawInputSlotText() const { return m_normalizedCandidate.content; }
  QString outputPreviewText() const { return m_outputPreview.content; }
  QString outputPreviewState() const;
  QString learningTipText() const { return m_learningTipText; }
  bool learningTipVisible() const { return m_learningTipVisible; }
  bool modelAvailable() const { return m_modelAvailable; }
  bool manualPasteHintVisible() const { return m_manualPasteHintVisible; }

  FloatingSessionState sessionState() const { return m_state; }

  /// 供测试/集成装配注入依赖（非 QML 可见），均为非拥有裸指针语义以外的所有权转移。
  void setFastCorrector(std::unique_ptr<babel::fastpath::FastCorrector> corrector);
  void setDebounceScheduler(std::unique_ptr<babel::slowpath::DebounceScheduler> scheduler);
  void setLlmEngine(std::unique_ptr<babel::slowpath::LlmEngine> engine);
  void setClipboardWriter(std::unique_ptr<babel::inject::ClipboardWriter> writer);
  void setPasteInjector(std::unique_ptr<babel::inject::PasteInjector> injector);
  void setGlobalHotkey(std::unique_ptr<babel::hotkey::GlobalHotkey> hotkey);

 public slots:
  /// 每次击键后由 QML 调用（FR-003/FR-004/FR-005）。
  Q_INVOKABLE void submitKeystrokes(const QString &text);
  /// 回车确认（FR-006/FR-007/FR-008）。
  Q_INVOKABLE void confirm();
  /// Esc 或再次触发热键（FR-006）。
  Q_INVOKABLE void cancel();

  /// 由 GlobalHotkey 回调触发：首次唤起浮窗 / 已显示时再次按下则取消。
  void onHotkeyToggled();

  /// 周期性检查 DebounceScheduler 是否应触发慢流生成（由 bootstrapPlatformDefaults()
  /// 启动的轮询定时器驱动，默认构造不自动启动）。
  void pollSlowPath();

 signals:
  void sessionInvoked();
  void sessionClosed(babel::session::SessionController::CloseReason reason);

  void rawInputSlotTextChanged();
  void outputPreviewTextChanged();
  void outputPreviewStateChanged();
  void learningTipTextChanged();
  void learningTipVisibleChanged();
  void modelAvailableChanged();
  void manualPasteHintVisibleChanged();

 private:
  void setState(FloatingSessionState next);
  void onSlowPathTriggered(const QString &inputSnapshot);
  void resetSessionData();

  FloatingSessionState m_state = FloatingSessionState::Hidden;
  RawDraftText m_rawDraft;
  NormalizedCandidateText m_normalizedCandidate;
  StandardOutputPreview m_outputPreview;
  babel::focus::TargetFocusContext m_targetFocus;

  QString m_learningTipText;
  bool m_learningTipVisible = false;
  bool m_modelAvailable = false;
  bool m_manualPasteHintVisible = false;

  std::unique_ptr<babel::fastpath::FastCorrector> m_fastCorrector;
  std::unique_ptr<babel::slowpath::DebounceScheduler> m_debounceScheduler;
  std::unique_ptr<babel::slowpath::LlmEngine> m_llmEngine;
  std::unique_ptr<babel::inject::ClipboardWriter> m_clipboardWriter;
  std::unique_ptr<babel::inject::PasteInjector> m_pasteInjector;
  std::unique_ptr<babel::hotkey::GlobalHotkey> m_globalHotkey;
  QTimer *m_pollTimer = nullptr;
};

}  // namespace babel::session
