#include <gtest/gtest.h>

#include <QCoreApplication>

#include "babel/src/slowpath/LlmEngine.h"
#include "babel/src/slowpath/ModelPaths.h"

// 回归测试：真实调用 LlmEngine::generate() 验证"用户确认后进程崩溃
// （llama_sampler_dist_apply 断言失败：assert(found) 因 KV cache 跨调用累积、
// 数值溢出导致 logits 出现 NaN）"问题已修复（见 LlmEngine.cpp generate() 开头的
// llama_memory_clear() 调用）。依赖用户手动下载的真实模型文件（research.md §7），
// 文件不存在的环境中自动跳过，不影响 CI。

TEST(LlmEngineGenerateTest, GeneratesWithoutCrashingOnMixedChineseEnglishDraft) {
  // ModelPaths::conventionalModelDirectory() 依赖 QCoreApplication::applicationName()
  // 推导约定目录（生产环境由 main.cpp 的可执行文件名隐式决定为 "Babel"）；独立测试
  // 进程无 QApplication 实例时需显式设置，否则会解析到错误的路径。
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  babel::slowpath::LlmEngine engine(modelPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实模型文件（" << modelPath.string()
                 << "），跳过真实推理生成验证，见 research.md §7。";
  }

  const QString output = engine.generate(QStringLiteral("wo today bushi very e"));
  EXPECT_FALSE(output.isEmpty());
}

// 回归测试：SessionController 的 DebounceScheduler 每次防抖窗口到期都会在
// **同一个** LlmEngine/llama_context 上调用一次 generate()（见
// SessionController::onSlowPathTriggered），模拟用户持续输入时产生的连续多次调用，
// 验证每次调用前清空 KV cache 后不再出现跨调用状态累积导致的崩溃/生成失败。
TEST(LlmEngineGenerateTest, RepeatedGenerateCallsOnSameEngineDoNotCrash) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));

  const auto modelPath = babel::slowpath::ModelPaths::conventionalModelFilePath();
  babel::slowpath::LlmEngine engine(modelPath);
  if (!engine.modelAvailable()) {
    GTEST_SKIP() << "本地未放置真实模型文件，跳过真实推理生成验证。";
  }

  // 模拟用户逐步打字、每次防抖触发时草稿文本不断增长的真实场景；重复多轮调用，
  // 验证每次调用前清空 KV cache 后各次调用彼此独立、不受此前调用残留状态影响。
  const QStringList baseDrafts = {
      QStringLiteral("wo"),
      QStringLiteral("wo today"),
      QStringLiteral("wo today bushi"),
      QStringLiteral("wo today bushi very"),
      QStringLiteral("wo today bushi very e"),
      QStringLiteral("wo today bushi very e if you e"),
      QStringLiteral("wo today bushi very e if you e we go eat malatang"),
  };
  for (int round = 0; round < 5; ++round) {
    for (const QString &draft : baseDrafts) {
      const QString output = engine.generate(draft);
      EXPECT_FALSE(output.isEmpty()) << "round=" << round << " draft=" << draft.toStdString();
    }
  }
}

