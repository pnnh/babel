#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "babel/src/slowpath/LlmEngine.h"

using babel::slowpath::LlmEngine;

// T048 [US3]：LlmEngine 模型文件缺失/损坏检测测试（modelAvailable=false，对应 FR-011）。
// 本测试不依赖真实 Qwen2.5-3B-Instruct GGUF 模型文件（该文件按 research.md §7
// 委派结果需由用户手动下载放置，不随本仓库/CI 分发），恰好对应 FR-011 规定的
// "模型文件缺失"这一预期可能状态，无需额外环境准备即可验证。

TEST(LlmEngineUnavailableTest, MissingModelFileYieldsModelUnavailable) {
  const std::filesystem::path missingPath = std::filesystem::temp_directory_path() / "babel_test_missing_model.gguf";
  std::filesystem::remove(missingPath);  // 确保确实不存在
  LlmEngine engine(missingPath);
  EXPECT_FALSE(engine.modelAvailable());
}

TEST(LlmEngineUnavailableTest, EmptyModelFileYieldsModelUnavailable) {
  const std::filesystem::path emptyPath = std::filesystem::temp_directory_path() / "babel_test_empty_model.gguf";
  {
    std::ofstream file(emptyPath, std::ios::trunc);
  }
  LlmEngine engine(emptyPath);
  EXPECT_FALSE(engine.modelAvailable());
  std::filesystem::remove(emptyPath);
}

TEST(LlmEngineUnavailableTest, UnavailableEngineGenerateReturnsEmptyWithoutCrashing) {
  const std::filesystem::path missingPath = std::filesystem::temp_directory_path() / "babel_test_missing_model2.gguf";
  std::filesystem::remove(missingPath);
  LlmEngine engine(missingPath);
  ASSERT_FALSE(engine.modelAvailable());
  EXPECT_TRUE(engine.generate(QStringLiteral("草稿")).isEmpty());
}
