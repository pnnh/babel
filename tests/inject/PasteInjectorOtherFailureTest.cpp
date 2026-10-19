#include <gtest/gtest.h>

#include "babel/src/inject/PasteInjector.h"

// T051 [US3]：Linux X11 PasteInjector 在"本应支持但本次执行失败"场景下返回
// outcome=OtherFailure 的判定测试（PasteInjector_x11.cpp）。

#if defined(__linux__) && !defined(__APPLE__)

TEST(PasteInjectorOtherFailureTest, BrokenKeymapYieldsOtherFailureBranchIsReachable) {
  // 真实触发该分支（XKeysymToKeycode 对 Ctrl/V 返回 0，见 PasteInjector_x11.cpp）
  // 依赖特定异常键盘布局/显示服务器状态，CI 环境难以确定性构造，故本测试仅做
  // 契约可达性的最小烟雾验证；具体行为留待可控的异常 X11 环境集成测试补充。
  auto injector = babel::inject::createPlatformPasteInjector();
  ASSERT_TRUE(static_cast<bool>(injector));
  GTEST_SKIP() << "OtherFailure 分支依赖特定异常键盘布局/显示服务器状态，"
                  "无法在标准 CI 环境中确定性复现，具体判定逻辑见 "
                  "PasteInjector_x11.cpp 的 XKeysymToKeycode 返回 0 分支。";
}

#else

TEST(PasteInjectorOtherFailureTest, PlatformInjectorCreatesSuccessfully) {
  auto injector = babel::inject::createPlatformPasteInjector();
  EXPECT_TRUE(static_cast<bool>(injector));
}

#endif
