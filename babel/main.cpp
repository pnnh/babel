#include <QDebug>
#include <QQmlDebuggingEnabler>
#include <QtQuick>
#include <iostream>
#include <spdlog/spdlog.h>

#include <QApplication>
#include <QFile>
#include <QTimer>

#include <filesystem>
#include <system_error>
#include <thread>

#include "babel/src/fastpath/DictionaryPaths.h"
#include "babel/src/ipc/UnixDomainSocketServer.h"
#include "babel/src/logging/DaemonLogger.h"
#include "babel/src/session/SessionController.h"

namespace {

/// 首次启动时若约定词库目录下不存在任何 .dict.yaml 文件，则从随包内嵌资源复制初始副本
void deployBundledDictionariesIfNeeded() {
  const std::filesystem::path destDir = babel::fastpath::DictionaryPaths::conventionalDictionaryDirectory();

  std::error_code ec;
  bool hasDictFile = false;
  if (std::filesystem::exists(destDir, ec) && !ec) {
    for (const auto &entry : std::filesystem::directory_iterator(destDir, ec)) {
      if (ec) {
        break;
      }
      const std::string name = entry.path().filename().string();
      static constexpr char kSuffix[] = ".dict.yaml";
      static constexpr std::size_t kSuffixLen = std::char_traits<char>::length(kSuffix);
      if (name.size() > kSuffixLen && name.compare(name.size() - kSuffixLen, kSuffixLen, kSuffix) == 0) {
        hasDictFile = true;
        break;
      }
    }
  }
  if (hasDictFile) {
    spdlog::info("main: 约定词库目录已存在词库文件，跳过随包资产首次部署（{}）", destDir.string());
    return;
  }

  std::filesystem::create_directories(destDir, ec);
  if (ec) {
    spdlog::error("main: 创建约定词库目录失败 {}: {}", destDir.string(), ec.message());
    return;
  }

  static constexpr const char *kBundledFiles[] = {
      "base.dict.yaml",
      "ext.dict.yaml",
      "8105.dict.yaml",
      "LICENSE-rime-ice.txt",
      "NOTICE-rime-ice.txt",
  };
  int copiedCount = 0;
  for (const char *fileName : kBundledFiles) {
    QFile src(QStringLiteral(":/qt/qml/quick/content/dictionaries/%1").arg(QString::fromLatin1(fileName)));
    if (!src.exists()) {
      spdlog::warn("main: 内嵌词库资源缺失，跳过 {}", fileName);
      continue;
    }
    const std::filesystem::path destPath = destDir / fileName;
    QFile::remove(QString::fromStdString(destPath.string()));
    if (!src.copy(QString::fromStdString(destPath.string()))) {
      spdlog::warn("main: 复制内嵌词库资源到约定目录失败 {} -> {}", fileName, destPath.string());
      continue;
    }
    ++copiedCount;
  }
  spdlog::info("main: 已将 {} 个随包词库资产文件首次部署到约定目录 {}", copiedCount, destDir.string());
}

/// 独立部署随包高频英文常用词表
void deployBundledCommonEnglishWordsIfNeeded() {
  const std::filesystem::path destDir = babel::fastpath::DictionaryPaths::conventionalDictionaryDirectory();
  const std::filesystem::path destPath = babel::fastpath::DictionaryPaths::conventionalCommonEnglishWordsFilePath();

  std::error_code ec;
  if (std::filesystem::exists(destPath, ec) && !ec) {
    spdlog::info("main: 约定词库目录已存在高频英文常用词表，跳过首次部署（{}）", destPath.string());
    return;
  }

  std::filesystem::create_directories(destDir, ec);
  if (ec) {
    spdlog::error("main: 创建约定词库目录失败 {}: {}", destDir.string(), ec.message());
    return;
  }

  QFile src(QStringLiteral(":/qt/qml/quick/content/dictionaries/common-english-words.txt"));
  if (!src.exists()) {
    spdlog::warn("main: 内嵌高频英文常用词表资源缺失，跳过部署");
    return;
  }
  QFile::remove(QString::fromStdString(destPath.string()));
  if (!src.copy(QString::fromStdString(destPath.string()))) {
    spdlog::warn("main: 复制内嵌高频英文常用词表资源到约定目录失败 -> {}", destPath.string());
    return;
  }
  spdlog::info("main: 已将随包高频英文常用词表首次部署到约定目录 {}", destPath.string());
}

}  // namespace

int main(int argc, char *argv[]) {
  bool isDaemon = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--daemon") {
      isDaemon = true;
      break;
    }
  }

  babel::logging::DaemonLogger::initialize();

  QApplication app(argc, argv);
  app.setApplicationDisplayName(QStringLiteral("Babel"));

  deployBundledDictionariesIfNeeded();
  deployBundledCommonEnglishWordsIfNeeded();

  if (isDaemon) {
    spdlog::info("main: 运行模式: 守护进程 (Daemon Mode)");
    babel::session::SessionController sessionController;
    sessionController.bootstrapPlatformDefaults();

    auto server = std::make_unique<babel::ipc::UnixDomainSocketServer>(nullptr, &app);
    if (!server->start()) {
      spdlog::error("main: 启动 Unix Domain Socket 服务失败");
      return -1;
    }
    return app.exec();
  }

  // Standalone 模式：启动现有 Qt QML 浮窗界面
  spdlog::info("main: 运行模式: 独立浮窗 (Standalone Mode)");
  babel::session::SessionController sessionController;
  sessionController.bootstrapPlatformDefaults();

  QQmlApplicationEngine engine;
  qmlRegisterSingletonInstance<babel::session::SessionController>("quick", 1, 0, "SessionController",
                                                                  &sessionController);
  engine.loadFromModule("quick", "Main");

  const auto &rootObjects = engine.rootObjects();
  if (rootObjects.isEmpty()) {
    qWarning() << "No root objects loaded!";
    return -1;
  }

  return app.exec();
}
