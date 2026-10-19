#include "babel/src/slowpath/ModelPaths.h"

#include <QStandardPaths>
#include <QString>

namespace babel::slowpath {

std::filesystem::path ModelPaths::conventionalModelDirectory() {
  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return std::filesystem::path(appData.toStdString()) / "models";
}

std::filesystem::path ModelPaths::conventionalModelFilePath() {
  return conventionalModelDirectory() / "qwen2.5-3b-instruct-q4_k_m.gguf";
}

}  // namespace babel::slowpath
