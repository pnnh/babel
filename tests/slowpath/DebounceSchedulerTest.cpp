#include <gtest/gtest.h>

#include "babel/src/slowpath/DebounceScheduler.h"

using babel::session::GenerationState;
using babel::slowpath::DebounceScheduler;

// T017 [US1]：防抖触发时序、输入再次变化时取消/重触发（FR-004/FR-005）。

TEST(DebounceSchedulerTest, StartsIdleAndMovesToDebouncingOnInput) {
  int triggerCount = 0;
  DebounceScheduler scheduler(std::chrono::milliseconds(100), [&](const QString &) { ++triggerCount; });
  EXPECT_EQ(scheduler.state(), GenerationState::Idle);

  const auto t0 = std::chrono::steady_clock::now();
  scheduler.onInputChanged(QStringLiteral("abc"), t0);
  EXPECT_EQ(scheduler.state(), GenerationState::Debouncing);
  EXPECT_EQ(triggerCount, 0);
}

TEST(DebounceSchedulerTest, TicksBeforeWindowElapsedDoNotTrigger) {
  int triggerCount = 0;
  DebounceScheduler scheduler(std::chrono::milliseconds(100), [&](const QString &) { ++triggerCount; });
  const auto t0 = std::chrono::steady_clock::now();
  scheduler.onInputChanged(QStringLiteral("abc"), t0);
  scheduler.tick(t0 + std::chrono::milliseconds(50));
  EXPECT_EQ(scheduler.state(), GenerationState::Debouncing);
  EXPECT_EQ(triggerCount, 0);
}

TEST(DebounceSchedulerTest, TriggersGeneratingAfterWindowElapsed) {
  int triggerCount = 0;
  QString lastSnapshot;
  DebounceScheduler scheduler(std::chrono::milliseconds(100), [&](const QString &snapshot) {
    ++triggerCount;
    lastSnapshot = snapshot;
  });
  const auto t0 = std::chrono::steady_clock::now();
  scheduler.onInputChanged(QStringLiteral("abc"), t0);
  scheduler.tick(t0 + std::chrono::milliseconds(150));
  EXPECT_EQ(scheduler.state(), GenerationState::Generating);
  EXPECT_EQ(triggerCount, 1);
  EXPECT_EQ(lastSnapshot, QStringLiteral("abc"));
}

TEST(DebounceSchedulerTest, NewInputDuringDebouncingResetsWindowWithoutExtraTrigger) {
  int triggerCount = 0;
  DebounceScheduler scheduler(std::chrono::milliseconds(100), [&](const QString &) { ++triggerCount; });
  const auto t0 = std::chrono::steady_clock::now();
  scheduler.onInputChanged(QStringLiteral("a"), t0);
  scheduler.tick(t0 + std::chrono::milliseconds(60));
  // 60ms 时输入再次变化：重置防抖窗口，不应在原定 100ms 时间点触发。
  scheduler.onInputChanged(QStringLiteral("ab"), t0 + std::chrono::milliseconds(60));
  scheduler.tick(t0 + std::chrono::milliseconds(110));
  EXPECT_EQ(scheduler.state(), GenerationState::Debouncing);
  EXPECT_EQ(triggerCount, 0);
  scheduler.tick(t0 + std::chrono::milliseconds(170));
  EXPECT_EQ(scheduler.state(), GenerationState::Generating);
  EXPECT_EQ(triggerCount, 1);
}

TEST(DebounceSchedulerTest, InputChangeDuringGeneratingMovesToStaleThenRetriggers) {
  int triggerCount = 0;
  DebounceScheduler scheduler(std::chrono::milliseconds(100), [&](const QString &) { ++triggerCount; });
  const auto t0 = std::chrono::steady_clock::now();
  scheduler.onInputChanged(QStringLiteral("a"), t0);
  scheduler.tick(t0 + std::chrono::milliseconds(110));
  ASSERT_EQ(scheduler.state(), GenerationState::Generating);
  ASSERT_EQ(triggerCount, 1);

  // 生成过程中输入再次变化：迁移至 Stale（data-model.md §3）。
  scheduler.onInputChanged(QStringLiteral("ab"), t0 + std::chrono::milliseconds(120));
  EXPECT_EQ(scheduler.state(), GenerationState::Stale);

  // llama_context 不支持并发 decode：只要上一次触发的生成仍在后台线程执行中
  // （尚未调用 markGenerationComplete()），即便防抖窗口已到期也 MUST NOT 重新
  // 触发第二次生成，避免并发调用同一 LlmEngine 实例导致崩溃。
  scheduler.tick(t0 + std::chrono::milliseconds(230));
  EXPECT_EQ(scheduler.state(), GenerationState::Stale);
  EXPECT_EQ(triggerCount, 1);

  // 待上一次生成真正完成（调用 markGenerationComplete()）后，后续 tick() 才
  // 应重新触发生成。
  scheduler.markGenerationComplete();
  scheduler.tick(t0 + std::chrono::milliseconds(240));
  EXPECT_EQ(scheduler.state(), GenerationState::Generating);
  EXPECT_EQ(triggerCount, 2);
}

TEST(DebounceSchedulerTest, MarkGenerationCompleteMovesToReadyWhenNoNewInput) {
  DebounceScheduler scheduler(std::chrono::milliseconds(100), [](const QString &) {});
  const auto t0 = std::chrono::steady_clock::now();
  scheduler.onInputChanged(QStringLiteral("a"), t0);
  scheduler.tick(t0 + std::chrono::milliseconds(110));
  ASSERT_EQ(scheduler.state(), GenerationState::Generating);
  scheduler.markGenerationComplete();
  EXPECT_EQ(scheduler.state(), GenerationState::Ready);
}

TEST(DebounceSchedulerTest, ReadyStateMovesToStaleOnNewInput) {
  DebounceScheduler scheduler(std::chrono::milliseconds(100), [](const QString &) {});
  const auto t0 = std::chrono::steady_clock::now();
  scheduler.onInputChanged(QStringLiteral("a"), t0);
  scheduler.tick(t0 + std::chrono::milliseconds(110));
  scheduler.markGenerationComplete();
  ASSERT_EQ(scheduler.state(), GenerationState::Ready);

  scheduler.onInputChanged(QStringLiteral("ab"), t0 + std::chrono::milliseconds(120));
  EXPECT_EQ(scheduler.state(), GenerationState::Stale);
}

// T013 [US1]：setDebounceWindow() 不改变既有状态机字段（FR-013，contracts/
// debounce-tier-window-internal-contract.md 五 1）。分别在 Debouncing/
// Generating/Stale 等既有状态下调用，断言调用前后 state() 不变。
TEST(DebounceSchedulerTest, SetDebounceWindowDoesNotAlterExistingStateFields) {
  {
    // Debouncing 状态下调用。
    DebounceScheduler scheduler(std::chrono::milliseconds(100), [](const QString &) {});
    const auto t0 = std::chrono::steady_clock::now();
    scheduler.onInputChanged(QStringLiteral("a"), t0);
    ASSERT_EQ(scheduler.state(), GenerationState::Debouncing);
    scheduler.setDebounceWindow(std::chrono::milliseconds(500));
    EXPECT_EQ(scheduler.state(), GenerationState::Debouncing);
  }
  {
    // Generating 状态下调用。
    DebounceScheduler scheduler(std::chrono::milliseconds(100), [](const QString &) {});
    const auto t0 = std::chrono::steady_clock::now();
    scheduler.onInputChanged(QStringLiteral("a"), t0);
    scheduler.tick(t0 + std::chrono::milliseconds(110));
    ASSERT_EQ(scheduler.state(), GenerationState::Generating);
    scheduler.setDebounceWindow(std::chrono::milliseconds(800));
    EXPECT_EQ(scheduler.state(), GenerationState::Generating);
  }
  {
    // Stale 状态下调用（生成期间输入再次变化）。
    DebounceScheduler scheduler(std::chrono::milliseconds(100), [](const QString &) {});
    const auto t0 = std::chrono::steady_clock::now();
    scheduler.onInputChanged(QStringLiteral("a"), t0);
    scheduler.tick(t0 + std::chrono::milliseconds(110));
    scheduler.onInputChanged(QStringLiteral("ab"), t0 + std::chrono::milliseconds(120));
    ASSERT_EQ(scheduler.state(), GenerationState::Stale);
    scheduler.setDebounceWindow(std::chrono::milliseconds(200));
    EXPECT_EQ(scheduler.state(), GenerationState::Stale);
  }
}

// T014 [US1]：setDebounceWindow() 调用后下一次 tick() 立即使用新窗口值判定，
// 调用前已排定的触发不受影响（contracts/debounce-tier-window-internal-
// contract.md 一/五 2）。
TEST(DebounceSchedulerTest, SetDebounceWindowAffectsNextTickButNotPastTrigger) {
  int triggerCount = 0;
  DebounceScheduler scheduler(std::chrono::milliseconds(100), [&](const QString &) { ++triggerCount; });
  const auto t0 = std::chrono::steady_clock::now();
  scheduler.onInputChanged(QStringLiteral("abc"), t0);
  scheduler.setDebounceWindow(std::chrono::milliseconds(500));

  // 旧 100ms 窗口下 t0+150ms 本应已触发，新 500ms 窗口下尚未到期，不应触发。
  scheduler.tick(t0 + std::chrono::milliseconds(150));
  EXPECT_EQ(scheduler.state(), GenerationState::Debouncing);
  EXPECT_EQ(triggerCount, 0);

  // 新 500ms 窗口到期后才触发。
  scheduler.tick(t0 + std::chrono::milliseconds(550));
  EXPECT_EQ(scheduler.state(), GenerationState::Generating);
  EXPECT_EQ(triggerCount, 1);
}
