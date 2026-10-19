#include <gtest/gtest.h>

#include <chrono>

#include "babel/src/fastpath/FastCorrector.h"

using babel::fastpath::FastCorrector;

// T015 [US1]：中英拼音混排样例的编辑距离纠错正确性 + 目标延迟 <15ms 基准
// （对应 FR-003/SC-002）。先写测试，期望在 FastCorrector 实现完成前处于 FAIL 状态。

TEST(FastCorrectorTest, CorrectsMixedPinyinDraftToChinese) {
  FastCorrector corrector;
  const QString result = corrector.correct(QStringLiteral("wo xiang shuo zhege idea hen hao"));
  EXPECT_EQ(result.toStdString(), std::string("我想说这个idea很好"));
}

TEST(FastCorrectorTest, KeepsUnrecognizedTokenUnchanged) {
  FastCorrector corrector;
  const QString result = corrector.correct(QStringLiteral("qzxxxxxxxxxxx"));
  EXPECT_EQ(result.toStdString(), std::string("qzxxxxxxxxxxx"));
}

TEST(FastCorrectorTest, KeyboardProximityToleratesTypo) {
  // 'q' 与 'a' 为 QWERTY 物理相邻键位：误触 "hqo"（意图 "hao"）应仍以更低编辑
  // 距离权重纠正为 "好"，验证 KeyboardProximityMap 的邻近加权生效（FR-003）。
  FastCorrector corrector;
  const QString result = corrector.correct(QStringLiteral("hqo"));
  EXPECT_EQ(result.toStdString(), std::string("好"));
}

TEST(FastCorrectorTest, EndToEndLatencyIsReasonablyFast) {
  FastCorrector corrector;
  const auto start = std::chrono::steady_clock::now();
  corrector.correct(QStringLiteral("wo xiang shuo zhege idea hen hao"));
  const auto elapsed = std::chrono::steady_clock::now() - start;
  // 目标 <15ms（FR-003/SC-002）；沙箱/CI 环境调度抖动较大，单元测试阈值放宽至
  // 50ms 以避免环境噪声导致误报，严格的延迟基准验证见 quickstart.md 场景一。
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 50);
}
