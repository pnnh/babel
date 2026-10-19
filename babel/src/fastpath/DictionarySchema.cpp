#include "babel/src/fastpath/DictionarySchema.h"

#include <spdlog/spdlog.h>

namespace babel::fastpath {

bool ensureDictionarySchema(sqlite3 *db) {
  static constexpr char kCreateTableSql[] =
      "CREATE TABLE IF NOT EXISTS dictionary_entry ("
      "entry_id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "surface_form TEXT NOT NULL,"
      "pinyin_full TEXT,"
      "pinyin_abbrev TEXT,"
      "frequency_weight REAL NOT NULL DEFAULT 1.0,"
      "UNIQUE(surface_form, pinyin_full)"
      ");";
  char *errMsg = nullptr;
  const int rc = sqlite3_exec(db, kCreateTableSql, nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK) {
    spdlog::error("ensureDictionarySchema 失败: {}", errMsg ? errMsg : "unknown");
    sqlite3_free(errMsg);
    return false;
  }
  return true;
}

bool insertDictionaryEntries(sqlite3 *db, const std::vector<DictionaryEntry> &entries) {
  static constexpr char kInsertSql[] =
      "INSERT OR IGNORE INTO dictionary_entry (surface_form, pinyin_full, pinyin_abbrev, frequency_weight) "
      "VALUES (?, ?, ?, ?);";
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, kInsertSql, -1, &stmt, nullptr) != SQLITE_OK) {
    spdlog::error("insertDictionaryEntries prepare 失败: {}", sqlite3_errmsg(db));
    return false;
  }
  bool ok = true;
  for (const auto &entry : entries) {
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, entry.surfaceForm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.pinyinFull.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entry.pinyinAbbrev.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, entry.frequencyWeight);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      spdlog::error("insertDictionaryEntries step 失败: {}", sqlite3_errmsg(db));
      ok = false;
    }
  }
  sqlite3_finalize(stmt);
  return ok;
}

std::vector<DictionaryEntry> loadAllDictionaryEntries(sqlite3 *db) {
  std::vector<DictionaryEntry> result;
  static constexpr char kSelectSql[] =
      "SELECT entry_id, surface_form, pinyin_full, pinyin_abbrev, frequency_weight FROM dictionary_entry;";
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, kSelectSql, -1, &stmt, nullptr) != SQLITE_OK) {
    spdlog::error("loadAllDictionaryEntries prepare 失败: {}", sqlite3_errmsg(db));
    return result;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    DictionaryEntry entry;
    entry.entryId = sqlite3_column_int64(stmt, 0);
    if (const auto *text = sqlite3_column_text(stmt, 1)) {
      entry.surfaceForm = reinterpret_cast<const char *>(text);
    }
    if (const auto *text = sqlite3_column_text(stmt, 2)) {
      entry.pinyinFull = reinterpret_cast<const char *>(text);
    }
    if (const auto *text = sqlite3_column_text(stmt, 3)) {
      entry.pinyinAbbrev = reinterpret_cast<const char *>(text);
    }
    entry.frequencyWeight = sqlite3_column_double(stmt, 4);
    result.push_back(std::move(entry));
  }
  sqlite3_finalize(stmt);
  return result;
}

std::vector<DictionaryEntry> builtinSeedDictionary() {
  // 最小内置种子词典：覆盖 quickstart.md 验证场景一的典型中英拼音混排样例
  // （如 "wo xiang shuo zhege idea hen hao"）。生产环境词库由用户/运维按需扩充，
  // 不随本特性引入网络下载能力（Assumptions：全程本地离线运行）。
  return {
      {0, "我", "wo", "w", 100.0},
      {0, "想", "xiang", "x", 100.0},
      {0, "说", "shuo", "s", 100.0},
      {0, "这个", "zhege", "zg", 100.0},
      {0, "这", "zhe", "z", 90.0},
      {0, "个", "ge", "g", 90.0},
      {0, "很", "hen", "h", 100.0},
      {0, "好", "hao", "h", 100.0},
      {0, "的", "de", "d", 100.0},
      {0, "是", "shi", "s", 100.0},
      {0, "不", "bu", "b", 100.0},
      {0, "了", "le", "l", 100.0},
      {0, "在", "zai", "z", 100.0},
      {0, "有", "you", "y", 100.0},
      {0, "idea", "", "", 80.0},
  };
}

}  // namespace babel::fastpath
