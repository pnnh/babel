#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>

#include "babel/src/fastpath/DictionarySchema.h"
#include "babel/src/fastpath/RimeDictLoader.h"

using babel::fastpath::DictionaryEntry;
using babel::fastpath::RimeDictLoader;
using babel::fastpath::RimeDictionarySourceFile;

// T008 [US1]：RimeDictLoader 单元测试（data-model.md §4，research.md §1/§2/§4.1/§4.2）。
// 先写测试，确认其在 RimeDictLoader 实现完成前处于 FAIL 状态。

namespace {

/// 简化版 `.dict.yaml` 样例：frontmatter 边界 + 注释行 + 制表符数据行，贴合
/// research.md §1 核实的 base.dict.yaml 结构（但体量精简，便于单元测试）。
constexpr char kSimpleDictContent[] =
    "# Rime dictionary\n"
    "# encoding: utf-8\n"
    "#\n"
    "# ------- 基础词库 -------\n"
    "#\n"
    "---\n"
    "name: base\n"
    "version: \"2026-09-20\"\n"
    "sort: by_weight\n"
    "...\n"
    "# +_+\n"
    "你好\tni hao\t100\n"
    "\n"
    "# 真实样例（research.md §1 已核实 base.dict.yaml 原文行，FR-001 背景场景词）：\n"
    "烤肉\tkao rou\t101498\n"
    "烧烤\tshao kao\t101200\n";

/// research.md §1 已核实的 rime_ice.dict.yaml 真实样例：frontmatter 内含嵌套列表
/// （`import_tables:` 下的 `- cn_dicts/xxx`）、行内注释（`# 字表`）、被注释掉的整行
/// （`# - cn_dicts/41448 ...`）。解析器 MUST 能正确跳过而不误判 `...` 边界/崩溃。
constexpr char kRimeIceFrontmatterContent[] =
    "# Rime dictionary\n"
    "# encoding: utf-8\n"
    "\n"
    "---\n"
    "name: rime_ice\n"
    "version: \"2026-01-26\"\n"
    "import_tables:\n"
    "  - cn_dicts/8105     # 字表\n"
    "  # - cn_dicts/41448  # 大字表（按需启用）\n"
    "  - cn_dicts/base     # 基础词库\n"
    "  - cn_dicts/ext      # 扩展词库\n"
    "\n"
    "  # 建议把扩展词库放到下面，有重复词条时，最上面的权重生效\n"
    "  # - mydict1           # 挂载配置目录下的 mydict1.dict.yaml 词库文件\n"
    "...\n"
    "\n"
    "# 按需启用：\n"
    "A\tA\n";

/// 格式错误样例：缺少 frontmatter 结束边界 `...`。
constexpr char kMalformedMissingEndBoundary[] =
    "---\n"
    "name: broken\n"
    "你好\tni hao\t100\n";

/// 格式错误样例：完全没有 `---` 起始边界。
constexpr char kMalformedMissingStartBoundary[] = "你好\tni hao\t100\n";

std::filesystem::path makeTempDir(const std::string &suffix) {
  const auto dir = std::filesystem::temp_directory_path() / ("babel_rime_dict_test_" + suffix);
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void writeFile(const std::filesystem::path &path, const std::string &content) {
  std::ofstream ofs(path, std::ios::binary);
  ofs << content;
}

}  // namespace

TEST(RimeDictLoaderTest, ParsesFrontmatterBoundaryAndTsvDataRows) {
  RimeDictionarySourceFile source;
  const auto entries = RimeDictLoader::parseDictionaryFileContent(kSimpleDictContent, &source);

  EXPECT_EQ(source.loadStatus, RimeDictionarySourceFile::LoadStatus::Loaded);
  ASSERT_TRUE(source.frontmatterName.has_value());
  EXPECT_EQ(*source.frontmatterName, "base");
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(source.entryCount, 3);

  const auto find = [&entries](const std::string &surface) -> const DictionaryEntry * {
    for (const auto &e : entries) {
      if (e.surfaceForm == surface) return &e;
    }
    return nullptr;
  };

  const auto *nihao = find("你好");
  ASSERT_NE(nihao, nullptr);
  EXPECT_EQ(nihao->pinyinFull, "nihao");
  EXPECT_DOUBLE_EQ(nihao->frequencyWeight, 100.0);
}

// 真实样例：验证此前因词典缺失被错误回退匹配的 "kaorou"/"shaokao" 可被正确解析
// 出真实候选（spec Independent Test，quickstart.md 验证场景一）。
TEST(RimeDictLoaderTest, ParsesRealWorldKaorouAndShaokaoSamples) {
  const auto entries = RimeDictLoader::parseDictionaryFileContent(kSimpleDictContent, nullptr);

  const auto find = [&entries](const std::string &surface) -> const DictionaryEntry * {
    for (const auto &e : entries) {
      if (e.surfaceForm == surface) return &e;
    }
    return nullptr;
  };

  const auto *kaorou = find("烤肉");
  ASSERT_NE(kaorou, nullptr);
  EXPECT_EQ(kaorou->pinyinFull, "kaorou");
  EXPECT_EQ(kaorou->pinyinAbbrev, "kr");

  const auto *shaokao = find("烧烤");
  ASSERT_NE(shaokao, nullptr);
  EXPECT_EQ(shaokao->pinyinFull, "shaokao");
  EXPECT_EQ(shaokao->pinyinAbbrev, "sk");
}

TEST(RimeDictLoaderTest, SkipsNestedListInlineCommentAndCommentedOutLinesInFrontmatter) {
  RimeDictionarySourceFile source;
  const auto entries = RimeDictLoader::parseDictionaryFileContent(kRimeIceFrontmatterContent, &source);

  EXPECT_EQ(source.loadStatus, RimeDictionarySourceFile::LoadStatus::Loaded);
  ASSERT_TRUE(source.frontmatterName.has_value());
  EXPECT_EQ(*source.frontmatterName, "rime_ice");
  // 数据区 "A\tA" 仅两列（非本特性要求的三列 TSV 词条/拼音/权重），不计入词条，
  // 但解析过程 MUST NOT 崩溃、MUST NOT 被误判为 Skipped（frontmatter 边界定位正确）。
  EXPECT_TRUE(entries.empty());
}

TEST(RimeDictLoaderTest, MissingEndBoundaryMarksSkippedWithoutCrash) {
  RimeDictionarySourceFile source;
  const auto entries = RimeDictLoader::parseDictionaryFileContent(kMalformedMissingEndBoundary, &source);
  EXPECT_EQ(source.loadStatus, RimeDictionarySourceFile::LoadStatus::Skipped);
  EXPECT_TRUE(entries.empty());
}

TEST(RimeDictLoaderTest, MissingStartBoundaryMarksSkippedWithoutCrash) {
  RimeDictionarySourceFile source;
  const auto entries = RimeDictLoader::parseDictionaryFileContent(kMalformedMissingStartBoundary, &source);
  EXPECT_EQ(source.loadStatus, RimeDictionarySourceFile::LoadStatus::Skipped);
  EXPECT_TRUE(entries.empty());
}

TEST(RimeDictLoaderTest, LoadDictionaryDirectoryMergesMultipleFilesAndSkipsMalformedOnes) {
  const auto dir = makeTempDir("merge");
  // 文件名字典序：a_dict.dict.yaml 先于 b_dict.dict.yaml 先于 z_broken.dict.yaml。
  writeFile(dir / "a_dict.dict.yaml",
            "---\nname: a\n...\n你好\tni hao\t50\n重复词\tchong fu ci\t10\n");
  writeFile(dir / "b_dict.dict.yaml",
            "---\nname: b\n...\n重复词\tchong fu ci\t999\n新词\txin ci\t20\n");
  writeFile(dir / "z_broken.dict.yaml", "没有边界的坏文件\n");

  sqlite3 *db = nullptr;
  ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);
  ASSERT_TRUE(babel::fastpath::ensureDictionarySchema(db));

  const auto sources = RimeDictLoader::loadDictionaryDirectory(db, dir);
  ASSERT_EQ(sources.size(), 3u);
  EXPECT_EQ(sources[0].filePath.filename().string(), "a_dict.dict.yaml");
  EXPECT_EQ(sources[0].loadStatus, RimeDictionarySourceFile::LoadStatus::Loaded);
  EXPECT_EQ(sources[1].filePath.filename().string(), "b_dict.dict.yaml");
  EXPECT_EQ(sources[1].loadStatus, RimeDictionarySourceFile::LoadStatus::Loaded);
  EXPECT_EQ(sources[2].filePath.filename().string(), "z_broken.dict.yaml");
  EXPECT_EQ(sources[2].loadStatus, RimeDictionarySourceFile::LoadStatus::Skipped);

  const auto all = babel::fastpath::loadAllDictionaryEntries(db);
  const auto find = [&all](const std::string &surface) -> const DictionaryEntry * {
    for (const auto &e : all) {
      if (e.surfaceForm == surface) return &e;
    }
    return nullptr;
  };

  // data-model.md §4.2：先加载文件（a_dict）中的重复词条权重生效，后加载文件
  // （b_dict）中的重复词条被 INSERT OR IGNORE 忽略。
  const auto *dup = find("重复词");
  ASSERT_NE(dup, nullptr);
  EXPECT_DOUBLE_EQ(dup->frequencyWeight, 10.0);

  EXPECT_NE(find("你好"), nullptr);
  EXPECT_NE(find("新词"), nullptr);

  std::filesystem::remove_all(dir);
  sqlite3_close(db);
}

TEST(RimeDictLoaderTest, LoadDictionaryDirectoryOnMissingDirectoryReturnsEmptyWithoutError) {
  sqlite3 *db = nullptr;
  ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);
  ASSERT_TRUE(babel::fastpath::ensureDictionarySchema(db));

  const auto missingDir = std::filesystem::temp_directory_path() / "babel_rime_dict_test_does_not_exist";
  std::filesystem::remove_all(missingDir);

  const auto sources = RimeDictLoader::loadDictionaryDirectory(db, missingDir);
  EXPECT_TRUE(sources.empty());

  sqlite3_close(db);
}
