#include "babel/src/slowpath/ModelPaths.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QString>
#include <cstdlib>
#include <iostream>

namespace babel::slowpath {

namespace {

std::filesystem::path expandTilde(const std::string &pathStr) {
  if (pathStr.empty()) {
    return {};
  }
  if (pathStr[0] == '~' && (pathStr.size() == 1 || pathStr[1] == '/' || pathStr[1] == '\\')) {
    const char *home = std::getenv("HOME");
#if defined(_WIN32)
    if (!home) {
      home = std::getenv("USERPROFILE");
    }
#endif
    if (home) {
      return std::filesystem::path(home) / pathStr.substr(pathStr.size() > 1 ? 2 : 1);
    }
  }
  return std::filesystem::path(pathStr);
}

}  // namespace

std::filesystem::path ModelPaths::conventionalModelDirectory() {
  // 单元测试隔离安全保护：若检测到测试应用名称，使用隔离的临时 AppDataLocation，
  // 防止测试用例的 std::filesystem::remove_all() 误清空用户真实的 ~/.local/models 目录。
  const QString appName = QCoreApplication::applicationName();
  if (!appName.isEmpty() && appName.contains(QStringLiteral("Test"))) {
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return std::filesystem::path(appData.toStdString()) / "models";
  }

  const char *home = std::getenv("HOME");
#if defined(_WIN32)
  if (!home) {
    home = std::getenv("USERPROFILE");
  }
#endif
  if (home && home[0] != '\0') {
    return std::filesystem::path(home) / ".local" / "models";
  }
  const QString homeLoc = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  if (!homeLoc.isEmpty()) {
    return std::filesystem::path(homeLoc.toStdString()) / ".local" / "models";
  }
  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return std::filesystem::path(appData.toStdString()) / "models";
}

std::filesystem::path ModelPaths::conventionalModelFilePath() {
  return conventionalModelDirectory() / "qwen2.5-3b-instruct-q4_k_m.gguf";
}

std::filesystem::path ModelPaths::resolveModelFilePath() {
  const char *envPath = std::getenv("BABEL_MODEL_PATH");
  if (envPath && envPath[0] != '\0') {
    return expandTilde(envPath);
  }

  const auto standardPath = conventionalModelFilePath();
  std::error_code ec;
  if (std::filesystem::exists(standardPath, ec) && !ec) {
    return standardPath;
  }

  // 历史 AppDataLocation 回退兼容（~/Library/Application Support/Babel/models）
  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const auto legacyPath = std::filesystem::path(appData.toStdString()) / "models" / "qwen2.5-3b-instruct-q4_k_m.gguf";
  if (std::filesystem::exists(legacyPath, ec) && !ec) {
    std::cerr << "[ModelPaths] Warning: Using legacy model path: " << legacyPath.string()
              << ". Please migrate to ~/.local/models/" << std::endl;
    return legacyPath;
  }

  return standardPath;
}

}  // namespace babel::slowpath
