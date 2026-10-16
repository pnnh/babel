 
#include <QDebug>
#include <QQmlDebuggingEnabler>
#include <QtQuick>
#include <iostream>
#include <spdlog/spdlog.h>

#include <QApplication>
#include <QTimer>

#include <thread>

int main(int argc, char *argv[]) { 
  std::cout << "current path: " << std::filesystem::current_path() << std::endl;
 

#ifndef NDEBUG
  spdlog::set_level(spdlog::level::info);
#endif

  QApplication app(argc, argv);
  app.setApplicationDisplayName(
    QStringLiteral("This example is powered by qmltc!"));

  QQmlApplicationEngine engine;
  engine.loadFromModule("quick", "Main");

  const auto &rootObjects = engine.rootObjects();
  if (rootObjects.isEmpty()) {
    qWarning() << "No root objects loaded!";
    return -1;
  }


  return app.exec();
}
