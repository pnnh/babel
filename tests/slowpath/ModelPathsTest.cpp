#include "babel/src/slowpath/ModelPaths.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <cstdlib>
#include <filesystem>

using babel::slowpath::ModelPaths;

class ModelPathsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 暂存原有环境变量
    const char *orig = std::getenv("BABEL_MODEL_PATH");
    if (orig) {
      m_originalEnv = orig;
    }
#if defined(_WIN32)
    _putenv("BABEL_MODEL_PATH=");
#else
    unsetenv("BABEL_MODEL_PATH");
#endif
  }

  void TearDown() override {
    if (m_originalEnv.has_value()) {
#if defined(_WIN32)
      _putenv_s("BABEL_MODEL_PATH", m_originalEnv.value().c_str());
#else
      setenv("BABEL_MODEL_PATH", m_originalEnv.value().c_str(), 1);
#endif
    } else {
#if defined(_WIN32)
      _putenv("BABEL_MODEL_PATH=");
#else
      unsetenv("BABEL_MODEL_PATH");
#endif
    }
  }

 private:
  std::optional<std::string> m_originalEnv;
};

TEST_F(ModelPathsTest, ProductionAppNameResolvesToLocalModels) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));
  const auto dir = ModelPaths::conventionalModelDirectory();
  EXPECT_TRUE(dir.string().find(".local") != std::string::npos);
  EXPECT_EQ(dir.filename().string(), "models");
}

TEST_F(ModelPathsTest, ConventionalModelFilePathHasExpectedFilename) {
  QCoreApplication::setApplicationName(QStringLiteral("Babel"));
  const auto path = ModelPaths::conventionalModelFilePath();
  EXPECT_EQ(path.filename().string(), "qwen2.5-3b-instruct-q4_k_m.gguf");
}

TEST_F(ModelPathsTest, ResolveModelFilePathPrioritizesEnvironmentVariable) {
  const std::string customPath = "/tmp/custom-test-model.gguf";
#if defined(_WIN32)
  _putenv_s("BABEL_MODEL_PATH", customPath.c_str());
#else
  setenv("BABEL_MODEL_PATH", customPath.c_str(), 1);
#endif

  const auto resolved = ModelPaths::resolveModelFilePath();
  EXPECT_EQ(resolved.string(), customPath);
}

TEST_F(ModelPathsTest, ResolveModelFilePathExpandsTilde) {
  const std::string tildePath = "~/.local/models/custom.gguf";
#if defined(_WIN32)
  _putenv_s("BABEL_MODEL_PATH", tildePath.c_str());
#else
  setenv("BABEL_MODEL_PATH", tildePath.c_str(), 1);
#endif

  const auto resolved = ModelPaths::resolveModelFilePath();
  EXPECT_NE(resolved.string().front(), '~');
  EXPECT_TRUE(resolved.string().find("custom.gguf") != std::string::npos);
}

