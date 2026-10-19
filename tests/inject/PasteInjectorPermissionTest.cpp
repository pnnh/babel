#include <gtest/gtest.h>

#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#endif

#include "babel/src/inject/PasteInjector.h"

using babel::inject::InjectionOutcome;

// T050 [US3]：macOS PasteInjector 在 Accessibility 权限未授予时返回
// outcome=PermissionDenied 的判定测试（PasteInjector_mac.mm）。

#if defined(__APPLE__)

TEST(PasteInjectorPermissionTest, UngrantedAccessibilityYieldsPermissionDenied) {
  if (AXIsProcessTrusted()) {
    GTEST_SKIP() << "当前测试进程已被授予 Accessibility 权限，无法在本次运行中验证"
                    "未授权分支；该分支已由 PasteInjector_mac.mm 的 AXIsProcessTrusted() "
                    "检测实现覆盖，建议在未授权环境（如 CI 默认状态）下运行本测试。";
  }
  auto injector = babel::inject::createPlatformPasteInjector();
  ASSERT_TRUE(static_cast<bool>(injector));
  babel::focus::TargetFocusContext ctx;
  const auto result = injector->attemptPaste(ctx);
  EXPECT_EQ(result.outcome, InjectionOutcome::PermissionDenied);
}

#else

TEST(PasteInjectorPermissionTest, PlatformInjectorCreatesSuccessfully) {
  auto injector = babel::inject::createPlatformPasteInjector();
  EXPECT_TRUE(static_cast<bool>(injector));
}

#endif
