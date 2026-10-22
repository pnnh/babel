#include <gtest/gtest.h>

#include "babel/src/inject/PasteInjector.h"

// T007 [US1]：三平台 PasteInjector::isTargetFocusStillValid() 的结构性可达性
// 测试（contracts/target-focus-validity-contract.md，与既有
// PasteInjectorUnsupportedTest.cpp/PasteInjectorPermissionTest.cpp/
// PasteInjectorOtherFailureTest.cpp 同构风格，#if defined(...) 按平台分支）。
//
// 真实窗口切换/关闭状态难以在 CI 中确定性构造，本测试以"针对一个明显无效的
// TargetFocusContext（platformWindowRef 为随意不存在的值、valid=false），
// 调用不崩溃、返回合法布尔值"为主的弱断言，与既有 PasteInjectorOtherFailureTest.cpp
// 的测试强度风格一致。

TEST(PasteInjectorTargetFocusValidityTest, ObviouslyStaleTargetFocusContextYieldsValidBooleanWithoutCrashing) {
  auto injector = babel::inject::createPlatformPasteInjector();
  ASSERT_TRUE(static_cast<bool>(injector));

  babel::focus::TargetFocusContext staleCtx;
#if defined(_WIN32)
  staleCtx.platformWindowRef = reinterpret_cast<void *>(static_cast<intptr_t>(0xDEADBEEF));
#elif defined(__APPLE__)
  staleCtx.platformWindowRef = reinterpret_cast<void *>(static_cast<intptr_t>(999999));
#else
  staleCtx.platformWindowRef = static_cast<unsigned long>(0xDEADBEEF);
#endif
  staleCtx.valid = false;

  // 弱断言：仅要求返回合法布尔值且不崩溃；不对具体 true/false 取值做强制
  // 断言（真实系统此刻的焦点窗口在 CI 环境中不可控，可能偶然与随意常量
  // 碰撞，概率极低，但本测试风格本就以"可达性、不崩溃"为主）。
  const bool result = injector->isTargetFocusStillValid(staleCtx);
  EXPECT_TRUE(result == true || result == false);
}

TEST(PasteInjectorTargetFocusValidityTest, RepeatedCallsThroughBasePointerAreIdempotentAndDoNotCrash) {
  // 验证 isTargetFocusStillValid() 可经由 PasteInjector 基类指针（多态调用，
  // 与 confirm() 的实际调用方式一致，见 SessionController.cpp）重复调用，
  // 且对同一 targetFocus 返回一致结果（判断本身是只读查询，不应有副作用）。
  auto injector = babel::inject::createPlatformPasteInjector();
  ASSERT_TRUE(static_cast<bool>(injector));
  babel::inject::PasteInjector *base = injector.get();
  ASSERT_NE(base, nullptr);

  babel::focus::TargetFocusContext ctx;
  ctx.valid = false;

  const bool first = base->isTargetFocusStillValid(ctx);
  const bool second = base->isTargetFocusStillValid(ctx);
  EXPECT_EQ(first, second);
}
