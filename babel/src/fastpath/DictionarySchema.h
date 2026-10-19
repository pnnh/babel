#pragma once

#include <cstdint>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace babel::fastpath {

/// 快流词典条目（data-model.md §7），持久化于 sqlite3（research.md §5）。
struct DictionaryEntry {
  int64_t entryId = 0;
  std::string surfaceForm;    ///< 规范化后的目标词形（中文词或英文词）
  std::string pinyinFull;     ///< 全拼，供拼音匹配；可为空
  std::string pinyinAbbrev;   ///< 简拼（首字母串），供简拼匹配；可为空
  double frequencyWeight = 1.0;  ///< 词频权重，参与编辑距离候选排序
};

/// 在给定 sqlite3 连接上建立 `dictionary_entry` 表（若不存在）。
bool ensureDictionarySchema(sqlite3 *db);

/// 写入一批词典条目（已存在的 surfaceForm+pinyinFull 组合将被忽略，不做更新）。
bool insertDictionaryEntries(sqlite3 *db, const std::vector<DictionaryEntry> &entries);

/// 读取全部词典条目，供 PinyinTrie 构建索引使用。
std::vector<DictionaryEntry> loadAllDictionaryEntries(sqlite3 *db);

/// 内置最小种子词典（中英拼音混排高频示例词，供模型文件/完整词库缺失时的 MVP 兜底）。
/// 不依赖网络下载的外部词库文件，仅覆盖 quickstart.md 验证场景的典型样例。
std::vector<DictionaryEntry> builtinSeedDictionary();

}  // namespace babel::fastpath
