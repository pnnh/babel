#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "babel/src/fastpath/DictionarySchema.h"

namespace babel::fastpath {

/// 词库源文件加载诊断记录（data-model.md §4，内部诊断用途，不面向用户展示）。
struct RimeDictionarySourceFile {
  enum class LoadStatus {
    Loaded,   ///< 解析成功，词条已写入 sqlite3
    Skipped,  ///< 解析失败（格式不符合规范），该文件被跳过，不中断其余文件加载
    Aborted,  ///< 本特性不使用，仅为结构兼容保留（research.md §4.1）
  };

  std::filesystem::path filePath;
  std::optional<std::string> frontmatterName;  ///< frontmatter `name:` 字段值（若存在）
  int entryCount = 0;                          ///< 该文件贡献的词条数（解析成功时）
  LoadStatus loadStatus = LoadStatus::Skipped;
};

/// Rime `.dict.yaml` 词库文件加载器（research.md §2：手写最小化解析器，零新增依赖）。
///
/// 仅识别本特性实际需要的最小结构：
/// - frontmatter 边界：首个独立一行的 `---` 开始，首个独立一行的 `...` 结束；
///   frontmatter 内容本身（含嵌套列表、行内注释、被注释掉的行）仅被跳过，不消费；
/// - frontmatter 之外（含 `...` 之后的数据区）以 `#` 开头的行与空白行均视为注释/
///   空行，忽略；
/// - 数据行：按制表符分隔的 "词条\t拼音（音节间以空格分隔）\t权重" 三列，拼音列
///   在写入 `DictionaryEntry.pinyinFull` 前去除音节间空格（如 "shao kao" ->
///   "shaokao"，与 `PinyinTrie`/`FastCorrector` 既有"连续拼音串"键约定一致），
///   `pinyinAbbrev` 由每个音节首字母拼接产生（如 "shaokao" -> "sk"）。
class RimeDictLoader {
 public:
  /// 解析单个 `.dict.yaml` 文件的完整文本内容，返回其贡献的词条列表；若格式不符合
  /// 预期（如找不到 frontmatter 结束边界 `...`），返回空列表且
  /// `outSourceFile->loadStatus = Skipped`。`outSourceFile` 可为 nullptr（调用方不关
  /// 心诊断记录时）。
  static std::vector<DictionaryEntry> parseDictionaryFileContent(const std::string &content,
                                                                   RimeDictionarySourceFile *outSourceFile);

  /// 扫描 `dir` 目录下全部 `*.dict.yaml` 文件（按文件名字典序排序后依次处理），解析
  /// 并通过 `insertDictionaryEntries()` 写入 `db`（`INSERT OR IGNORE` 语义：先加载
  /// 文件中的重复词条权重生效，data-model.md §4.2），返回每个文件对应的加载诊断记
  /// 录。目录不存在/为空时返回空列表，不视为错误。
  static std::vector<RimeDictionarySourceFile> loadDictionaryDirectory(sqlite3 *db,
                                                                        const std::filesystem::path &dir);
};

}  // namespace babel::fastpath
