#include "babel/src/fastpath/DictionaryPaths.h"

#include <QStandardPaths>
#include <QString>

namespace babel::fastpath {

std::filesystem::path DictionaryPaths::conventionalDictionaryDirectory() {
  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return std::filesystem::path(appData.toStdString()) / "dictionaries";
}

std::filesystem::path DictionaryPaths::conventionalCommonEnglishWordsFilePath() {
  return conventionalDictionaryDirectory() / "common-english-words.txt";
}

}  // namespace babel::fastpath
