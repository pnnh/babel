#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "babel/src/inject/PasteInjector.h"

using babel::inject::InjectionOutcome;

// T042 [US2]：Wayland 会话下 PasteInjector 返回 outcome=UnsupportedEnvironment
// 的判定测试（对应 FR-009，babel/src/inject/PasteInjector_x11.cpp 的 Linux 分支）。
//
// 本测试仅在 Linux（UNIX AND NOT APPLE）构建中实际编译/运行 PasteInjector_x11.cpp
// 分支；在其余平台上，createPlatformPasteInjector() 返回对应平台实现，
// 不具备 Wayland 判定语义，故仅做"创建成功"的最小烟雾断言，避免误报。

#if defined(__linux__) && !defined(__APPLE__)

namespace {
class ScopedEnvVar {
 public:
  ScopedEnvVar(const char *name, const char *value) : m_name(name) {
    const char *prev = std::getenv(name);
    m_hadPrev = prev != nullptr;
    if (m_hadPrev) {
      m_prevValue = prev;
    }
    setenv(name, value, 1);
  }
  ~ScopedEnvVar() {
    if (m_hadPrev) {
      setenv(m_name, m_prevValue.c_str(), 1);
    } else {
      unsetenv(m_name);
    }
  }

 private:
  const char *m_name;
  bool m_hadPrev = false;
  std::string m_prevValue;
};
}  // namespace

TEST(PasteInjectorUnsupportedTest, WaylandSessionTypeYieldsUnsupportedEnvironment) {
  ScopedEnvVar sessionType("XDG_SESSION_TYPE", "wayland");
  auto injector = babel::inject::createPlatformPasteInjector();
  ASSERT_TRUE(static_cast<bool>(injector));
  babel::focus::TargetFocusContext ctx;
  const auto result = injector->attemptPaste(ctx);
  EXPECT_EQ(result.outcome, InjectionOutcome::UnsupportedEnvironment);
}

#else

TEST(PasteInjectorUnsupportedTest, PlatformInjectorCreatesSuccessfully) {
  auto injector = babel::inject::createPlatformPasteInjector();
  EXPECT_TRUE(static_cast<bool>(injector));
}

#endif
