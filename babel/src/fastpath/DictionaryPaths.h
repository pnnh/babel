#pragma once

#include <filesystem>

namespace babel::fastpath {

/// 约定词库目录路径工具（research.md §3/§7.1，镜像
/// `babel::slowpath::ModelPaths::conventionalModelDirectory()` 既有模式）：
/// 用户可将自定义/补充的 Rime `.dict.yaml` 细胞词库文件放置于本约定目录，
/// `RimeDictLoader` 会自动扫描合并（FR-003/FR-004"纯约定目录模式"）。
class DictionaryPaths {
 public:
  /// 约定本地词库目录：平台标准用户数据目录（Qt QStandardPaths::AppDataLocation）
  /// 下的 `dictionaries/` 子目录。
  static std::filesystem::path conventionalDictionaryDirectory();

  /// bugfix: english-word-pinyin-misdetection —— 约定高频英文常用词表文件路径
  /// （`conventionalDictionaryDirectory()` 下的 `common-english-words.txt`，供
  /// `EnglishWordList::loadFromFile()` 加载，内容随包分发，见 `main.cpp`
  /// `deployBundledCommonEnglishWordsIfNeeded()`）。
  static std::filesystem::path conventionalCommonEnglishWordsFilePath();
};

}  // namespace babel::fastpath
