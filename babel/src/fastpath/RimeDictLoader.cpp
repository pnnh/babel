#include "babel/src/fastpath/RimeDictLoader.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace babel::fastpath {

namespace {

std::string rtrim(const std::string &s) {
  std::size_t end = s.size();
  while (end > 0 && (s[end - 1] == '\r' || s[end - 1] == '\n')) {
    --end;
  }
  return s.substr(0, end);
}

std::string trim(const std::string &s) {
  std::size_t begin = 0;
  std::size_t end = s.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(begin, end - begin);
}

std::vector<std::string> splitLines(const std::string &content) {
  std::vector<std::string> lines;
  std::size_t pos = 0;
  while (pos <= content.size()) {
    const std::size_t next = content.find('\n', pos);
    if (next == std::string::npos) {
      lines.push_back(rtrim(content.substr(pos)));
      break;
    }
    lines.push_back(rtrim(content.substr(pos, next - pos)));
    pos = next + 1;
  }
  return lines;
}

/// 将 Rime 拼音列（音节间以空格分隔，如 "shao kao"）拆分为全拼（无空格拼接，如
/// "shaokao"）与简拼（各音节首字母拼接，如 "sk"）。
void derivePinyinForms(const std::string &pinyinColumn, std::string *outFull, std::string *outAbbrev) {
  std::istringstream iss(pinyinColumn);
  std::string syllable;
  while (iss >> syllable) {
    *outFull += syllable;
    if (!syllable.empty()) {
      outAbbrev->push_back(syllable.front());
    }
  }
}

/// 从 frontmatter 顶层行中提取 `name:` 字段值（若存在）；仅识别顶层（无前导空白）
/// 的简单标量写法 `name: value`（研究阶段已核实 rime-ice 的实际写法），不尝试解
/// 析嵌套结构中可能同名的字段。
std::optional<std::string> extractFrontmatterName(const std::vector<std::string> &frontmatterLines) {
  for (const auto &rawLine : frontmatterLines) {
    if (rawLine.empty() || std::isspace(static_cast<unsigned char>(rawLine.front()))) {
      continue;  // 跳过嵌套/缩进行（如 import_tables 下的列表项）
    }
    static constexpr char kPrefix[] = "name:";
    if (rawLine.rfind(kPrefix, 0) == 0) {
      std::string value = trim(rawLine.substr(std::char_traits<char>::length(kPrefix)));
      // 去除可能的引号包裹。
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
      }
      return value;
    }
  }
  return std::nullopt;
}

}  // namespace

std::vector<DictionaryEntry> RimeDictLoader::parseDictionaryFileContent(const std::string &content,
                                                                         RimeDictionarySourceFile *outSourceFile) {
  const std::vector<std::string> lines = splitLines(content);

  // 1) 定位 frontmatter 起始边界：首个独立一行、trim 后恰为 "---" 的行。
  std::size_t frontmatterStart = lines.size();
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (trim(lines[i]) == "---") {
      frontmatterStart = i;
      break;
    }
  }
  if (frontmatterStart == lines.size()) {
    if (outSourceFile) {
      outSourceFile->loadStatus = RimeDictionarySourceFile::LoadStatus::Skipped;
      outSourceFile->entryCount = 0;
    }
    spdlog::warn("[RimeDictLoader] skip malformed file reason=frontmatter_start_not_found");
    return {};
  }

  // 2) 定位 frontmatter 结束边界：frontmatterStart 之后首个独立一行、trim 后恰为
  //    "..." 的行。frontmatter 内部任意内容（含嵌套列表、行内注释、被注释掉的
  //    行）均原样跳过，不解析（research.md §2 方案 1）。
  std::size_t frontmatterEnd = lines.size();
  for (std::size_t i = frontmatterStart + 1; i < lines.size(); ++i) {
    if (trim(lines[i]) == "...") {
      frontmatterEnd = i;
      break;
    }
  }
  if (frontmatterEnd == lines.size()) {
    if (outSourceFile) {
      outSourceFile->loadStatus = RimeDictionarySourceFile::LoadStatus::Skipped;
      outSourceFile->entryCount = 0;
    }
    spdlog::warn("[RimeDictLoader] skip malformed file reason=frontmatter_end_not_found");
    return {};
  }

  const std::vector<std::string> frontmatterLines(lines.begin() + static_cast<std::ptrdiff_t>(frontmatterStart) + 1,
                                                    lines.begin() + static_cast<std::ptrdiff_t>(frontmatterEnd));
  const std::optional<std::string> frontmatterName = extractFrontmatterName(frontmatterLines);

  // 3) 数据区（"..." 之后）：逐行解析 "词条\t拼音\t权重" 三列 TSV；`#` 开头/空白
  //    行视为注释，忽略；非三列的行（如 Rime 原生扩展语法的单列/双列写法）静默
  //    跳过该行（不影响其余行解析，不触发整文件 Skipped）。
  std::vector<DictionaryEntry> entries;
  for (std::size_t i = frontmatterEnd + 1; i < lines.size(); ++i) {
    const std::string &rawLine = lines[i];
    const std::string trimmed = trim(rawLine);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }
    std::vector<std::string> columns;
    std::size_t start = 0;
    while (true) {
      const std::size_t tab = rawLine.find('\t', start);
      if (tab == std::string::npos) {
        columns.push_back(rawLine.substr(start));
        break;
      }
      columns.push_back(rawLine.substr(start, tab - start));
      start = tab + 1;
    }
    if (columns.size() != 3) {
      continue;  // 非本特性约定的三列词条行，跳过该行
    }
    const std::string &surfaceForm = columns[0];
    const std::string &pinyinColumn = columns[1];
    const std::string &weightColumn = columns[2];
    if (surfaceForm.empty()) {
      continue;
    }
    double weight = 0.0;
    try {
      weight = std::stod(weightColumn);
    } catch (const std::exception &) {
      continue;  // 权重列不是合法数值，跳过该行
    }

    DictionaryEntry entry;
    entry.surfaceForm = surfaceForm;
    derivePinyinForms(pinyinColumn, &entry.pinyinFull, &entry.pinyinAbbrev);
    entry.frequencyWeight = weight;
    entries.push_back(std::move(entry));
  }

  if (outSourceFile) {
    outSourceFile->loadStatus = RimeDictionarySourceFile::LoadStatus::Loaded;
    outSourceFile->frontmatterName = frontmatterName;
    outSourceFile->entryCount = static_cast<int>(entries.size());
  }
  return entries;
}

std::vector<RimeDictionarySourceFile> RimeDictLoader::loadDictionaryDirectory(sqlite3 *db,
                                                                               const std::filesystem::path &dir) {
  std::vector<RimeDictionarySourceFile> sources;
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || ec || !std::filesystem::is_directory(dir, ec) || ec) {
    return sources;
  }

  std::vector<std::filesystem::path> dictFiles;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string fileName = entry.path().filename().string();
    static constexpr char kSuffix[] = ".dict.yaml";
    static constexpr std::size_t kSuffixLen = std::char_traits<char>::length(kSuffix);
    if (fileName.size() > kSuffixLen && fileName.compare(fileName.size() - kSuffixLen, kSuffixLen, kSuffix) == 0) {
      dictFiles.push_back(entry.path());
    }
  }
  // data-model.md §4.2：按文件名字典序依次加载，先加载者在 INSERT OR IGNORE 语义
  // 下权重生效。
  std::sort(dictFiles.begin(), dictFiles.end(),
            [](const auto &a, const auto &b) { return a.filename().string() < b.filename().string(); });

  for (const auto &path : dictFiles) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
      RimeDictionarySourceFile source;
      source.filePath = path;
      source.loadStatus = RimeDictionarySourceFile::LoadStatus::Skipped;
      spdlog::warn("[RimeDictLoader] skip malformed file path={} reason=cannot_open_file", path.string());
      sources.push_back(std::move(source));
      continue;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    const std::string content = oss.str();

    RimeDictionarySourceFile source;
    source.filePath = path;
    const auto entries = parseDictionaryFileContent(content, &source);
    if (source.loadStatus == RimeDictionarySourceFile::LoadStatus::Skipped) {
      spdlog::warn("[RimeDictLoader] skip malformed file path={} reason=frontmatter_boundary_not_found",
                   path.string());
    } else if (!entries.empty()) {
      insertDictionaryEntries(db, entries);
    }
    sources.push_back(std::move(source));
  }
  return sources;
}

}  // namespace babel::fastpath
