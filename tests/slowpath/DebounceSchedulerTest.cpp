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

  // 新一轮防抖完成后应重新触发生成。
  scheduler.tick(t0 + std::chrono::milliseconds(230));
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
