#include <gtest/gtest.h>

#include <memory>

#include "babel/src/hotkey/GlobalHotkey.h"
#include "babel/src/hotkey/HotkeyBinding.h"
#include "babel/src/session/SessionController.h"

using babel::hotkey::GlobalHotkey;
using babel::hotkey::HotkeyBinding;
using babel::hotkey::Key;
using babel::hotkey::Modifier;
using babel::session::SessionController;

namespace {
/// 测试替身：不调用任何真实系统 API，仅记录注册的回调供测试手动触发，
/// 避免单元测试进程意外抢占真实系统全局热键。
class FakeGlobalHotkey : public GlobalHotkey {
 public:
  bool registerHotkey(const HotkeyBinding &binding, ToggleCallback onToggle) override {
    lastBinding = binding;
    callback = std::move(onToggle);
    registered = true;
    return true;
  }
  void unregisterHotkey() override {
    registered = false;
    callback = nullptr;
  }

  HotkeyBinding lastBinding;
  ToggleCallback callback;
  bool registered = false;
};
}  // namespace

// T018 [US1]：默认组合键校验、ToggleFloatingWindow 状态迁移语义
// （contracts/global-hotkey-contract.md）。

TEST(HotkeyBindingTest, DefaultBindingMatchesContract) {
  const HotkeyBinding binding = HotkeyBinding::defaultBinding();
  ASSERT_EQ(binding.modifiers.size(), 2u);
  EXPECT_EQ(binding.key, Key::Space);
  EXPECT_EQ(binding.modifiers[0], Modifier::Control);
#if defined(__APPLE__)
  EXPECT_EQ(binding.modifiers[1], Modifier::Option);  // macOS: Control+Option+Space
#else
  EXPECT_EQ(binding.modifiers[1], Modifier::Alt);  // Windows/Linux: Ctrl+Alt+Space
#endif
}

TEST(GlobalHotkeyContractTest, FakeImplementationStoresCallbackOnRegister) {
  FakeGlobalHotkey fake;
  bool invoked = false;
  const bool ok = fake.registerHotkey(HotkeyBinding::defaultBinding(), [&]() { invoked = true; });
  EXPECT_TRUE(ok);
  ASSERT_TRUE(static_cast<bool>(fake.callback));
  fake.callback();
  EXPECT_TRUE(invoked);
}

TEST(GlobalHotkeyContractTest, ToggleFloatingWindowSemanticsViaSessionController) {
  auto fakeHotkey = std::make_unique<FakeGlobalHotkey>();
  FakeGlobalHotkey *fakeHotkeyPtr = fakeHotkey.get();

  SessionController controller;
  controller.setGlobalHotkey(std::move(fakeHotkey));
  // 本测试聚焦 GlobalHotkey 契约语义，不经由 bootstrapPlatformDefaults() 装配，
  // 手动完成 registerHotkey 接线以模拟生产环境行为。
  fakeHotkeyPtr->registerHotkey(HotkeyBinding::defaultBinding(), [&controller]() { controller.onHotkeyToggled(); });

  int invokedCount = 0;
  QObject::connect(&controller, &SessionController::sessionInvoked, [&]() { ++invokedCount; });
  int closedCount = 0;
  SessionController::CloseReason lastReason = SessionController::CloseReason::Confirmed;
  QObject::connect(&controller, &SessionController::sessionClosed,
                    [&](SessionController::CloseReason reason) {
                      ++closedCount;
                      lastReason = reason;
                    });

  ASSERT_TRUE(static_cast<bool>(fakeHotkeyPtr->callback));

  // 首次按下（当前处于 Hidden）：捕获焦点、迁移至非 Hidden、发出 sessionInvoked()。
  fakeHotkeyPtr->callback();
  EXPECT_EQ(invokedCount, 1);
  EXPECT_NE(controller.sessionState(), babel::session::FloatingSessionState::Hidden);

  // 再次按下（当前处于非 Hidden）：Cancelled -> Hidden，不产生剪贴板写入/粘贴动作。
  fakeHotkeyPtr->callback();
  EXPECT_EQ(closedCount, 1);
  EXPECT_EQ(lastReason, SessionController::CloseReason::Cancelled);
  EXPECT_EQ(controller.sessionState(), babel::session::FloatingSessionState::Hidden);
}
