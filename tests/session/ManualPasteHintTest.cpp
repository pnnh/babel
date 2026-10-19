#include <gtest/gtest.h>

#include <memory>

#include "babel/src/inject/ClipboardWriter.h"
#include "babel/src/inject/PasteInjector.h"
#include "babel/src/session/SessionController.h"

using babel::inject::ClipboardWriter;
using babel::inject::InjectionAttemptResult;
using babel::inject::InjectionOutcome;
using babel::inject::PasteInjector;
using babel::session::SessionController;

namespace {

class NoopClipboardWriter : public ClipboardWriter {
 public:
  void write(const QString & /*text*/) override {}
};

class FixedOutcomePasteInjector : public PasteInjector {
 public:
  explicit FixedOutcomePasteInjector(InjectionOutcome outcome) : m_outcome(outcome) {}
  InjectionAttemptResult attemptPaste(const babel::focus::TargetFocusContext & /*targetFocus*/) override {
    InjectionAttemptResult result;
    result.outcome = m_outcome;
    return result;
  }

 private:
  InjectionOutcome m_outcome;
};

bool confirmAndObserveHint(InjectionOutcome outcome) {
  SessionController controller;
  controller.setClipboardWriter(std::make_unique<NoopClipboardWriter>());
  controller.setPasteInjector(std::make_unique<FixedOutcomePasteInjector>(outcome));
  controller.onHotkeyToggled();
  controller.submitKeystrokes(QStringLiteral("x"));
  controller.confirm();
  return controller.manualPasteHintVisible();
}

}  // namespace

// T043 [US2]：SessionController 针对非 Succeeded outcome 统一展示
// "已复制到剪贴板，请手动粘贴"、不泄露具体 outcome 差异的测试（FR-012 强约束）。

TEST(ManualPasteHintTest, AllNonSucceededOutcomesShowSameHintFlag) {
  EXPECT_TRUE(confirmAndObserveHint(InjectionOutcome::UnsupportedEnvironment));
  EXPECT_TRUE(confirmAndObserveHint(InjectionOutcome::PermissionDenied));
  EXPECT_TRUE(confirmAndObserveHint(InjectionOutcome::OtherFailure));
}

TEST(ManualPasteHintTest, SucceededOutcomeDoesNotShowHint) {
  EXPECT_FALSE(confirmAndObserveHint(InjectionOutcome::Succeeded));
}
