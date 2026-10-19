#include "babel/src/session/SessionController.h"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <spdlog/spdlog.h>

#include <thread>

#include "babel/src/fastpath/FastCorrector.h"
#include "babel/src/hotkey/GlobalHotkey.h"
#include "babel/src/inject/ClipboardWriter.h"
#include "babel/src/inject/PasteInjector.h"
#include "babel/src/slowpath/DebounceScheduler.h"
#include "babel/src/slowpath/LlmEngine.h"
#include "babel/src/slowpath/ModelPaths.h"

namespace babel::session {

SessionController::SessionController(QObject *parent) : QObject(parent) {
  m_pollTimer = new QTimer(this);
  m_pollTimer->setInterval(30);
  connect(m_pollTimer, &QTimer::timeout, this, &SessionController::pollSlowPath);
}

SessionController::~SessionController() {
  if (m_globalHotkey) {
    m_globalHotkey->unregisterHotkey();
  }
}

void SessionController::bootstrapPlatformDefaults() {
  m_fastCorrector = std::make_unique<babel::fastpath::FastCorrector>();

  auto llmEngine = std::make_unique<babel::slowpath::LlmEngine>(babel::slowpath::ModelPaths::conventionalModelFilePath());
  m_modelAvailable = llmEngine->modelAvailable();
  m_llmEngine = std::move(llmEngine);
  emit modelAvailableChanged();

  // 防抖窗口 200ms：配合首批内容 <300ms、整句 <800ms 的目标（SC-003）。
  m_debounceScheduler = std::make_unique<babel::slowpath::DebounceScheduler>(
      std::chrono::milliseconds(200), [this](const QString &snapshot) { onSlowPathTriggered(snapshot); });

  m_clipboardWriter = std::make_unique<babel::inject::ClipboardWriter>();
  m_pasteInjector = babel::inject::createPlatformPasteInjector();

  m_globalHotkey = babel::hotkey::createPlatformGlobalHotkey();
  if (m_globalHotkey) {
    const bool hotkeyRegistered =
        m_globalHotkey->registerHotkey(babel::hotkey::HotkeyBinding::defaultBinding(), [this]() {
          // 热键回调运行在平台事件线程上，跨线程安全地切回 Qt 主线程再处理状态变更。
          QMetaObject::invokeMethod(this, "onHotkeyToggled", Qt::QueuedConnection);
        });
    // 契约（global-hotkey-contract.md）MUST NOT 发明用户可见的冲突提示，
    // 此处仅记录内部诊断日志，便于排查"热键无响应"问题（如与系统/其他应用已占用的组合键冲突）。
    if (hotkeyRegistered) {
      spdlog::info("全局热键注册成功（默认组合键）");
    } else {
      spdlog::warn("全局热键注册失败：可能与系统或其他应用已占用的快捷键冲突，浮窗将无法通过热键唤起");
    }
  } else {
    spdlog::warn("当前平台未提供全局热键实现，浮窗将无法通过热键唤起");
  }

  m_pollTimer->start();
}

void SessionController::setFastCorrector(std::unique_ptr<babel::fastpath::FastCorrector> corrector) {
  m_fastCorrector = std::move(corrector);
}

void SessionController::setDebounceScheduler(std::unique_ptr<babel::slowpath::DebounceScheduler> scheduler) {
  m_debounceScheduler = std::move(scheduler);
}

void SessionController::setLlmEngine(std::unique_ptr<babel::slowpath::LlmEngine> engine) {
  m_modelAvailable = engine && engine->modelAvailable();
  m_llmEngine = std::move(engine);
  emit modelAvailableChanged();
}

void SessionController::setClipboardWriter(std::unique_ptr<babel::inject::ClipboardWriter> writer) {
  m_clipboardWriter = std::move(writer);
}

void SessionController::setPasteInjector(std::unique_ptr<babel::inject::PasteInjector> injector) {
  m_pasteInjector = std::move(injector);
}

void SessionController::setGlobalHotkey(std::unique_ptr<babel::hotkey::GlobalHotkey> hotkey) {
  if (m_globalHotkey) {
    m_globalHotkey->unregisterHotkey();
  }
  m_globalHotkey = std::move(hotkey);
}

QString SessionController::outputPreviewState() const {
  switch (m_outputPreview.generationState) {
    case GenerationState::Idle:
      return QStringLiteral("Idle");
    case GenerationState::Debouncing:
      return QStringLiteral("Debouncing");
    case GenerationState::Generating:
      return QStringLiteral("Generating");
    case GenerationState::Ready:
      return QStringLiteral("Ready");
    case GenerationState::Stale:
      return QStringLiteral("Stale");
  }
  return QStringLiteral("Idle");
}

void SessionController::setState(FloatingSessionState next) { m_state = next; }

void SessionController::resetSessionData() {
  m_rawDraft = RawDraftText{};
  m_normalizedCandidate = NormalizedCandidateText{};
  m_outputPreview = StandardOutputPreview{};
  m_manualPasteHintVisible = false;
  emit rawInputSlotTextChanged();
  emit outputPreviewTextChanged();
  emit outputPreviewStateChanged();
  emit manualPasteHintVisibleChanged();
}

void SessionController::onHotkeyToggled() {
  // 诊断日志：确认热键按下事件确实从平台层传递到此处（排查"注册成功但无响应"问题）。
  spdlog::info("收到全局热键触发回调，当前会话状态 = {}", static_cast<int>(m_state));
  if (m_state == FloatingSessionState::Hidden) {
    m_targetFocus = babel::focus::captureCurrentFocus();
    resetSessionData();
    setState(m_modelAvailable ? FloatingSessionState::Invoked : FloatingSessionState::ModelUnavailable);
    emit sessionInvoked();
  } else {
    cancel();
  }
}

void SessionController::submitKeystrokes(const QString &text) {
  if (m_state == FloatingSessionState::Hidden) {
    return;
  }
  m_rawDraft.content = text;
  m_rawDraft.lastKeystrokeAt = std::chrono::system_clock::now();

  m_normalizedCandidate.content = m_fastCorrector ? m_fastCorrector->correct(text) : text;
  m_normalizedCandidate.updatedAt = std::chrono::system_clock::now();
  emit rawInputSlotTextChanged();

  if (m_state == FloatingSessionState::Invoked) {
    setState(FloatingSessionState::Editing);
  }

  if (m_modelAvailable && m_debounceScheduler) {
    m_debounceScheduler->onInputChanged(m_normalizedCandidate.content);
    m_outputPreview.generationState = m_debounceScheduler->state();
    emit outputPreviewStateChanged();
  }
}

void SessionController::pollSlowPath() {
  if (!m_debounceScheduler) {
    return;
  }
  const auto before = m_debounceScheduler->state();
  m_debounceScheduler->tick();
  const auto after = m_debounceScheduler->state();
  if (before != after) {
    m_outputPreview.generationState = after;
    emit outputPreviewStateChanged();
  }
}

void SessionController::onSlowPathTriggered(const QString &inputSnapshot) {
  m_outputPreview.generationState = babel::session::GenerationState::Generating;
  m_outputPreview.requestedAt = std::chrono::system_clock::now();
  emit outputPreviewStateChanged();

  if (!m_modelAvailable || !m_llmEngine) {
    // 不应到达（modelAvailable=false 时不驱动慢流，见 bootstrapPlatformDefaults()/
    // submitKeystrokes() 的 m_modelAvailable 判定），此处仅作防御性处理。
    if (m_debounceScheduler) {
      m_debounceScheduler->markGenerationComplete();
    }
    return;
  }

  // 在后台线程执行推理，避免阻塞 UI 线程；结果通过 QueuedConnection 回传主线程，
  // 并以 QPointer 守护 SessionController 可能已被销毁（如浮窗已关闭/应用退出）的情况。
  babel::slowpath::LlmEngine *engine = m_llmEngine.get();
  QPointer<SessionController> self(this);
  std::thread([self, engine, inputSnapshot]() {
    const QString generated = engine->generate(inputSnapshot);
    if (!self) {
      return;
    }
    QMetaObject::invokeMethod(
        self.data(),
        [self, generated]() {
          if (!self) {
            return;
          }
          self->m_outputPreview.content = generated;
          self->m_outputPreview.readyAt = std::chrono::system_clock::now();
          if (self->m_debounceScheduler) {
            self->m_debounceScheduler->markGenerationComplete();
            self->m_outputPreview.generationState = self->m_debounceScheduler->state();
          } else {
            self->m_outputPreview.generationState = babel::session::GenerationState::Ready;
          }
          emit self->outputPreviewTextChanged();
          emit self->outputPreviewStateChanged();
        },
        Qt::QueuedConnection);
  }).detach();
}

void SessionController::confirm() {
  if (m_state == FloatingSessionState::Hidden) {
    return;
  }
  if (m_clipboardWriter) {
    m_clipboardWriter->write(m_outputPreview.content);
  }

  babel::inject::InjectionOutcome outcome = babel::inject::InjectionOutcome::OtherFailure;
  if (m_pasteInjector) {
    outcome = m_pasteInjector->attemptPaste(m_targetFocus).outcome;
  }

  if (outcome == babel::inject::InjectionOutcome::Succeeded) {
    // 成功路径：浮窗正常关闭，不展示额外提示（contracts/injection-outcome-contract.md）。
    m_manualPasteHintVisible = false;
    emit manualPasteHintVisibleChanged();
    setState(FloatingSessionState::Confirmed);
    emit sessionClosed(CloseReason::Confirmed);
    setState(FloatingSessionState::Hidden);
  } else {
    // 非 Succeeded（UnsupportedEnvironment/PermissionDenied/OtherFailure）：
    // MUST 统一展示"已复制到剪贴板，请手动粘贴"，不区分具体 outcome（FR-009/FR-012）。
    // 浮窗保持可见以便用户看到提示，由用户按 Esc/再次触发热键（cancel()）关闭。
    m_manualPasteHintVisible = true;
    emit manualPasteHintVisibleChanged();
    setState(FloatingSessionState::Confirmed);
  }
}

void SessionController::cancel() {
  if (m_state == FloatingSessionState::Hidden) {
    return;
  }
  setState(FloatingSessionState::Cancelled);
  emit sessionClosed(CloseReason::Cancelled);
  resetSessionData();
  setState(FloatingSessionState::Hidden);
}

}  // namespace babel::session
