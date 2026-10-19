#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include "babel/src/fastpath/FastCorrector.h"
#include "babel/src/session/SessionController.h"
#include "babel/src/slowpath/LlmEngine.h"

using babel::session::FloatingSessionState;
using babel::session::SessionController;
using babel::slowpath::LlmEngine;

// T049 [US3]：SessionController 迁移至 ModelUnavailable 状态的测试
// （快流仍可用、慢流不可用且不崩溃，对应 FR-011）。

TEST(ModelUnavailableTest, HotkeyTogglesToModelUnavailableWhenModelMissing) {
  const std::filesystem::path missingPath =
      std::filesystem::temp_directory_path() / "babel_test_modelunavailable.gguf";
  std::filesystem::remove(missingPath);

  SessionController controller;
  controller.setLlmEngine(std::make_unique<LlmEngine>(missingPath));
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());

  ASSERT_FALSE(controller.modelAvailable());
  controller.onHotkeyToggled();
  EXPECT_EQ(controller.sessionState(), FloatingSessionState::ModelUnavailable);
}

TEST(ModelUnavailableTest, FastPathStillWorksWhileModelUnavailable) {
  const std::filesystem::path missingPath =
      std::filesystem::temp_directory_path() / "babel_test_modelunavailable2.gguf";
  std::filesystem::remove(missingPath);

  SessionController controller;
  controller.setLlmEngine(std::make_unique<LlmEngine>(missingPath));
  controller.setFastCorrector(std::make_unique<babel::fastpath::FastCorrector>());
  controller.onHotkeyToggled();

  // 快流（原始输入槽）不受慢流不可用影响，仍可正常规整化展示。
  EXPECT_NO_THROW(controller.submitKeystrokes(QStringLiteral("wo hao")));
  EXPECT_FALSE(controller.rawInputSlotText().isEmpty());
  // 慢流不应被驱动：outputPreviewState 保持 Idle，浮窗不崩溃、不无响应。
  EXPECT_EQ(controller.outputPreviewState(), QStringLiteral("Idle"));
}
