 
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
#include "babel/src/session/SessionController.h"

namespace {

/// 002-lattice-dict-model-switch（T010）：首次启动时，若约定词库目录
/// （`DictionaryPaths::conventionalDictionaryDirectory()`）下不存在任何
/// `.dict.yaml` 文件，则从随包内嵌资源（T001/T002，`babel/content/dictionaries/`）
/// 复制一份初始副本到该目录，确保 FR-001"随包分发"对用户可见、FR-004"纯约定
/// 目录"内容包含官方词库作为起点。目录下已存在词库文件时不覆盖（尊重用户自行
/// 替换/扩充的内容）。
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

  // 与 babel/CMakeLists.txt T002 收录的 content/dictionaries/* 一一对应
  // （qt_add_qml_module URI "quick" -> 资源前缀 ":/qt/qml/quick/"）。
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
    QFile::remove(QString::fromStdString(destPath.string()));  // QFile::copy 要求目标路径不存在
    if (!src.copy(QString::fromStdString(destPath.string()))) {
      spdlog::warn("main: 复制内嵌词库资源到约定目录失败 {} -> {}", fileName, destPath.string());
      continue;
    }
    ++copiedCount;
  }
  spdlog::info("main: 已将 {} 个随包词库资产文件首次部署到约定目录 {}", copiedCount, destDir.string());
}

}  // namespace

int main(int argc, char *argv[]) { 
  std::cout << "current path: " << std::filesystem::current_path() << std::endl;
 

#ifndef NDEBUG
  spdlog::set_level(spdlog::level::info);
#endif

  QApplication app(argc, argv);
  app.setApplicationDisplayName(
    QStringLiteral("This example is powered by qmltc!"));

  deployBundledDictionariesIfNeeded();

  // 浮窗转换器会话控制器（001-floating-ime-converter，T014）：声明顺序早于
  // engine，确保其析构晚于 engine（先析构 QML 引擎，再析构被其引用的单例实例）。
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
