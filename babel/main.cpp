 
#include <QDebug>
#include <QQmlDebuggingEnabler>
#include <QtQuick>
#include <iostream>
#include <spdlog/spdlog.h>

#include <QApplication>
#include <QTimer>

#include <thread>

#include "babel/src/session/SessionController.h"

int main(int argc, char *argv[]) { 
  std::cout << "current path: " << std::filesystem::current_path() << std::endl;
 

#ifndef NDEBUG
  spdlog::set_level(spdlog::level::info);
#endif

  QApplication app(argc, argv);
  app.setApplicationDisplayName(
    QStringLiteral("This example is powered by qmltc!"));

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
