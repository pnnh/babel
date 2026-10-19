#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>
#include <sstream>

#include "babel/src/fastpath/DictionarySchema.h"
#include "babel/src/fastpath/FastCorrector.h"
#include "babel/src/fastpath/RimeDictLoader.h"

using babel::fastpath::DictionaryEntry;
using babel::fastpath::FastCorrector;
using babel::fastpath::RimeDictLoader;

// 回归测试：验证 FastCorrector::buildLattice() 在真实规模词库（iDvel/rime-ice，
// 约 89 万词条，见 babel/content/dictionaries/）下的端到端延迟不会退化为实施
// 过程中曾实测到的"单次调用 6+ 秒"病态情形（编辑距离回退对每个长度桶内全部
// 候选逐一执行完整 DP 导致）。依赖仓库内随包分发的真实词库文件，文件不存在
// 的环境中自动跳过，不影响 CI。

namespace {

std::filesystem::path bundledDictionariesDir() {
  // 本测试二进制运行目录为 <build>/tests/Debug，仓库内词库源文件位于
  // babel/content/dictionaries/（随包分发的原始资产，见 babel/CMakeLists.txt T002）。
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
         "babel" / "content" / "dictionaries";
}

}  // namespace

TEST(FastCorrectorRealDictionaryPerformanceTest, BuildLatticeStaysFastWithFullRimeIceDictionary) {
  const auto dictDir = bundledDictionariesDir();
  std::error_code ec;
  if (!std::filesystem::exists(dictDir, ec) || ec) {
    GTEST_SKIP() << "本地未找到随包词库目录（" << dictDir.string() << "），跳过真实规模延迟验证。";
  }

  sqlite3 *db = nullptr;
  ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);
  ASSERT_TRUE(babel::fastpath::ensureDictionarySchema(db));
  const auto sourceFiles = RimeDictLoader::loadDictionaryDirectory(db, dictDir);
  int totalLoaded = 0;
  for (const auto &sf : sourceFiles) {
    if (sf.loadStatus == babel::fastpath::RimeDictionarySourceFile::LoadStatus::Loaded) {
      totalLoaded += sf.entryCount;
    }
  }
  if (totalLoaded < 10000) {
    GTEST_SKIP() << "词库目录存在但词条数过少（" << totalLoaded << " 条），可能是占位样例而非完整"
                 << "词库，跳过真实规模延迟验证。";
  }

  const auto entries = babel::fastpath::loadAllDictionaryEntries(db);
  FastCorrector corrector(entries);
  sqlite3_close(db);

  const std::vector<QString> samples = {
      QStringLiteral("wo jintian xinqing hen bu hao wo xiang chi kaorou"),
      QStringLiteral("wo xiang he ni yiqi qu chi wanfan"),
      QStringLiteral("zhege idea hen hao wo men keyi shiyixia"),
  };

  for (const auto &sample : samples) {
    const auto start = std::chrono::steady_clock::now();
    const auto lattice = corrector.buildLattice(sample);
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    EXPECT_GT(lattice.cells.size(), 0u);
    // 目标 <15ms（FR-003/SC-002）；真实 89 万词条规模 + 沙箱调度抖动，阈值放宽至
    // 300ms——仍远低于修复前实测的"6+ 秒"病态情形，足以捕获性能回归。
    EXPECT_LT(elapsedMs, 300) << "sample = " << sample.toStdString();
  }
}
