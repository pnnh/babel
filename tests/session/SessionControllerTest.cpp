#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "babel/src/inject/ClipboardWriter.h"
#include "babel/src/inject/PasteInjector.h"
#include "babel/src/session/SessionController.h"

using babel::inject::ClipboardWriter;
using babel::inject::InjectionAttemptResult;
using babel::inject::InjectionOutcome;
using babel::inject::PasteInjector;
using babel::session::FloatingSessionState;
using babel::session::SessionController;

namespace {

/// 记录调用顺序的剪贴板写入测试替身（不依赖真实 QClipboard/QGuiApplication）。
class RecordingClipboardWriter : public ClipboardWriter {
 public:
  explicit RecordingClipboardWriter(std::vector<std::string> *log) : m_log(log) {}
  void write(const QString &text) override { m_log->push_back("clipboard:" + text.toStdString()); }

 private:
  std::vector<std::string> *m_log;
};

/// 记录调用顺序、返回预设 outcome 的粘贴注入测试替身。
class RecordingPasteInjector : public PasteInjector {
 public:
  RecordingPasteInjector(std::vector<std::string> *log, InjectionOutcome outcome) : m_log(log), m_outcome(outcome) {}
  InjectionAttemptResult attemptPaste(const babel::focus::TargetFocusContext & /*targetFocus*/) override {
    m_log->push_back("paste");
    InjectionAttemptResult result;
    result.outcome = m_outcome;
    return result;
  }

 private:
  std::vector<std::string> *m_log;
  InjectionOutcome m_outcome;
};

/// 构造一个已"唤起"（非 Hidden）的 SessionController，仅注入
/// ClipboardWriter/PasteInjector 测试替身；不装配 GlobalHotkey/FastCorrector/
/// DebounceScheduler/LlmEngine（measurement 聚焦 confirm()/cancel() 本身）。
std::unique_ptr<SessionController> buildInvokedController(std::vector<std::string> *log, InjectionOutcome outcome) {
  auto controller = std::make_unique<SessionController>();
  controller->setClipboardWriter(std::make_unique<RecordingClipboardWriter>(log));
  controller->setPasteInjector(std::make_unique<RecordingPasteInjector>(log, outcome));
  controller->onHotkeyToggled();  // Hidden -> Invoked/ModelUnavailable
  controller->submitKeystrokes(QStringLiteral("草稿"));
  return controller;
}

}  // namespace

// T019 [US1]：SessionController.confirm()/cancel() 状态迁移与"剪贴板写入先于
// 粘贴注入"调用顺序测试（contracts/injection-outcome-contract.md）。

TEST(SessionControllerTest, ConfirmWritesClipboardBeforeAttemptingPaste) {
  std::vector<std::string> log;
  auto controller = buildInvokedController(&log, InjectionOutcome::Succeeded);
  controller->confirm();
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log[0].rfind("clipboard:", 0), 0u);  // 剪贴板写入 MUST 先于粘贴注入
  EXPECT_EQ(log[1], "paste");
}

TEST(SessionControllerTest, ConfirmSucceededClosesWithoutManualPasteHint) {
  std::vector<std::string> log;
  auto controller = buildInvokedController(&log, InjectionOutcome::Succeeded);
  controller->confirm();
  EXPECT_EQ(controller->sessionState(), FloatingSessionState::Hidden);
  EXPECT_FALSE(controller->manualPasteHintVisible());
}

TEST(SessionControllerTest, ConfirmNonSucceededShowsUnifiedManualPasteHint) {
  for (const auto outcome :
       {InjectionOutcome::UnsupportedEnvironment, InjectionOutcome::PermissionDenied, InjectionOutcome::OtherFailure}) {
    std::vector<std::string> log;
    auto controller = buildInvokedController(&log, outcome);
    controller->confirm();
    EXPECT_TRUE(controller->manualPasteHintVisible());
    // 剪贴板写入仍 MUST 已完成（FR-012：即便注入失败，用户仍可手动粘贴）。
    ASSERT_FALSE(log.empty());
    EXPECT_EQ(log.front().rfind("clipboard:", 0), 0u);
  }
}

TEST(SessionControllerTest, CancelProducesNoClipboardOrPasteAction) {
  std::vector<std::string> log;
  auto controller = buildInvokedController(&log, InjectionOutcome::Succeeded);
  controller->cancel();
  EXPECT_TRUE(log.empty());
  EXPECT_EQ(controller->sessionState(), FloatingSessionState::Hidden);
}
